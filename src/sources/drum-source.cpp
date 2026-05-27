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
// Pad visual styles
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int kPadSquare  = 0;  // fills the full cell rectangle
static constexpr int kPadRounded = 1;  // 84 % × 84 % centred (bevelled look)
static constexpr int kPadCircle  = 2;  // min(w,h)*0.78 centred (circular look)

// ─────────────────────────────────────────────────────────────────────────────
// Device preset descriptors
// Each preset is a fully self-contained set of defaults that can be applied
// in one click from the Properties panel.  Users can still override any field
// after applying a preset.
// ─────────────────────────────────────────────────────────────────────────────
struct DevicePreset {
    const char *id;
    const char *displayName;
    int         cols, rows, baseNote;
    uint32_t    colorPanel;   // outer panel / canvas background
    uint32_t    colorIdle;    // unlit pad colour
    uint32_t    colorHit;     // active / hit pad colour
    int         padStyle;     // kPad*
};

static const DevicePreset kPresets[] = {
    // ── Generic ──────────────────────────────────────────────────────────────
    {"generic_4x4",     "Generic 4×4",              4, 4,  36,
     0xFF0D0D0D, 0xFF1A1A2E, 0xFFFF6600, kPadSquare},
    {"generic_8x8",     "Generic 8×8",              8, 8,  36,
     0xFF0D0D0D, 0xFF1A1A2E, 0xFFFF6600, kPadSquare},

    // ── Roland ───────────────────────────────────────────────────────────────
    // TR-808 — warm orange hits on near-black wood-panel background;
    //          the gold standard of hip-hop and trap kick/snare/hi-hat.
    {"tr808",           "Roland TR-808",             4, 4,  35,
     0xFF1C1208, 0xFF2E1E08, 0xFFFF8800, kPadSquare},
    // TR-909 — clean white hits on charcoal; the house and techno archetype.
    {"tr909",           "Roland TR-909",             4, 4,  36,
     0xFF141414, 0xFF282828, 0xFFE8E8E8, kPadSquare},
    // TR-707 — bright red-orange on dark grey; pop and early electronic.
    {"tr707",           "Roland TR-707",             4, 4,  36,
     0xFF242424, 0xFF333333, 0xFFFF3300, kPadSquare},
    // TR-606 — 6-instrument companion to the TB-303 (2-col × 3-row).
    {"tr606",           "Roland TR-606",             2, 3,  36,
     0xFF1A1208, 0xFF2A1E10, 0xFFFF6600, kPadSquare},
    // SP-404 — portable sampler; staple of lo-fi and beat-tape production.
    {"sp404",           "Roland SP-404",             4, 4,  36,
     0xFF1A0A00, 0xFF2A1400, 0xFFFF5500, kPadSquare},

    // ── Akai MPC ─────────────────────────────────────────────────────────────
    // MPC 60 — Roger Linn / Akai, the first MPC; defined golden-era hip-hop.
    {"mpc60",           "Akai MPC 60",               4, 4,  35,
     0xFF1C1A18, 0xFF282422, 0xFFFFAA00, kPadRounded},
    // MPC 3000 — the Dre / Premier / Dilla workhorse.
    {"mpc3000",         "Akai MPC 3000",             4, 4,  35,
     0xFF1A1818, 0xFF262222, 0xFFFFBB00, kPadRounded},
    // MPC Live — modern standalone MPC with vivid RGB pads.
    {"mpc_live",        "Akai MPC Live",             4, 4,  36,
     0xFF101010, 0xFF1A1A1A, 0xFFFF4400, kPadRounded},

    // ── Native Instruments Maschine ──────────────────────────────────────────
    {"maschine_studio", "NI Maschine Studio",        4, 4,  36,
     0xFF080808, 0xFF141414, 0xFFFF3300, kPadRounded},
    {"maschine_mk3",    "NI Maschine Mk3",           4, 4,  36,
     0xFF0A0A0A, 0xFF161616, 0xFFFF6600, kPadRounded},
    {"maschine_mikro",  "NI Maschine Mikro",         4, 4,  36,
     0xFF0D0D0D, 0xFF181818, 0xFFFF5500, kPadRounded},

    // ── Hip-Hop Heritage ─────────────────────────────────────────────────────
    // E-mu SP-1200 — punchy, filtered sound; golden-age NY hip-hop (4×2 bank).
    {"sp1200",          "E-mu SP-1200",              4, 2,  35,
     0xFF1E1610, 0xFF2A2018, 0xFFFFCC00, kPadSquare},
    // Oberheim DMX — Run-DMC, LL Cool J; hard-clap and deep kick (4×2).
    {"dmx",             "Oberheim DMX",              4, 2,  35,
     0xFF1A1A22, 0xFF26263A, 0xFFFF0066, kPadSquare},
    // LinnDrum — Prince, Human League, Hall & Oates; pop-electronic (4×2).
    {"linndrum",        "LinnDrum",                  4, 2,  35,
     0xFF1E1E28, 0xFF2A2A38, 0xFFFF4488, kPadSquare},

    // ── Elektron ─────────────────────────────────────────────────────────────
    // Digitakt — modern digital sampler/sequencer, amber accent, 4×4.
    {"digitakt",        "Elektron Digitakt",         4, 4,  36,
     0xFF111214, 0xFF1C1E22, 0xFFFFAA00, kPadSquare},
    // Analog Rytm — analog/acoustic sample hybrid, 12 voices (4×3).
    {"analog_rytm",     "Elektron Analog Rytm",      4, 3,  36,
     0xFF0E1012, 0xFF181C20, 0xFFFF5500, kPadSquare},

    // ── Novation Launchpad ───────────────────────────────────────────────────
    // Original Launchpad — the live-performance clip launcher that defined
    // festival EDM button-pushing; 8×8 green pads on near-black.
    {"launchpad",       "Novation Launchpad",        8, 8,  36,
     0xFF080808, 0xFF101010, 0xFF00CC44, kPadSquare},
    {"launchpad_mini",  "Novation Launchpad Mini",   8, 8,  36,
     0xFF0A0A0A, 0xFF141414, 0xFF00AA33, kPadSquare},
    // Launchpad X — full RGB, velocity-sensitive; rounded pad aesthetic.
    {"launchpad_x",     "Novation Launchpad X",      8, 8,  36,
     0xFF060606, 0xFF0E0E0E, 0xFF6633FF, kPadRounded},
    // Launchpad Pro — flagship; aftertouch, extra top/side rows (8×8 main grid).
    {"launchpad_pro",   "Novation Launchpad Pro",    8, 8,  36,
     0xFF060606, 0xFF0E0E0E, 0xFF00FFCC, kPadRounded},

    // ── Arturia ──────────────────────────────────────────────────────────────
    // BeatStep Pro — 16-step sequencer rows, circular pads (8×2 display).
    {"beatstep_pro",    "Arturia BeatStep Pro",      8, 2,  36,
     0xFF1A1214, 0xFF2A2030, 0xFFFF2255, kPadCircle},
    // DrumBrute — analog drum synthesizer, circular pads, warm-red flash.
    {"drumbrute",       "Arturia DrumBrute",         4, 4,  36,
     0xFF181C20, 0xFF242830, 0xFFFF6622, kPadCircle},

    // ── Korg ─────────────────────────────────────────────────────────────────
    // Volca Beats — lo-fi analog; yellow accent on dark olive.
    {"volca_beats",     "Korg Volca Beats",          4, 4,  36,
     0xFF1A1A10, 0xFF282814, 0xFFFFCC00, kPadCircle},
};

