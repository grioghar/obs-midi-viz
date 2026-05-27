#include "drum-source.hpp"
#include "../midi-engine.hpp"
#include "../plugin-support.h"
#include <obs-module.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// General MIDI percussion map (note 35–81); everything else is nullptr.
// ─────────────────────────────────────────────────────────────────────────────
static const char *GM_DRUM_NAMES[128] = {
    nullptr,nullptr,nullptr,nullptr,nullptr, // 0–4
    nullptr,nullptr,nullptr,nullptr,nullptr, // 5–9
    nullptr,nullptr,nullptr,nullptr,nullptr, // 10–14
    nullptr,nullptr,nullptr,nullptr,nullptr, // 15–19
    nullptr,nullptr,nullptr,nullptr,nullptr, // 20–24
    nullptr,nullptr,nullptr,nullptr,nullptr, // 25–29
    nullptr,nullptr,nullptr,nullptr,nullptr, // 30–34
    "Bass2",    "Bass1",    "Rimshot",   "Snare",      // 35–38
    "Clap",     "Snare2",   "Tom1",      "HH-C",       // 39–42
    "Tom2",     "HH-P",     "Tom3",      "HH-O",       // 43–46
    "Tom4",     "Tom5",     "Crash1",    "Tom6",        // 47–50
    "Ride1",    "China",    "RideBell",  "Tamb",        // 51–54
    "Splash",   "Cowbell",  "Crash2",    "Vibraslap",   // 55–58
    "Ride2",    "HiBongo",  "LoBongo",   "Conga1",      // 59–62
    "Conga2",   "Conga3",   "HiTimb",    "LoTimb",      // 63–66
    "HiAgogo",  "LoAgogo",  "Cabasa",    "Maracas",     // 67–70
    "SWhistle", "LWhistle", "ShortGui",  "LongGui",     // 71–74
    "Claves",   "HiWood",   "LoWood",    "MuteTri",     // 75–78
    "OpenTri",  "MuteHH",   "OpenHH",                   // 79–81
    nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,    // 82–87
    nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,    // 88–93
    nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,    // 94–99
    nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,    // 100–105
    nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,    // 106–111
    nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,    // 112–117
    nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,    // 118–123
    nullptr,nullptr,nullptr,nullptr,                    // 124–127
};

// ─────────────────────────────────────────────────────────────────────────────
// 3×5 pixel bitmap font — digits 0-9 (idx 0-9), A-Z (idx 10-35),
// '-' (36), space (37).  Each byte = one row; bit2=left, bit1=centre, bit0=right.
// ─────────────────────────────────────────────────────────────────────────────
static const uint8_t kFont3x5[38][5] = {
    {7,5,5,5,7}, // 0
    {2,6,2,2,7}, // 1
    {7,1,7,4,7}, // 2
    {7,1,7,1,7}, // 3
    {5,5,7,1,1}, // 4
    {7,4,7,1,6}, // 5
    {7,4,7,5,7}, // 6
    {7,1,2,4,4}, // 7
    {7,5,7,5,7}, // 8
    {7,5,7,1,7}, // 9
    {2,5,7,5,5}, // A
    {6,5,6,5,6}, // B
    {3,4,4,4,3}, // C
    {6,5,5,5,6}, // D
    {7,4,6,4,7}, // E
    {7,4,6,4,4}, // F
    {3,4,7,5,3}, // G
    {5,5,7,5,5}, // H
    {7,2,2,2,7}, // I
    {1,1,1,5,2}, // J
    {5,5,6,5,5}, // K
    {4,4,4,4,7}, // L
    {5,7,5,5,5}, // M
    {5,5,7,5,5}, // N
    {2,5,5,5,2}, // O
    {6,5,6,4,4}, // P
    {2,5,5,7,3}, // Q
    {6,5,6,5,5}, // R
    {3,4,2,1,6}, // S
    {7,2,2,2,2}, // T
    {5,5,5,5,7}, // U
    {5,5,5,2,2}, // V
    {5,5,7,5,5}, // W
    {5,5,2,5,5}, // X
    {5,5,2,2,2}, // Y
    {7,1,2,4,7}, // Z
    {0,0,7,0,0}, // -
    {0,0,0,0,0}, // space
};

static int fontIdx(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    char u = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    if (u >= 'A' && u <= 'Z') return 10 + (u - 'A');
    if (c == '-') return 36;
    return 37; // space / unknown
}

