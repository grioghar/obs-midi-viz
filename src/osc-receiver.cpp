// osc-receiver.cpp — UDP OSC listener engine for obs-midi-viz Phase 7
//
// Platform abstraction:
//   Windows  — Winsock2 (ws2_32); SOCKET; WSAStartup/Cleanup
//   POSIX    — BSD sockets; int; no init required
//
// Thread model: one background thread per OscReceiver (singleton).
// Background thread calls select() with a 100 ms timeout so it can notice
// the stop flag without blocking indefinitely. The main thread closes the
// socket BEFORE joining, which also interrupts any in-progress recv.
//
// drainQueue() swaps the queue under a brief lock then dispatches without
// holding the lock — same pattern as MidiEngine.

#include "osc-receiver.hpp"
#include "plugin-support.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Platform socket layer
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   using sock_t = SOCKET;
   static constexpr sock_t kInvalidSock = INVALID_SOCKET;
   static void sockClose(sock_t s) { ::closesocket(s); }
   static int  lastSockErr()       { return (int)::WSAGetLastError(); }
   static bool initSockLib()       {
       WSADATA wd; return ::WSAStartup(MAKEWORD(2,2), &wd) == 0;
   }
   static void cleanupSockLib()    { ::WSACleanup(); }
#else
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <netdb.h>
#  include <cerrno>
   using sock_t = int;
   static constexpr sock_t kInvalidSock = -1;
   static void sockClose(sock_t s) { ::close(s); }
   static int  lastSockErr()       { return errno; }
   static bool initSockLib()       { return true; }
   static void cleanupSockLib()    {}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// OSC packet parser
// ─────────────────────────────────────────────────────────────────────────────
static bool oscReadString(const uint8_t *buf, size_t size,
                          size_t &off, std::string &out)
{
    size_t start = off;
    while (off < size && buf[off] != 0) ++off;
    if (off >= size) return false;
    out = std::string(reinterpret_cast<const char *>(buf + start), off - start);
    ++off;                      // consume the null terminator
    off = (off + 3u) & ~3u;    // pad to 4-byte boundary
    return true;
}