static constexpr int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

// Finds a preset by id; returns nullptr if not found.
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

    int     padNote[MAX_PADS];   // MIDI note number; -1 = unused
    float   padFlash[MAX_PADS];  // 0.0–1.0 brightness (decays each tick)

    char     presetId[32] = "generic_4x4";
    int      columns      = 4;
    int      rows         = 4;
    int      baseNote     = 36;
    bool     showLabels   = true;
    int      padStyleInt  = kPadSquare;

    uint32_t colorPanel = 0xFF0D0D0D;   // canvas / panel background
    uint32_t colorHit   = 0xFFFF6600;   // active pad
    uint32_t colorIdle  = 0xFF1A1A2E;   // unlit pad

    uint32_t midiHandle = 0;
    int      midiPort   = -1;
    uint32_t cx = 400;
    uint32_t cy = 400;

    DrumSource()
    {
        for (int i = 0; i < MAX_PADS; ++i) {
            padNote[i]  = (i < 16) ? 36 + i : -1;
            padFlash[i] = 0.0f;
        }
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

// Reads all settings into an existing DrumSource (used by create + update).
static void load_settings(DrumSource *s, obs_data_t *settings)
{
    strncpy(s->presetId, obs_data_get_string(settings, "preset_id"),
            sizeof(s->presetId) - 1);
    s->presetId[sizeof(s->presetId) - 1] = '\0';

    s->columns     = (int)obs_data_get_int(settings,    "columns");
    s->rows        = (int)obs_data_get_int(settings,    "rows");
    s->baseNote    = (int)obs_data_get_int(settings,    "base_note");
    s->showLabels  = obs_data_get_bool(settings,        "show_labels");
    s->padStyleInt = (int)obs_data_get_int(settings,    "pad_style");
    s->colorPanel  = (uint32_t)obs_data_get_int(settings, "color_panel");
    s->colorHit    = (uint32_t)obs_data_get_int(settings, "color_hit");
    s->colorIdle   = (uint32_t)obs_data_get_int(settings, "color_idle");
    s->midiPort    = (int)obs_data_get_int(settings,    "midi_port");
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
    });

    MIDI_LOG_INFO("Drum source created (%dx%d, baseNote=%d, preset=%s)",
                  s->columns, s->rows, s->baseNote, s->presetId);
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
}

