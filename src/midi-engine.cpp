#include "midi-engine.hpp"
#include "plugin-support.h"
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
MidiEngine::MidiEngine()
{
    try {
        m_midi = std::make_unique<RtMidiIn>(
            RtMidi::UNSPECIFIED, "obs-midi-viz");
        m_midi->ignoreTypes(false, true, true); // receive SysEx off, timing off
        MIDI_LOG_INFO("RtMidi backend: %s", m_midi->getApiDisplayName(
            m_midi->getCurrentApi()).c_str());
    } catch (RtMidiError &e) {
        MIDI_LOG_ERR("RtMidi init failed: %s", e.getMessage().c_str());
    }
}

MidiEngine::~MidiEngine()
{
    closePort();
}

// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::string> MidiEngine::portNames()
{
    std::vector<std::string> names;
    if (!m_midi) return names;
    unsigned int count = m_midi->getPortCount();
    names.reserve(count);
    for (unsigned int i = 0; i < count; ++i)
        names.push_back(m_midi->getPortName(i));
    return names;
}

bool MidiEngine::openPort(int portIndex)
{
    closePort();
    if (!m_midi) return false;

    try {
        if (portIndex < 0) {
            // Virtual port — useful for DAW routing
            m_midi->openVirtualPort("obs-midi-viz");
            MIDI_LOG_INFO("Opened virtual MIDI port");
        } else {
            m_midi->openPort((unsigned int)portIndex, "obs-midi-viz");
            MIDI_LOG_INFO("Opened MIDI port %d: %s",
                portIndex, m_midi->getPortName(portIndex).c_str());
        }
        m_midi->setCallback(&MidiEngine::rtmidiCallback, this);
        m_portIndex = portIndex;
        m_open.store(true);
        return true;
    } catch (RtMidiError &e) {
        MIDI_LOG_ERR("Failed to open port %d: %s",
            portIndex, e.getMessage().c_str());
        return false;
    }
}

void MidiEngine::closePort()
{
    if (m_open.load() && m_midi) {
        m_midi->cancelCallback();
        m_midi->closePort();
        m_open.store(false);
        MIDI_LOG_INFO("MIDI port closed");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Called on RtMidi's background thread — only push to queue, never render
// ─────────────────────────────────────────────────────────────────────────────
void MidiEngine::rtmidiCallback(double timestamp,
                                  std::vector<unsigned char> *msg,
                                  void *userData)
{
    if (!msg || msg->size() < 2) return;
    auto *self = static_cast<MidiEngine *>(userData);

    MidiEvent ev;
    ev.timestamp = timestamp;
    uint8_t status = (*msg)[0];
    uint8_t type   = status & 0xF0;
    ev.channel     = status & 0x0F;
    ev.param1      = msg->size() > 1 ? (*msg)[1] : 0;
    ev.param2      = msg->size() > 2 ? (*msg)[2] : 0;

    switch (type) {
    case 0x80: ev.type = MidiEventType::NoteOff;       break;
    case 0x90:
        ev.type = (ev.param2 == 0)
            ? MidiEventType::NoteOff   // velocity 0 = note-off convention
            : MidiEventType::NoteOn;
        break;
    case 0xB0: ev.type = MidiEventType::ControlChange; break;
    case 0xE0: ev.type = MidiEventType::PitchBend;     break;
    default:   ev.type = MidiEventType::Other;         break;
    }

    std::lock_guard<std::mutex> lk(self->m_queueMutex);
    self->m_queue.push(ev);
}

// ─────────────────────────────────────────────────────────────────────────────
// Called from OBS render thread — dispatch queued events to all subscribers
// ─────────────────────────────────────────────────────────────────────────────
void MidiEngine::drainQueue()
{
    // Swap to a local queue to minimise lock hold time
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
