#include "midi-engine.hpp"
#include "plugin-support.h"
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
MidiEngine::MidiEngine()
{
    try {
        m_probe = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "obs-midi-viz-probe");
        MIDI_LOG_INFO("RtMidi backend: %s",
            m_probe->getApiDisplayName(m_probe->getCurrentApi()).c_str());
    } catch (RtMidiError &e) {
        MIDI_LOG_ERR("RtMidi probe init failed: %s", e.getMessage().c_str());
    }
}

MidiEngine::~MidiEngine()
{
    closeAll();
}

// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::string> MidiEngine::portNames()
{
    std::vector<std::string> names;
    if (!m_probe) return names;
    unsigned int count = m_probe->getPortCount();
    names.reserve(count);
    for (unsigned int i = 0; i < count; ++i)
        names.push_back(m_probe->getPortName(i));
    return names;
}

// ─────────────────────────────────────────────────────────────────────────────
// openPort — idempotent; each portIndex gets its own RtMidiIn instance so
// multiple devices can be open simultaneously without closing each other.
// The PortCbData struct lives inside std::map, whose nodes are pointer-stable
// after insertion — safe to pass &cbData to RtMidi as the callback userData.
// ─────────────────────────────────────────────────────────────────────────────
bool MidiEngine::openPort(int portIndex)
{
    std::lock_guard<std::mutex> lk(m_portsMutex);

    if (m_ports.count(portIndex))
        return true;  // already open — idempotent

    try {
        // Insert default-constructed entry first so cbData is in its final
        // stable map-node address before we hand the pointer to RtMidi.
        auto &op        = m_ports[portIndex];
        op.cbData       = { this, portIndex };
        op.midi         = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "obs-midi-viz");
        op.midi->ignoreTypes(false, true, true); // pass SysEx; drop timing/active-sense

        op.midi->openPort((unsigned int)portIndex, "obs-midi-viz");
        op.midi->setCallback(&MidiEngine::rtmidiCallback, &op.cbData);

        std::string name = m_probe ? m_probe->getPortName(portIndex) : "?";
        MIDI_LOG_INFO("Opened MIDI port %d: %s", portIndex, name.c_str());
        return true;

    } catch (RtMidiError &e) {
        m_ports.erase(portIndex);
        MIDI_LOG_ERR("Failed to open MIDI port %d: %s", portIndex, e.getMessage().c_str());
        return false;
    }
}

void MidiEngine::closePort(int portIndex)
{
    std::lock_guard<std::mutex> lk(m_portsMutex);
    auto it = m_ports.find(portIndex);
    if (it == m_ports.end()) return;
    // cancelCallback() before erasing so the RtMidi background thread
    // stops firing before the cbData pointer becomes dangling.
    it->second.midi->cancelCallback();
    it->second.midi->closePort();
    m_ports.erase(it);
    MIDI_LOG_INFO("Closed MIDI port %d", portIndex);
}

void MidiEngine::closeAll()
{
    std::lock_guard<std::mutex> lk(m_portsMutex);
    for (auto &[idx, op] : m_ports) {
        op.midi->cancelCallback();
        op.midi->closePort();
    }
    m_ports.clear();
}

bool MidiEngine::isOpen() const
{
    std::lock_guard<std::mutex> lk(m_portsMutex);
    return !m_ports.empty();
}

bool MidiEngine::isPortOpen(int p) const
{
    std::lock_guard<std::mutex> lk(m_portsMutex);
    return m_ports.count(p) > 0;
}

int MidiEngine::currentPort() const
{
    std::lock_guard<std::mutex> lk(m_portsMutex);
    return m_ports.empty() ? -1 : m_ports.begin()->first;
}

// ─────────────────────────────────────────────────────────────────────────────
// RtMidi callback — runs on RtMidi's background thread.
// Only enqueues; never calls subscribers directly.
// userData points to a PortCbData stored inside m_ports (pointer-stable).
// ─────────────────────────────────────────────────────────────────────────────
void MidiEngine::rtmidiCallback(double timestamp,
                                  std::vector<unsigned char> *msg,
                                  void *userData)
{
    if (!msg || msg->size() < 2 || !userData) return;
    auto *pd = static_cast<PortCbData *>(userData);

    MidiEvent ev;
    ev.timestamp = timestamp;
    ev.portIndex = pd->portIndex;

    uint8_t status = (*msg)[0];
    uint8_t type   = status & 0xF0;
    ev.channel     = status & 0x0F;
    ev.param1      = msg->size() > 1 ? (*msg)[1] : 0;
    ev.param2      = msg->size() > 2 ? (*msg)[2] : 0;

    switch (type) {
    case 0x80: ev.type = MidiEventType::NoteOff;       break;
    case 0x90:
        ev.type = (ev.param2 == 0)
            ? MidiEventType::NoteOff   // velocity-0 = note-off convention
            : MidiEventType::NoteOn;
        break;
    case 0xB0: ev.type = MidiEventType::ControlChange; break;
    case 0xE0: ev.type = MidiEventType::PitchBend;     break;
    default:   ev.type = MidiEventType::Other;         break;
    }

    std::lock_guard<std::mutex> lk(pd->engine->m_queueMutex);
    pd->engine->m_queue.push(ev);
}

// ─────────────────────────────────────────────────────────────────────────────
// drainQueue — called from the OBS render thread via video_tick.
// Swaps the queue under lock then dispatches without holding the lock.
// ─────────────────────────────────────────────────────────────────────────────
void MidiEngine::drainQueue()
{
    std::queue<MidiEvent> local;
    {
        std::lock_guard<std::mutex> lk(m_queueMutex);
        std::swap(local, m_queue);
    }

    std::lock_guard<std::mutex> subLk(m_subMutex);
    while (!local.empty()) {
        const MidiEvent &ev = local.front();
        for (auto &[handle, cb] : m_subscribers)
            cb(ev);
        local.pop();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
uint32_t MidiEngine::subscribe(MidiCallback cb)
{
    std::lock_guard<std::mutex> lk(m_subMutex);
    uint32_t handle = m_nextHandle++;
    m_subscribers.emplace_back(handle, std::move(cb));
    return handle;
}

void MidiEngine::unsubscribe(uint32_t handle)
{
    std::lock_guard<std::mutex> lk(m_subMutex);
    m_subscribers.erase(
        std::remove_if(m_subscribers.begin(), m_subscribers.end(),
            [handle](const auto &p){ return p.first == handle; }),
        m_subscribers.end());
}