// ─────────────────────────────────────────────────────────────────────────────
// Modified callback: when the preset dropdown changes, push all preset values
// into settings so the other property widgets reflect the new preset.
// ─────────────────────────────────────────────────────────────────────────────
static bool preset_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
    (void)props;
    const char *id = obs_data_get_string(settings, "preset_id");
    const DevicePreset *p = findPreset(id);
    if (!p) return false;

    obs_data_set_int(settings,  "columns",     p->cols);
    obs_data_set_int(settings,  "rows",        p->rows);
    obs_data_set_int(settings,  "base_note",   p->baseNote);
    obs_data_set_int(settings,  "pad_style",   p->padStyle);
    obs_data_set_int(settings,  "color_panel", (int64_t)p->colorPanel);
    obs_data_set_int(settings,  "color_idle",  (int64_t)p->colorIdle);
    obs_data_set_int(settings,  "color_hit",   (int64_t)p->colorHit);
    return true;  // signal OBS to refresh the properties panel
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

    // ── Grid ─────────────────────────────────────────────────────────────────
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
    constexpr float kDecayRate = 5.0f;
    for (int i = 0; i < DrumSource::MAX_PADS; ++i)
        if (s->padFlash[i] > 0.0f)
            s->padFlash[i] = std::max(0.0f, s->padFlash[i] - kDecayRate * seconds);
}

static uint32_t drum_get_width (void *data) { return static_cast<DrumSource*>(data)->cx; }
static uint32_t drum_get_height(void *data) { return static_cast<DrumSource*>(data)->cy; }

