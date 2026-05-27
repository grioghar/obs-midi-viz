#pragma once

#include <RtMidi.h>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// MidiEvent
// portIndex tells subscribers WHICH physical device sent the event, so a
// piano source with 4 controller slots can filter by port.
// ─────────────────────────────────────────────────────────────────────────────
enum class MidiEventType { NoteOn, NoteOff, ControlChange, PitchBend, Other };

struct MidiEvent {
    MidiEventType        type      = MidiEventType::Other;
    uint8_t              channel   = 0;   // 0–15
    uint8_t              param1    = 0;   // note number or CC number
    uint8_t              param2    = 0;   // velocity or CC value
    double               timestamp = 0;   // seconds since last event (from RtMidi)
    int                  portIndex = -1;  // which port this event came from
    std::vector<uint8_t> raw;             // full raw bytes for SysEx (type==Other, raw[0]==0xF0)
};

using MidiCallback = std::function<void(const MidiEvent &)>;

// ─────────────────────────────────────────────────────────────────────────────
// MidiEngine — singleton.
//
// Multiple ports can be open simultaneously; each has its own RtMidiIn
// instance so different sources can listen to different physical devices.
// openPort() is idempotent — calling it twice for the same index is a no-op.
// Ports stay open until closeAll() (called from the destructor on OBS exit).
// ─────────────────────────────────────────────────────────────────────────────
class MidiEngine {
public:
    static MidiEngine &instance()
    {
        static MidiEngine eng;
        return eng;
    }

    // Enumerate available MIDI input ports (always fresh)
    std::vector<std::string> portNames();

    // Open a port by index. Idempotent — opening an already-open port is safe.
    bool openPort(int portIndex);

    // Close a specific port (or all ports)
    void closePort(int portIndex);
    void closeAll();

    bool isOpen()          const;   // true if at least one port is open
    bool isPortOpen(int p) const;
    int  currentPort()     const;   // first open port index, or -1

    // Subscribe to receive all events from all open ports.
    uint32_t subscribe(MidiCallback cb);
    void     unsubscribe(uint32_t handle);

    // Call from OBS tick — drains queue and dispatches to all subscribers.
    void drainQueue();

private:
    MidiEngine();
    ~MidiEngine();

    // Per-port callback data passed as userData to RtMidi.
    // Must stay pointer-stable after insertion into m_ports (std::map guarantees this).
    struct PortCbData {
        MidiEngine *engine;
        int         portIndex;
    };

    struct OpenPort {
        std::unique_ptr<RtMidiIn> midi;
        PortCbData                cbData;
    };

    static void rtmidiCallback(double timestamp,
                                std::vector<unsigned char> *message,
                                void *userData);

    // Separate probe instance used only for port enumeration; never opened
    std::unique_ptr<RtMidiIn>  m_probe;

    mutable std::mutex         m_portsMutex;
    std::map<int, OpenPort>    m_ports;      // portIndex → open port

    std::mutex                 m_queueMutex;
    std::queue<MidiEvent>      m_queue;

    mutable std::mutex                                   m_subMutex;
    std::vector<std::pair<uint32_t, MidiCallback>>       m_subscribers;
    uint32_t                                             m_nextHandle{1};
};
