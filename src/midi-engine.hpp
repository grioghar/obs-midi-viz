#pragma once

#include <RtMidi.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// MidiEvent — the single currency passed from RtMidi callback → source renderers
// ─────────────────────────────────────────────────────────────────────────────
enum class MidiEventType { NoteOn, NoteOff, ControlChange, PitchBend, Other };

struct MidiEvent {
    MidiEventType type    = MidiEventType::Other;
    uint8_t       channel = 0;   // 0–15
    uint8_t       param1  = 0;   // note number or CC number
    uint8_t       param2  = 0;   // velocity or CC value
    double        timestamp = 0; // seconds since last event (from RtMidi)
};

using MidiCallback = std::function<void(const MidiEvent &)>;

// ─────────────────────────────────────────────────────────────────────────────
// MidiEngine — singleton, shared across all three source types
// Sources register callbacks; engine dispatches to all of them.
// ─────────────────────────────────────────────────────────────────────────────
class MidiEngine {
public:
    static MidiEngine &instance()
    {
        static MidiEngine eng;
        return eng;
    }

    // List available MIDI input port names
    std::vector<std::string> portNames();

    // Open a port by index (0-based). -1 = open all ports.
    bool openPort(int portIndex);
    void closePort();
    bool isOpen() const { return m_open.load(); }
    int  currentPort() const { return m_portIndex; }

    // Sources subscribe here to receive events on the render thread.
    // Returns a handle that must be passed to unsubscribe().
    uint32_t subscribe(MidiCallback cb);
    void     unsubscribe(uint32_t handle);

    // Called from OBS tick/render — drains the lock-free queue and
    // dispatches to all subscribers. Call once per frame from any source.
    void drainQueue();

private:
    MidiEngine();
    ~MidiEngine();

    static void rtmidiCallback(double timestamp,
                                std::vector<unsigned char> *message,
                                void *userData);

    std::unique_ptr<RtMidiIn> m_midi;
    std::atomic<bool>         m_open{false};
    int                       m_portIndex{-1};

    // Inter-thread queue: RtMidi callback → render thread
    std::mutex            m_queueMutex;
    std::queue<MidiEvent> m_queue;

    // Subscriber registry
    std::mutex                               m_subMutex;
    std::vector<std::pair<uint32_t, MidiCallback>> m_subscribers;
    uint32_t                                 m_nextHandle{1};
};