static bool oscParseMessage(const uint8_t *buf, size_t size, OscMessage &msg)
{
    if (size < 8) return false;

    size_t off = 0;

    // Address pattern (must start with '/')
    if (!oscReadString(buf, size, off, msg.address)) return false;
    if (msg.address.empty() || msg.address[0] != '/') return false;

    // Type tag string (must start with ',')
    std::string tags;
    if (!oscReadString(buf, size, off, tags)) return false;
    if (tags.empty() || tags[0] != ',') return false;

    // Arguments
    for (size_t ti = 1; ti < tags.size(); ++ti) {
        char t = tags[ti];
        OscArg arg;
        if (t == 'i' || t == 'I') {
            if (off + 4 > size) return false;
            uint32_t raw;
            memcpy(&raw, buf + off, 4);
            arg = OscArg::fromInt((int32_t)ntohl(raw));
            off += 4;
        } else if (t == 'f') {
            if (off + 4 > size) return false;
            uint32_t raw;
            memcpy(&raw, buf + off, 4);
            raw = ntohl(raw);
            float fv;
            memcpy(&fv, &raw, 4);
            arg = OscArg::fromFloat(fv);
            off += 4;
        } else if (t == 's' || t == 'S') {
            std::string sv;
            if (!oscReadString(buf, size, off, sv)) return false;
            arg = OscArg::fromStr(sv);
        } else if (t == 'T') {
            arg = OscArg::fromInt(1);
        } else if (t == 'F') {
            arg = OscArg::fromInt(0);
        } else if (t == 'N') {
            arg = OscArg::fromInt(0);
        } else {
            // Skip blob or other types: read 4-byte size then pad
            if (off + 4 <= size) {
                uint32_t blobSz = ntohl(0); memcpy(&blobSz, buf+off, 4); off += 4;
                off += (blobSz + 3u) & ~3u;
            }
            continue;
        }
        msg.args.push_back(std::move(arg));
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// OSC packet builder
// ─────────────────────────────────────────────────────────────────────────────
static void oscAppendStr(std::vector<uint8_t> &pkt, const char *s)
{
    size_t len = strlen(s);
    for (size_t i = 0; i <= len; ++i) pkt.push_back((uint8_t)s[i]);
    while (pkt.size() % 4 != 0) pkt.push_back(0);
}

static std::vector<uint8_t> oscBuildPacket(const char *address,
                                            const std::vector<OscArg> &args)
{
    std::vector<uint8_t> pkt;
    pkt.reserve(64);

    // Address
    oscAppendStr(pkt, address);

    // Type tags
    std::string tags = ",";
    for (const auto &a : args) {
        switch (a.type) {
        case OscArg::Type::Int32:   tags += 'i'; break;
        case OscArg::Type::Float32: tags += 'f'; break;
        case OscArg::Type::String:  tags += 's'; break;
        }
    }
    oscAppendStr(pkt, tags.c_str());

    // Argument values
    for (const auto &a : args) {
        if (a.type == OscArg::Type::Int32) {
            uint32_t v = htonl((uint32_t)a.i);
            const uint8_t *b = reinterpret_cast<const uint8_t *>(&v);
            pkt.insert(pkt.end(), b, b + 4);
        } else if (a.type == OscArg::Type::Float32) {
            uint32_t raw;
            memcpy(&raw, &a.f, 4);
            raw = htonl(raw);
            const uint8_t *b = reinterpret_cast<const uint8_t *>(&raw);
            pkt.insert(pkt.end(), b, b + 4);
        } else if (a.type == OscArg::Type::String) {
            oscAppendStr(pkt, a.s.c_str());
        }
    }
    return pkt;
}

// ─────────────────────────────────────────────────────────────────────────────
// OscReceiver::Impl
// ─────────────────────────────────────────────────────────────────────────────
struct OscReceiver::Impl {
    std::atomic<bool> running{false};
    std::thread       thread;
    sock_t            sockfd  = kInvalidSock;
    uint16_t          port    = 0;
    bool              winsockOk = false;

    mutable std::mutex          queueMutex;
    std::queue<OscMessage>      queue;

    mutable std::mutex                                    subMutex;
    std::vector<std::pair<uint32_t, OscCallback>>         subs;
    uint32_t                                              nextHandle{1};
};

// ─────────────────────────────────────────────────────────────────────────────
// Background receiver thread
// ─────────────────────────────────────────────────────────────────────────────
static void receiverThread(OscReceiver::Impl *impl)
{
    constexpr size_t kBufSize = 8192;
    std::vector<uint8_t> buf(kBufSize);

    while (impl->running) {
        // select() with 100 ms timeout so we can notice stop flag
        fd_set fds;
        FD_ZERO(&fds);
#ifdef _WIN32
        FD_SET(impl->sockfd, &fds);
#else
        FD_SET(impl->sockfd, &fds);
#endif
        struct timeval tv{};
        tv.tv_sec  = 0;
        tv.tv_usec = 100000;  // 100 ms

#ifdef _WIN32
        int sr = ::select(0, &fds, nullptr, nullptr, &tv);
#else
        int sr = ::select((int)impl->sockfd + 1, &fds, nullptr, nullptr, &tv);
#endif
        if (sr <= 0) continue;
        if (!FD_ISSET(impl->sockfd, &fds)) continue;

#ifdef _WIN32
        int n = ::recv(impl->sockfd, reinterpret_cast<char *>(buf.data()),
                       (int)buf.size(), 0);
#else
        ssize_t n = ::recv(impl->sockfd, buf.data(), buf.size(), 0);
#endif
        if (n <= 0) continue;

        OscMessage msg;
        if (oscParseMessage(buf.data(), (size_t)n, msg)) {
            std::lock_guard<std::mutex> lk(impl->queueMutex);
            impl->queue.push(std::move(msg));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OscReceiver public API
// ─────────────────────────────────────────────────────────────────────────────
OscReceiver &OscReceiver::instance()
{
    static OscReceiver inst;
    return inst;
}

OscReceiver::OscReceiver() : m_impl(new Impl{})
{
    m_impl->winsockOk = initSockLib();
}

OscReceiver::~OscReceiver()
{
    stop();
    if (m_impl->winsockOk) cleanupSockLib();
}

bool OscReceiver::start(uint16_t port)
{
    if (m_impl->running && m_impl->port == port)
        return true;  // already running on the same port

    stop();  // stop any existing listener

    if (!m_impl->winsockOk) {
        MIDI_LOG_ERR("OscReceiver: socket library init failed");
        return false;
    }

    sock_t s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kInvalidSock) {
        MIDI_LOG_ERR("OscReceiver: socket() failed (err %d)", lastSockErr());
        return false;
    }

    // SO_REUSEADDR so re-binding the same port after a restart works
    int yes = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char *>(&yes), sizeof(yes));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (::bind(s, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
        MIDI_LOG_ERR("OscReceiver: bind() port %u failed (err %d)", port, lastSockErr());
        sockClose(s);
        return false;
    }

    m_impl->sockfd  = s;
    m_impl->port    = port;
    m_impl->running = true;
    m_impl->thread  = std::thread(receiverThread, m_impl.get());

    MIDI_LOG_INFO("OscReceiver: listening on UDP port %u", port);
    return true;
}

void OscReceiver::stop()
{
    if (!m_impl->running) return;
    m_impl->running = false;                  // signal thread to exit
    if (m_impl->sockfd != kInvalidSock) {
        sockClose(m_impl->sockfd);            // interrupts select/recv
        m_impl->sockfd = kInvalidSock;
    }
    if (m_impl->thread.joinable())
        m_impl->thread.join();
    m_impl->port = 0;
    MIDI_LOG_INFO("OscReceiver: stopped");
}

bool     OscReceiver::isRunning()   const { return m_impl->running; }
uint16_t OscReceiver::currentPort() const { return m_impl->port; }

bool OscReceiver::send(const char *host, uint16_t dstPort,
                       const char *address,
                       const std::vector<OscArg> &args)
{
    if (!m_impl->winsockOk) return false;
    if (m_impl->sockfd == kInvalidSock) return false;

    auto pkt = oscBuildPacket(address, args);
    if (pkt.empty()) return false;

    struct sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(dstPort);
    if (::inet_pton(AF_INET, host, &dst.sin_addr) != 1) {
        // Try as hostname
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (::getaddrinfo(host, nullptr, &hints, &res) != 0) return false;
        dst.sin_addr = reinterpret_cast<struct sockaddr_in *>(res->ai_addr)->sin_addr;
        ::freeaddrinfo(res);
    }

    int sent = (int)::sendto(m_impl->sockfd,
                             reinterpret_cast<const char *>(pkt.data()),
                             (int)pkt.size(), 0,
                             reinterpret_cast<struct sockaddr *>(&dst),
                             sizeof(dst));
    return sent == (int)pkt.size();
}

uint32_t OscReceiver::subscribe(OscCallback cb)
{
    std::lock_guard<std::mutex> lk(m_impl->subMutex);
    uint32_t h = m_impl->nextHandle++;
    m_impl->subs.emplace_back(h, std::move(cb));
    return h;
}

void OscReceiver::unsubscribe(uint32_t handle)
{
    std::lock_guard<std::mutex> lk(m_impl->subMutex);
    m_impl->subs.erase(
        std::remove_if(m_impl->subs.begin(), m_impl->subs.end(),
            [handle](const auto &p){ return p.first == handle; }),
        m_impl->subs.end());
}

void OscReceiver::drainQueue()
{
    std::queue<OscMessage> local;
    {
        std::lock_guard<std::mutex> lk(m_impl->queueMutex);
        std::swap(local, m_impl->queue);
    }

    std::lock_guard<std::mutex> subLk(m_impl->subMutex);
    while (!local.empty()) {
        const OscMessage &msg = local.front();
        for (auto &[handle, cb] : m_impl->subs)
            cb(msg);
        local.pop();
    }
}