// ─────────────────────────────────────────────────────────────────────────────
// Render — pad grid with per-pad colour, pad-style shaping, and bitmap labels.
//
// Layout:
//   gap  = 5% of the smaller pad dimension (min 2 px)
//   padW = (cx - gap*(cols+1)) / cols        ← grid cell size
//   padH = (cy - gap*(rows+1)) / rows
//
// Pad shape (inside the grid cell) is governed by padStyle:
//   Square  : full padW × padH
//   Rounded : 84 % × 84 %, centred in the cell
//   Circle  : min(padW,padH)*0.78 centred (looks round when cells are square)
//
// Label text (when showLabels) occupies the bottom 35 % of the drawn pad,
// auto-scaled to 80 % width using the 3×5 bitmap font.
//
// Uses gs_technique_begin/end directly — gs_effect_loop cannot be nested
// inside OBS's compositing pass.
// ─────────────────────────────────────────────────────────────────────────────
static void drum_render(void *data, gs_effect_t *effect)
{
    (void)effect;
    auto *s = static_cast<DrumSource *>(data);

    const float W    = (float)s->cx;
    const float H    = (float)s->cy;
    const int   cols = std::max(1, s->columns);
    const int   rows = std::max(1, s->rows);

    const float minDim = std::min(W / cols, H / rows);
    const float gap    = std::max(2.0f, minDim * 0.05f);
    const float padW   = (W - gap * (cols + 1)) / cols;
    const float padH   = (H - gap * (rows + 1)) / rows;
    if (padW < 2.0f || padH < 2.0f) return;

    gs_effect_t    *solid      = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t    *colorParam = gs_effect_get_param_by_name(solid, "color");
    gs_technique_t *tech       = gs_effect_get_technique(solid, "Solid");
    size_t          passes     = gs_technique_begin(tech);

    // ── drawRect helper ──────────────────────────────────────────────────────
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

        // ── Panel background (full canvas) ───────────────────────────────────
        drawRect(0.0f, 0.0f, W, H, s->colorPanel);

        const int total = cols * rows;
        for (int i = 0; i < total && i < DrumSource::MAX_PADS; ++i) {
            const int   col = i % cols;
            const int   row = i / cols;
            const float cellX = gap + col * (padW + gap);
            const float cellY = gap + row * (padH + gap);

            // ── Pad shape ────────────────────────────────────────────────────
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
            } else { // kPadSquare
                drawW = padW; drawH = padH;
                offX  = offY = 0.0f;
            }

            const float padX = cellX + offX;
            const float padY = cellY + offY;

            // ── Pad background ───────────────────────────────────────────────
            const uint32_t padColor = lerp_argb(s->colorIdle, s->colorHit, s->padFlash[i]);
            drawRect(padX, padY, drawW, drawH, padColor);

            // ── Label ────────────────────────────────────────────────────────
            if (!s->showLabels || s->padNote[i] < 0) continue;

            char label[16] = {};
            int  note = s->padNote[i];
            if (note < 128 && GM_DRUM_NAMES[note])
                snprintf(label, sizeof(label), "%s", GM_DRUM_NAMES[note]);
            else
                snprintf(label, sizeof(label), "%d", note);
            int len = (int)strlen(label);
            if (len == 0) continue;

            // Scale to fit in 80 % drawW × 35 % drawH
            float maxTxtW = drawW * 0.80f;
            float maxTxtH = drawH * 0.35f;
            float scaleW  = maxTxtW / (len * 4.0f);
            float scaleH  = maxTxtH / 5.0f;
            float scale   = std::floor(std::min(scaleW, scaleH));
            if (scale < 1.0f) scale = 1.0f;

            float textW = (float)len * 4.0f * scale - scale; // remove trailing gap
            float textH = 5.0f * scale;

            // Centre horizontally; place in bottom 35 % of pad
            float tx = padX + (drawW - textW) * 0.5f;
            float ty = padY + drawH * 0.62f;
            if (ty + textH > padY + drawH) ty = padY + drawH - textH - 1.0f;

            // Text colour by background luminance
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