static uint32_t lerp_argb(uint32_t a, uint32_t b, float t)
{
    auto l8 = [t](uint32_t ca, uint32_t cb) -> uint32_t {
        return (uint32_t)((float)ca + ((float)cb - (float)ca) * t);
    };
    return (l8((a>>24)&0xFF,(b>>24)&0xFF)<<24)
         | (l8((a>>16)&0xFF,(b>>16)&0xFF)<<16)
         | (l8((a>> 8)&0xFF,(b>> 8)&0xFF)<< 8)
         |  l8( a     &0xFF, b     &0xFF);
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout modes
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int kLayoutGrid    = 0;   // pad grid (4×4, 8×8, etc.)
static constexpr int kLayoutStepSeq = 1;   // authentic step-sequencer row view

// ─────────────────────────────────────────────────────────────────────────────
// Pad visual styles
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int kPadSquare  = 0;  // fills the full cell rectangle
static constexpr int kPadRounded = 1;  // 84 % × 84 % centred (bevelled look)
static constexpr int kPadCircle  = 2;  // min(w,h)*0.78 centred (circular look)

// ─────────────────────────────────────────────────────────────────────────────
// Step-sequencer row descriptor
//
// noteLow / noteHigh semantics:
//   noteHigh == -1  → exact note match:      ev.note == noteLow
//   noteHigh == -2  → pitch-class match:     (ev.note % 12) == noteLow
//   noteHigh >= 0   → range match:           noteLow <= ev.note <= noteHigh
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int kSeqCols    = 16;   // always 16 time steps
static constexpr int kSeqMaxRows = 12;   // enough for TR-808 (8) and TB-303 chromatic (12)

struct SeqRowDef {
    int         noteLow;   // trigger note, pitch class, or range start
    int         noteHigh;  // -1=exact, -2=pitchClass, >=0=rangeEnd
    const char *label;     // display label (2–3 chars recommended)
};

// ── TR-808 (8 instrument rows) ───────────────────────────────────────────────
static const SeqRowDef kSeqRows808[] = {
    {36, -1, "BD"},   // Bass Drum  (GM 36)
    {38, -1, "SD"},   // Snare      (GM 38)
    {39, -1, "CP"},   // Hand Clap  (GM 39)
    {42, -1, "CH"},   // Closed HH  (GM 42)
    {46, -1, "OH"},   // Open HH    (GM 46)
    {45, -1, "LT"},   // Low Tom    (GM 45)
    {47, -1, "MT"},   // Mid Tom    (GM 47)
    {50, -1, "HT"},   // High Tom   (GM 50)
};
static constexpr int kNumSeqRows808 = (int)(sizeof(kSeqRows808) / sizeof(kSeqRows808[0]));

// ── TR-909 (8 instrument rows) ───────────────────────────────────────────────
static const SeqRowDef kSeqRows909[] = {
    {36, -1, "BD"},   // Bass Drum  (GM 36)
    {38, -1, "SD"},   // Snare      (GM 38)
    {37, -1, "RM"},   // Rimshot    (GM 37)
    {39, -1, "CP"},   // Clap       (GM 39)
    {42, -1, "CH"},   // Closed HH  (GM 42)
    {46, -1, "OH"},   // Open HH    (GM 46)
    {41, -1, "LT"},   // Low Tom    (GM 41)
    {45, -1, "HT"},   // High Tom   (GM 45)
};
static constexpr int kNumSeqRows909 = (int)(sizeof(kSeqRows909) / sizeof(kSeqRows909[0]));

// ── TR-707 (8 instrument rows) ───────────────────────────────────────────────
static const SeqRowDef kSeqRows707[] = {
    {35, -1, "BD"},   // Bass Drum 2 (GM 35)
    {38, -1, "SD"},   // Snare       (GM 38)
    {37, -1, "RM"},   // Rimshot     (GM 37)
    {42, -1, "CH"},   // Closed HH   (GM 42)
    {46, -1, "OH"},   // Open HH     (GM 46)
    {41, -1, "LT"},   // Low Tom     (GM 41)
    {43, -1, "MT"},   // Mid Tom     (GM 43)
    {49, -1, "CY"},   // Crash Cym.  (GM 49)
};
static constexpr int kNumSeqRows707 = (int)(sizeof(kSeqRows707) / sizeof(kSeqRows707[0]));

// ── TB-303 Bassline (12 chromatic pitch classes) ─────────────────────────────
// noteHigh == -2 → match by pitch class (ev.note % 12 == noteLow).
// Every octave of the note fires the same row — great for acid bassline patterns.
static const SeqRowDef kSeqRowsTB303[] = {
    { 0, -2, "C"},
    { 1, -2, "C#"},
    { 2, -2, "D"},
    { 3, -2, "D#"},
    { 4, -2, "E"},
    { 5, -2, "F"},
    { 6, -2, "F#"},
    { 7, -2, "G"},
    { 8, -2, "G#"},
    { 9, -2, "A"},
    {10, -2, "A#"},
    {11, -2, "B"},
};
static constexpr int kNumSeqRowsTB303 = (int)(sizeof(kSeqRowsTB303) / sizeof(kSeqRowsTB303[0]));

// ─────────────────────────────────────────────────────────────────────────────
// Device preset descriptors
// ─────────────────────────────────────────────────────────────────────────────
struct DevicePreset {
    const char *id;
    const char *displayName;
    int         cols, rows, baseNote;
    uint32_t    colorPanel;   // outer panel / canvas background
    uint32_t    colorIdle;    // unlit pad / cell colour
    uint32_t    colorHit;     // active / hit colour
    int         padStyle;     // kPad*
    // Step-sequencer config (nullptr / 0 for grid presets)
    int              layoutMode;
    const SeqRowDef *seqRows;
    int              seqRowCount;
};

static const DevicePreset kPresets[] = {
    // ── Generic ──────────────────────────────────────────────────────────────
    {"generic_4x4", "Generic 4x4", 4, 4, 36,
     0xFF0D0D0D, 0xFF1A1A2E, 0xFFFF6600, kPadSquare,    kLayoutGrid, nullptr, 0},
    {"generic_8x8", "Generic 8x8", 8, 8, 36,
     0xFF0D0D0D, 0xFF1A1A2E, 0xFFFF6600, kPadSquare,    kLayoutGrid, nullptr, 0},

    // ── Roland (pad grid) ────────────────────────────────────────────────────
    {"tr808",  "Roland TR-808",  4, 4, 35, 0xFF1C1208, 0xFF2E1E08, 0xFFFF8800, kPadSquare,  kLayoutGrid, nullptr, 0},
    {"tr909",  "Roland TR-909",  4, 4, 36, 0xFF141414, 0xFF282828, 0xFFE8E8E8, kPadSquare,  kLayoutGrid, nullptr, 0},
    {"tr707",  "Roland TR-707",  4, 4, 36, 0xFF242424, 0xFF333333, 0xFFFF3300, kPadSquare,  kLayoutGrid, nullptr, 0},
    {"tr606",  "Roland TR-606",  2, 3, 36, 0xFF1A1208, 0xFF2A1E10, 0xFFFF6600, kPadSquare,  kLayoutGrid, nullptr, 0},
    {"sp404",  "Roland SP-404",  4, 4, 36, 0xFF1A0A00, 0xFF2A1400, 0xFFFF5500, kPadSquare,  kLayoutGrid, nullptr, 0},

    // ── Roland (authentic step-sequencer view) ───────────────────────────────
    // Instruments as rows, 16 time steps as columns.  Lights up as notes arrive.
    {"tr808_seq", "Roland TR-808 (Step Seq)", 0, 0, 35,
     0xFF1C1208, 0xFF2E1E08, 0xFFFF8800, kPadSquare,
     kLayoutStepSeq, kSeqRows808, kNumSeqRows808},
    {"tr909_seq", "Roland TR-909 (Step Seq)", 0, 0, 36,
     0xFF141414, 0xFF282828, 0xFFE8E8E8, kPadSquare,
     kLayoutStepSeq, kSeqRows909, kNumSeqRows909},
    {"tr707_seq", "Roland TR-707 (Step Seq)", 0, 0, 36,
     0xFF242424, 0xFF333333, 0xFFFF3300, kPadSquare,
     kLayoutStepSeq, kSeqRows707, kNumSeqRows707},

    // TB-303 Transistor Bass — chromatic pitch-class step view; each row is a
    // semitone (C through B), every octave of that pitch fires the same row.
    // Turns incoming acid basslines into a beautiful scrolling chromatic pattern.
    {"tb303", "Roland TB-303 (Bassline)", 0, 0, 48,
     0xFF061A0A, 0xFF0D2E14, 0xFF00FF66, kPadSquare,
     kLayoutStepSeq, kSeqRowsTB303, kNumSeqRowsTB303},

    // ── Akai MPC ─────────────────────────────────────────────────────────────
    {"mpc60",    "Akai MPC 60",    4, 4, 35, 0xFF1C1A18, 0xFF282422, 0xFFFFAA00, kPadRounded, kLayoutGrid, nullptr, 0},
    {"mpc3000",  "Akai MPC 3000",  4, 4, 35, 0xFF1A1818, 0xFF262222, 0xFFFFBB00, kPadRounded, kLayoutGrid, nullptr, 0},
    {"mpc_live", "Akai MPC Live",  4, 4, 36, 0xFF101010, 0xFF1A1A1A, 0xFFFF4400, kPadRounded, kLayoutGrid, nullptr, 0},

    // ── Native Instruments Maschine ──────────────────────────────────────────
    {"maschine_studio", "NI Maschine Studio", 4, 4, 36, 0xFF080808, 0xFF141414, 0xFFFF3300, kPadRounded, kLayoutGrid, nullptr, 0},
    {"maschine_mk3",    "NI Maschine Mk3",    4, 4, 36, 0xFF0A0A0A, 0xFF161616, 0xFFFF6600, kPadRounded, kLayoutGrid, nullptr, 0},
    {"maschine_mikro",  "NI Maschine Mikro",  4, 4, 36, 0xFF0D0D0D, 0xFF181818, 0xFFFF5500, kPadRounded, kLayoutGrid, nullptr, 0},

    // ── Hip-Hop Heritage ─────────────────────────────────────────────────────
    {"sp1200",   "E-mu SP-1200",  4, 2, 35, 0xFF1E1610, 0xFF2A2018, 0xFFFFCC00, kPadSquare,  kLayoutGrid, nullptr, 0},
    {"dmx",      "Oberheim DMX",  4, 2, 35, 0xFF1A1A22, 0xFF26263A, 0xFFFF0066, kPadSquare,  kLayoutGrid, nullptr, 0},
    {"linndrum", "LinnDrum",      4, 2, 35, 0xFF1E1E28, 0xFF2A2A38, 0xFFFF4488, kPadSquare,  kLayoutGrid, nullptr, 0},

    // ── Elektron ─────────────────────────────────────────────────────────────
    {"digitakt",    "Elektron Digitakt",    4, 4, 36, 0xFF111214, 0xFF1C1E22, 0xFFFFAA00, kPadSquare,  kLayoutGrid, nullptr, 0},
    {"analog_rytm", "Elektron Analog Rytm", 4, 3, 36, 0xFF0E1012, 0xFF181C20, 0xFFFF5500, kPadSquare,  kLayoutGrid, nullptr, 0},

    // ── Novation Launchpad ───────────────────────────────────────────────────
    {"launchpad",      "Novation Launchpad",      8, 8, 36, 0xFF080808, 0xFF101010, 0xFF00CC44, kPadSquare,  kLayoutGrid, nullptr, 0},
    {"launchpad_mini", "Novation Launchpad Mini", 8, 8, 36, 0xFF0A0A0A, 0xFF141414, 0xFF00AA33, kPadSquare,  kLayoutGrid, nullptr, 0},
    {"launchpad_x",    "Novation Launchpad X",    8, 8, 36, 0xFF060606, 0xFF0E0E0E, 0xFF6633FF, kPadRounded, kLayoutGrid, nullptr, 0},
    {"launchpad_pro",  "Novation Launchpad Pro",  8, 8, 36, 0xFF060606, 0xFF0E0E0E, 0xFF00FFCC, kPadRounded, kLayoutGrid, nullptr, 0},

    // ── Arturia ──────────────────────────────────────────────────────────────
    {"beatstep_pro", "Arturia BeatStep Pro", 8, 2, 36, 0xFF1A1214, 0xFF2A2030, 0xFFFF2255, kPadCircle,  kLayoutGrid, nullptr, 0},
    {"drumbrute",    "Arturia DrumBrute",    4, 4, 36, 0xFF181C20, 0xFF242830, 0xFFFF6622, kPadCircle,  kLayoutGrid, nullptr, 0},

    // ── Korg ─────────────────────────────────────────────────────────────────
    {"volca_beats",  "Korg Volca Beats",  4, 4, 36, 0xFF1A1A10, 0xFF282814, 0xFFFFCC00, kPadCircle,  kLayoutGrid, nullptr, 0},
};

static constexpr int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

static const DevicePreset *findPreset(const char *id)
{
    for (int i = 0; i < kNumPresets; ++i)
        if (strcmp(kPresets[i].id, id) == 0)
            return &kPresets[i];
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
struct DrumSource {
    static constexpr int MAX_PADS = 64;

    // ── Grid layout state ────────────────────────────────────────────────────
    int     padNote[MAX_PADS];    // MIDI note; -1 = unused
    float   padFlash[MAX_PADS];   // 0.0–1.0 brightness, decays each tick

    // ── Step-seq layout state ────────────────────────────────────────────────
    SeqRowDef seqRowDefs[kSeqMaxRows];            // resolved from preset on load
    float     seqFlash[kSeqMaxRows][kSeqCols];    // per-cell brightness
    int       seqRowCount = 0;
    int       seqCurCol   = 0;      // most-recently advanced column (0–15)
    float     seqColAge   = 999.0f; // seconds since last column advance (debounce)

    // ── Settings ─────────────────────────────────────────────────────────────
    char     presetId[32] = "generic_4x4";
    int      layoutMode   = kLayoutGrid;
    int      columns      = 4;
    int      rows         = 4;
    int      baseNote     = 36;
    bool     showLabels   = true;
    int      padStyleInt  = kPadSquare;
    uint32_t colorPanel   = 0xFF0D0D0D;
    uint32_t colorHit     = 0xFFFF6600;
    uint32_t colorIdle    = 0xFF1A1A2E;
    uint32_t midiHandle   = 0;
    int      midiPort     = -1;
    uint32_t cx = 400;
    uint32_t cy = 400;

    DrumSource()
    {
        for (int i = 0; i < MAX_PADS; ++i) {
            padNote[i]  = (i < 16) ? 36 + i : -1;
            padFlash[i] = 0.0f;
        }
        memset(seqRowDefs, 0, sizeof(seqRowDefs));
        memset(seqFlash,   0, sizeof(seqFlash));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
static void apply_midi_port(int portIdx)
{
    if (portIdx >= 0)
        MidiEngine::instance().openPort(portIdx);
}

static void rebuild_pad_notes(DrumSource *s)
{
    int totalPads = s->rows * s->columns;
    for (int i = 0; i < DrumSource::MAX_PADS; ++i)
        s->padNote[i] = (i < totalPads) ? (s->baseNote + i) : -1;
}

static void load_settings(DrumSource *s, obs_data_t *settings)
{
    strncpy(s->presetId, obs_data_get_string(settings, "preset_id"),
            sizeof(s->presetId) - 1);
    s->presetId[sizeof(s->presetId) - 1] = '\0';

    s->columns     = (int)obs_data_get_int(settings,      "columns");
    s->rows        = (int)obs_data_get_int(settings,      "rows");
    s->baseNote    = (int)obs_data_get_int(settings,      "base_note");
    s->showLabels  = obs_data_get_bool(settings,          "show_labels");
    s->padStyleInt = (int)obs_data_get_int(settings,      "pad_style");
    s->colorPanel  = (uint32_t)obs_data_get_int(settings, "color_panel");
    s->colorHit    = (uint32_t)obs_data_get_int(settings, "color_hit");
    s->colorIdle   = (uint32_t)obs_data_get_int(settings, "color_idle");
    s->midiPort    = (int)obs_data_get_int(settings,      "midi_port");
    s->layoutMode  = (int)obs_data_get_int(settings,      "layout_mode");

    // Resolve step-seq row definitions from the active preset
    const DevicePreset *p = findPreset(s->presetId);
    if (p && p->seqRows && p->seqRowCount > 0) {
        s->seqRowCount = std::min(p->seqRowCount, kSeqMaxRows);
        for (int i = 0; i < s->seqRowCount; ++i)
            s->seqRowDefs[i] = p->seqRows[i];
    } else {
        s->seqRowCount = 0;
    }
}

static const char *drum_get_name(void *) { return obs_module_text("DrumSource.Name"); }

static void *drum_create(obs_data_t *settings, obs_source_t *source)
{
    (void)source;
    auto *s = new DrumSource{};
    load_settings(s, settings);
    rebuild_pad_notes(s);
    apply_midi_port(s->midiPort);

    s->midiHandle = MidiEngine::instance().subscribe([s](const MidiEvent &ev) {
        if (ev.portIndex != s->midiPort && s->midiPort >= 0) return;
        if (ev.type != MidiEventType::NoteOn) return;

        if (s->layoutMode == kLayoutStepSeq) {
            // Advance to the next step column with 80 ms debounce so
            // near-simultaneous notes on the same beat don't skip two columns.
            if (s->seqColAge > 0.08f) {
                s->seqCurCol = (s->seqCurCol + 1) % kSeqCols;
                // Clear the incoming column so old flash doesn't bleed through
                for (int r = 0; r < kSeqMaxRows; ++r)
                    s->seqFlash[r][s->seqCurCol] = 0.0f;
                s->seqColAge = 0.0f;
            }
            // Light up every row whose note definition matches this event
            const float vel = ev.param2 / 127.0f;
            for (int r = 0; r < s->seqRowCount; ++r) {
                const SeqRowDef &rd = s->seqRowDefs[r];
                bool match;
                if (rd.noteHigh == -1) {
                    match = ((int)ev.param1 == rd.noteLow);
                } else if (rd.noteHigh == -2) {
                    match = ((int)ev.param1 % 12 == rd.noteLow);
                } else {
                    match = ((int)ev.param1 >= rd.noteLow &&
                             (int)ev.param1 <= rd.noteHigh);
                }
                if (match)
                    s->seqFlash[r][s->seqCurCol] = vel;
            }
        } else {
            // Grid mode — find the matching pad by note number
            bool matched = false;
            for (int i = 0; i < DrumSource::MAX_PADS; ++i) {
                if (s->padNote[i] == (int)ev.param1) {
                    s->padFlash[i] = ev.param2 / 127.0f;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                MIDI_LOG_INFO(
                    "Drum: received unmapped NoteOn %d (vel=%d ch=%d port=%d). "
                    "Adjust 'Base Note' in source properties if this is your pad controller.",
                    (int)ev.param1, (int)ev.param2, (int)ev.channel, ev.portIndex);
            }
        }
    });

    MIDI_LOG_INFO("Drum source created (preset=%s, layout=%s)",
                  s->presetId, s->layoutMode == kLayoutStepSeq ? "stepseq" : "grid");
    return s;
}

static void drum_destroy(void *data)
{
    auto *s = static_cast<DrumSource *>(data);
    MidiEngine::instance().unsubscribe(s->midiHandle);
    delete s;
}

static void drum_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, "preset_id",    "generic_4x4");
    obs_data_set_default_int(   settings, "midi_port",    -1);
    obs_data_set_default_int(   settings, "columns",       4);
    obs_data_set_default_int(   settings, "rows",          4);
    obs_data_set_default_int(   settings, "base_note",    36);
    obs_data_set_default_bool(  settings, "show_labels",  true);
    obs_data_set_default_int(   settings, "pad_style",    kPadSquare);
    obs_data_set_default_int(   settings, "color_panel",  (int64_t)0xFF0D0D0Du);
    obs_data_set_default_int(   settings, "color_hit",    (int64_t)0xFFFF6600u);
    obs_data_set_default_int(   settings, "color_idle",   (int64_t)0xFF1A1A2Eu);
    obs_data_set_default_int(   settings, "layout_mode",  kLayoutGrid);
}

// ─────────────────────────────────────────────────────────────────────────────
// Modified callback: when the preset dropdown changes, push all preset values
// into settings so other property widgets update to reflect the new preset.
// ─────────────────────────────────────────────────────────────────────────────
static bool preset_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
    (void)props;
    const char *id = obs_data_get_string(settings, "preset_id");
    const DevicePreset *p = findPreset(id);
    if (!p) return false;

    obs_data_set_int(settings, "columns",     p->cols);
    obs_data_set_int(settings, "rows",        p->rows);
    obs_data_set_int(settings, "base_note",   p->baseNote);
    obs_data_set_int(settings, "pad_style",   p->padStyle);
    obs_data_set_int(settings, "color_panel", (int64_t)p->colorPanel);
    obs_data_set_int(settings, "color_idle",  (int64_t)p->colorIdle);
    obs_data_set_int(settings, "color_hit",   (int64_t)p->colorHit);
    obs_data_set_int(settings, "layout_mode", (int64_t)p->layoutMode);
    return true;
}

static obs_properties_t *drum_properties(void *)
{
    obs_properties_t *props = obs_properties_create();

    // ── Device preset ────────────────────────────────────────────────────────
    obs_property_t *preset_list = obs_properties_add_list(props, "preset_id",
        "Device Preset", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    for (int i = 0; i < kNumPresets; ++i)
        obs_property_list_add_string(preset_list, kPresets[i].displayName, kPresets[i].id);
    obs_property_set_modified_callback(preset_list, preset_changed);

    // ── MIDI port ────────────────────────────────────────────────────────────
    obs_property_t *port_list = obs_properties_add_list(props, "midi_port",
        "MIDI Input Device", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(port_list, "Disabled", -1);
    auto ports = MidiEngine::instance().portNames();
    for (int i = 0; i < (int)ports.size(); ++i)
        obs_property_list_add_int(port_list, ports[i].c_str(), i);

    obs_properties_add_button(props, "midi_refresh", "Refresh Devices",
        [](obs_properties_t *pp, obs_property_t *, void *) -> bool {
            obs_property_t *lst = obs_properties_get(pp, "midi_port");
            obs_property_list_clear(lst);
            obs_property_list_add_int(lst, "Disabled", -1);
            auto ps = MidiEngine::instance().portNames();
            for (int i = 0; i < (int)ps.size(); ++i)
                obs_property_list_add_int(lst, ps[i].c_str(), i);
            return true;
        });

    // ── Grid properties (used in grid layout; ignored by step-seq layout) ────
    obs_properties_add_int( props, "columns",    "Grid columns",          1, 8, 1);
    obs_properties_add_int( props, "rows",       "Grid rows",             1, 8, 1);
    obs_properties_add_int( props, "base_note",  "Base Note (first pad)", 0, 120, 1);

    // ── Visuals ──────────────────────────────────────────────────────────────
    obs_property_t *style_list = obs_properties_add_list(props, "pad_style",
        "Pad Style", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(style_list, "Square",  kPadSquare);
    obs_property_list_add_int(style_list, "Rounded", kPadRounded);
    obs_property_list_add_int(style_list, "Circle",  kPadCircle);

    obs_properties_add_bool( props, "show_labels",  "Show pad labels");
    obs_properties_add_color(props, "color_panel",  "Panel background");
    obs_properties_add_color(props, "color_idle",   "Pad colour (idle)");
    obs_properties_add_color(props, "color_hit",    "Pad colour (hit)");
    return props;
}

static void drum_update(void *data, obs_data_t *settings)
{
    auto *s = static_cast<DrumSource *>(data);
    load_settings(s, settings);
    rebuild_pad_notes(s);
    apply_midi_port(s->midiPort);
}

static void drum_tick(void *data, float seconds)
{
    auto *s = static_cast<DrumSource *>(data);
    MidiEngine::instance().drainQueue();

    s->seqColAge += seconds;  // always advance; used for step-seq debounce

    if (s->layoutMode == kLayoutStepSeq) {
        // Slower decay so notes stay readable across 16 steps at human tempos
        constexpr float kSeqDecay = 2.5f;
        for (int r = 0; r < kSeqMaxRows; ++r)
            for (int c = 0; c < kSeqCols; ++c)
                if (s->seqFlash[r][c] > 0.0f)
                    s->seqFlash[r][c] = std::max(0.0f, s->seqFlash[r][c] - kSeqDecay * seconds);
    } else {
        constexpr float kDecayRate = 5.0f;
        for (int i = 0; i < DrumSource::MAX_PADS; ++i)
            if (s->padFlash[i] > 0.0f)
                s->padFlash[i] = std::max(0.0f, s->padFlash[i] - kDecayRate * seconds);
    }
}

static uint32_t drum_get_width (void *data) { return static_cast<DrumSource*>(data)->cx; }
static uint32_t drum_get_height(void *data) { return static_cast<DrumSource*>(data)->cy; }

// ─────────────────────────────────────────────────────────────────────────────
// Render
//
// Two layout modes share the same gs_technique_begin/end wrapper:
//
//  kLayoutGrid    — existing pad-grid renderer (pad shapes + bitmap labels)
//  kLayoutStepSeq — step-sequencer row view:
//                     left strip  = 15 % of canvas width, instrument labels
//                     right area  = 16 equally-spaced step columns
//                     separators  = thin lines every 4 steps (1-bar groups)
//                     highlight   = faint band behind the current/newest column
//
// Uses gs_technique_begin/end directly — gs_effect_loop cannot be nested
// inside OBS's compositing pass.
// ─────────────────────────────────────────────────────────────────────────────
static void drum_render(void *data, gs_effect_t *effect)
{
    (void)effect;
    auto *s = static_cast<DrumSource *>(data);

    const float W = (float)s->cx;
    const float H = (float)s->cy;

    gs_effect_t    *solid      = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t    *colorParam = gs_effect_get_param_by_name(solid, "color");
    gs_technique_t *tech       = gs_effect_get_technique(solid, "Solid");
    size_t          passes     = gs_technique_begin(tech);

    auto drawRect = [&](float x, float y, float w, float h, uint32_t argb) {
        if (w < 1.0f || h < 1.0f) return;
        struct vec4 color;
        vec4_set(&color,
            ((argb >> 16) & 0xFF) / 255.0f,
            ((argb >>  8) & 0xFF) / 255.0f,
            ( argb        & 0xFF) / 255.0f,
            ((argb >> 24) & 0xFF) / 255.0f);
        gs_effect_set_vec4(colorParam, &color);
        gs_matrix_push();
        gs_matrix_translate3f(x, y, 0.0f);
        gs_draw_sprite(nullptr, 0, (uint32_t)w, (uint32_t)h);
        gs_matrix_pop();
    };

    for (size_t p = 0; p < passes; ++p) {
        gs_technique_begin_pass(tech, p);

        if (s->layoutMode == kLayoutStepSeq) {
            // ── Step-sequencer layout ────────────────────────────────────────
            const int numRows = std::max(1, std::min(s->seqRowCount, kSeqMaxRows));

            const float rowH    = H / (float)numRows;
            const float labelW  = W * 0.15f;

            // Font scale: fit 3 chars (11 px wide at scale 1) in the label strip
            // and 5 px tall in 60 % of the row height.
            float fsW    = (labelW - 6.0f) / 11.0f;
            float fsH    = rowH * 0.60f / 5.0f;
            float fscale = std::max(1.0f, std::floor(std::min(fsW, fsH)));

            const float gridW = W - labelW;
            const float colW  = gridW / (float)kSeqCols;
            const float gap   = std::max(1.0f, std::min(colW, rowH) * 0.05f);

            // Panel background
            drawRect(0.0f, 0.0f, W, H, s->colorPanel);

            // Current-column highlight band (draw before cells)
            {
                float hx  = labelW + (float)s->seqCurCol * colW;
                uint32_t hcol = lerp_argb(s->colorPanel, s->colorIdle, 0.45f);
                drawRect(hx, 0.0f, colW, H, hcol);
            }

            // Group separator lines every 4 steps (steps 4, 8, 12)
            uint32_t sepColor = lerp_argb(s->colorPanel, 0xFF888888u, 0.35f);
            for (int g = 4; g < kSeqCols; g += 4) {
                float sx = labelW + (float)g * colW - 1.0f;
                drawRect(sx, 0.0f, 2.0f, H, sepColor);
            }

            // Label-strip right-edge separator
            drawRect(labelW - 1.0f, 0.0f, 2.0f, H,
                     lerp_argb(s->colorPanel, 0xFF666666u, 0.40f));

            // Per-row labels + step cells
            for (int r = 0; r < numRows; ++r) {
                const float rowY = (float)r * rowH;

                // ── Label ────────────────────────────────────────────────────
                const char *lbl = (s->seqRowDefs[r].label) ? s->seqRowDefs[r].label : "--";
                int lblLen = (int)strlen(lbl);
                // Trim trailing spaces
                while (lblLen > 0 && lbl[lblLen - 1] == ' ') --lblLen;
                if (lblLen == 0) { lbl = "--"; lblLen = 2; }

                float txtW = ((float)lblLen * 4.0f - 1.0f) * fscale;
                float txtH = 5.0f * fscale;
                float tx   = (labelW - txtW) * 0.5f;
                float ty   = rowY + (rowH - txtH) * 0.5f;

                constexpr uint32_t lblColor = 0xFFAAAAAA;
                for (int ci = 0; ci < lblLen; ++ci) {
                    int   fi    = fontIdx(lbl[ci]);
                    float charX = tx + (float)ci * 4.0f * fscale;
                    for (int row5 = 0; row5 < 5; ++row5) {
                        uint8_t bits = kFont3x5[fi][row5];
                        for (int bit = 0; bit < 3; ++bit) {
                            if (bits & (1u << (2 - bit))) {
                                drawRect(charX + (float)bit * fscale,
                                         ty    + (float)row5 * fscale,
                                         fscale, fscale, lblColor);
                            }
                        }
                    }
                }

                // ── Step cells ───────────────────────────────────────────────
                for (int c = 0; c < kSeqCols; ++c) {
                    float cellX = labelW + (float)c * colW + gap;
                    float cellY = rowY + gap;
                    float cellW = colW - gap * 2.0f;
                    float cellH = rowH - gap * 2.0f;
                    if (cellW < 1.0f || cellH < 1.0f) continue;

                    float    flash     = s->seqFlash[r][c];
                    uint32_t cellColor = lerp_argb(s->colorIdle, s->colorHit, flash);
                    drawRect(cellX, cellY, cellW, cellH, cellColor);
                }
            }

        } else {
            // ── Pad-grid layout ──────────────────────────────────────────────
            const int cols = std::max(1, s->columns);
            const int rows = std::max(1, s->rows);

            const float minDim = std::min(W / cols, H / rows);
            const float gap    = std::max(2.0f, minDim * 0.05f);
            const float padW   = (W - gap * (cols + 1)) / cols;
            const float padH   = (H - gap * (rows + 1)) / rows;
            if (padW < 2.0f || padH < 2.0f) {
                gs_technique_end_pass(tech);
                break;
            }

            // Panel background
            drawRect(0.0f, 0.0f, W, H, s->colorPanel);

            const int total = cols * rows;
            for (int i = 0; i < total && i < DrumSource::MAX_PADS; ++i) {
                const int   col   = i % cols;
                const int   row   = i / cols;
                const float cellX = gap + col * (padW + gap);
                const float cellY = gap + row * (padH + gap);

                // Pad shape
                float drawW, drawH, offX, offY;
                if (s->padStyleInt == kPadRounded) {
                    drawW = padW * 0.84f;
                    drawH = padH * 0.84f;
                    offX  = (padW - drawW) * 0.5f;
                    offY  = (padH - drawH) * 0.5f;
                } else if (s->padStyleInt == kPadCircle) {
                    float d = std::min(padW, padH) * 0.78f;
                    drawW = drawH = d;
                    offX  = (padW - drawW) * 0.5f;
                    offY  = (padH - drawH) * 0.5f;
                } else {
                    drawW = padW; drawH = padH; offX = offY = 0.0f;
                }

                const float padX = cellX + offX;
                const float padY = cellY + offY;

                const uint32_t padColor = lerp_argb(s->colorIdle, s->colorHit, s->padFlash[i]);
                drawRect(padX, padY, drawW, drawH, padColor);

                if (!s->showLabels || s->padNote[i] < 0) continue;

                // Bitmap label
                char label[16] = {};
                int  note = s->padNote[i];
                if (note < 128 && GM_DRUM_NAMES[note])
                    snprintf(label, sizeof(label), "%s", GM_DRUM_NAMES[note]);
                else
                    snprintf(label, sizeof(label), "%d", note);
                int len = (int)strlen(label);
                if (len == 0) continue;

                float maxTxtW = drawW * 0.80f;
                float maxTxtH = drawH * 0.35f;
                float scaleW  = maxTxtW / (len * 4.0f);
                float scaleH  = maxTxtH / 5.0f;
                float scale   = std::floor(std::min(scaleW, scaleH));
                if (scale < 1.0f) scale = 1.0f;

                float textW = (float)len * 4.0f * scale - scale;
                float textH = 5.0f * scale;
                float tx    = padX + (drawW - textW) * 0.5f;
                float ty    = padY + drawH * 0.62f;
                if (ty + textH > padY + drawH) ty = padY + drawH - textH - 1.0f;

                float lr  = ((padColor >> 16) & 0xFF) / 255.0f;
                float lg  = ((padColor >>  8) & 0xFF) / 255.0f;
                float lb  = ( padColor        & 0xFF) / 255.0f;
                float lum = 0.299f * lr + 0.587f * lg + 0.114f * lb;
                uint32_t textColor = (lum > 0.45f) ? 0xFF000000u : 0xFFFFFFFFu;

                for (int ci = 0; ci < len; ++ci) {
                    int   fi    = fontIdx(label[ci]);
                    float charX = tx + ci * 4.0f * scale;
                    for (int row5 = 0; row5 < 5; ++row5) {
                        uint8_t bits = kFont3x5[fi][row5];
                        for (int bit = 0; bit < 3; ++bit) {
                            if (bits & (1u << (2 - bit))) {
                                drawRect(charX + bit * scale,
                                         ty    + row5 * scale,
                                         scale, scale, textColor);
                            }
                        }
                    }
                }
            }
        }

        gs_technique_end_pass(tech);
    }

    gs_technique_end(tech);
}

void drum_source_register(void)
{
    static obs_source_info info{};
    info.id             = "midi_drum_source";
    info.type           = OBS_SOURCE_TYPE_INPUT;
    info.output_flags   = OBS_SOURCE_VIDEO;
    info.get_name       = drum_get_name;
    info.create         = drum_create;
    info.destroy        = drum_destroy;
    info.get_defaults   = drum_defaults;
    info.get_properties = drum_properties;
    info.update         = drum_update;
    info.video_tick     = drum_tick;
    info.video_render   = drum_render;
    info.get_width      = drum_get_width;
    info.get_height     = drum_get_height;
    obs_register_source(&info);
    MIDI_LOG_INFO("Registered source: midi_drum_source");
}
