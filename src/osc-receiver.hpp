#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// OscReceiver — singleton UDP/OSC listener, parallel to MidiEngine.
//
// Background thread binds a UDP port and parses incoming OSC packets.
// Parsed messages are queued; drainQueue() (called from video_tick) dispatches
// them to all registered subscribers without holding any lock.
//
// send() transmits an OSC packet back to a remote host — used by sources to
// request state from AbletonOSC on startup or on demand.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// OscArg — one argument inside an OSC message.
// Supports int32, float32, and string types (sufficient for AbletonOSC).
// ─────────────────────────────────────────────────────────────────────────────
struct OscArg {
    enum class Type { Int32, Float32, String } type = Type::Int32;
    int32_t     i = 0;
    float       f = 0.0f;
    std::string s;

    static OscArg fromInt  (int32_t v)           { OscArg a; a.type=Type::Int32;   a.i=v; return a; }
    static OscArg fromFloat(float   v)           { OscArg a; a.type=Type::Float32; a.f=v; return a; }
    static OscArg fromStr  (const std::string &v){ OscArg a; a.type=Type::String;  a.s=v; return a; }

    bool isInt()    const { return type == Type::Int32; }
    bool isFloat()  const { return type == Type::Float32; }
    bool isString() const { return type == Type::String; }
    int32_t     asInt()   const { return i; }
    float       asFloat() const { return f; }
    const std::string &asString() const { return s; }
};

// ─────────────────────────────────────────────────────────────────────────────
// OscMessage — a parsed OSC address + argument list.
// ─────────────────────────────────────────────────────────────────────────────
struct OscMessage {
    std::string          address;  // e.g. "/live/song/get/tempo"
    std::vector<OscArg>  args;

    bool has(int idx) const { return idx >= 0 && idx < (int)args.size(); }

    int32_t     intAt   (int i, int32_t     def=0)    const { return (has(i)&&args[i].isInt())    ? args[i].asInt()    : def; }
    float       floatAt (int i, float       def=0.0f) const { return (has(i)&&args[i].isFloat())  ? args[i].asFloat()  : def; }
    const char *strAt   (int i, const char *def="")   const { return (has(i)&&args[i].isString()) ? args[i].asString().c_str() : def; }
};

using OscCallback = std::function<void(const OscMessage &)>;

// ─────────────────────────────────────────────────────────────────────────────
// OscReceiver
// ─────────────────────────────────────────────────────────────────────────────
class OscReceiver {
public:
    static OscReceiver &instance();

    // Start listening on the given port (idempotent if already running on same port).
    // Returns true on success.
    bool start(uint16_t port = 11000);

    // Stop the background thread and close the socket.
    void stop();

    bool     isRunning() const;
    uint16_t currentPort() const;

    // Transmit an OSC message to a remote host:port.
    // address and args are encoded into a valid OSC packet and sent via UDP.
    bool send(const char *host, uint16_t port,
              const char *address,
              const std::vector<OscArg> &args = {});

    // Subscribe / unsubscribe — subscribe returns an opaque handle.
    uint32_t subscribe  (OscCallback cb);
    void     unsubscribe(uint32_t handle);

    // Drain the event queue and dispatch to all subscribers.
    // MUST be called from the OBS render/tick thread (not from the background thread).
    void drainQueue();

    // Pimpl type — forward-declared public so the free background-thread function
    // in osc-receiver.cpp can name it in its parameter signature.  The full
    // definition (and therefore the actual implementation details) lives only in
    // osc-receiver.cpp and is never visible to users of this header.
    struct Impl;

private:
    OscReceiver();
    ~OscReceiver();

    std::unique_ptr<Impl> m_impl;
};
