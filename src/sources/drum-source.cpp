#include "drum-source.hpp"
#include "../midi-engine.hpp"
#include "../plugin-support.h"
#include <obs-module.h>
#include <algorithm>
#include <array>

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
    {5,5,7,5,5}, // N  (differs from M: mid-row filled vs top)
    {2,5,5,5,2}, // O
    {6,5,6,4,4}, // P
    {2,5,5,7,3}, // Q
    {6,5,6,5,5}, // R
    {3,4,2,1,6}, // S
    {7,2,2,2,2}, // T
    {5,5,5,5,7}, // U
    {5,5,5,2,2}, // V
    {5,5,7,5,5}, // W  (accepted visual overlap with N; context disambiguates)
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

struct DrumSource {
    static constexpr int MAX_PADS = 64;

    int     padNote[MAX_PADS];   // MIDI note number; -1 = unused
    float   padFlash[MAX_PADS];  // 0.0–1.0 brightness (decays each tick)

    int  columns    = 4;
    int  rows       = 4;
    int  baseNote   = 36;   // MIDI note for the first pad (GM = 36 = Bass Drum 1)
    bool showLabels = true;

    uint32_t colorHit  = 0xFFFF6600;
    uint32_t colorIdle = 0xFF1A1A2E;

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

static const char *drum_get_name(void *) { return obs_module_text("DrumSource.Name"); }

static void *drum_create(obs_data_t *settings, obs_source_t *source)
{
    (void)source;
    auto *s = new DrumSource{};
    s->columns    = (int)obs_data_get_int(settings, "columns");
    s->rows       = (int)obs_data_get_int(settings, "rows");
    s->baseNote   = (int)obs_data_get_int(settings, "base_note");
    s->showLabels = obs_data_get_bool(settings, "show_labels");
    s->colorHit   = (uint32_t)obs_data_get_int(settings, "color_hit");
    s->colorIdle  = (uint32_t)obs_data_get_int(settings, "color_idle");
    s->midiPort   = (int)obs_data_get_int(settings, "midi_port");

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
            // Help the user diagnose controller mappings.
            // OBS log: Tools → Log Files → View Current Log File
            MIDI_LOG_INFO(
                "Drum: received unmapped NoteOn %d (vel=%d ch=%d port=%d). "
                "Adjust 'Base Note' in source properties if this is your pad controller.",
                (int)ev.param1, (int)ev.param2, (int)ev.channel, ev.portIndex);
        }
    });

    MIDI_LOG_INFO("Drum source created (%dx%d, baseNote=%d)", s->columns, s->rows, s->baseNote);
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
    obs_data_set_default_int( settings, "midi_port",    -1);
    obs_data_set_default_int( settings, "columns",       4);
    obs_data_set_default_int( settings, "rows",          4);
    obs_data_set_default_int( settings, "base_note",    36);
    obs_data_set_default_bool(settings, "show_labels",  true);
    obs_data_set_default_int( settings, "color_hit",    (int64_t)0xFFFF6600u);
    obs_data_set_default_int( settings, "color_idle",   (int64_t)0xFF1A1A2Eu);
}

static obs_properties_t *drum_properties(void *)
{
    obs_properties_t *props = obs_properties_create();

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

    obs_properties_add_int( props, "columns",    "Grid columns",          1, 8, 1);
    obs_properties_add_int( props, "rows",       "Grid rows",             1, 8, 1);
    obs_properties_add_int( props, "base_note",  "Base Note (first pad)", 0, 120, 1);
    obs_properties_add_bool(props, "show_labels","Show pad labels");
    obs_properties_add_color(props, "color_hit",  "Hit colour");
    obs_properties_add_color(props, "color_idle", "Idle colour");
    return props;
}

static void drum_update(void *data, obs_data_t *settings)
{
    auto *s = static_cast<DrumSource *>(data);
    s->columns    = (int)obs_data_get_int(settings, "columns");
    s->rows       = (int)obs_data_get_int(settings, "rows");
    s->baseNote   = (int)obs_data_get_int(settings, "base_note");
    s->showLabels = obs_data_get_bool(settings, "show_labels");
    s->colorHit   = (uint32_t)obs_data_get_int(settings, "color_hit");
    s->colorIdle  = (uint32_t)obs_data_get_int(settings, "color_idle");
    s->midiPort   = (int)obs_data_get_int(settings, "midi_port");
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
// Phase 4 render — actual pad grid with per-pad colour and bitmap labels.
//
// Layout:
//   gap  = 5% of the smaller pad dimension (min 2 px)
//   padW = (cx - gap*(cols+1)) / cols
//   padH = (cy - gap*(rows+1)) / rows
//
// Each pad is lerped from colorIdle → colorHit by padFlash[i].
// When showLabels is true, a GM drum name (or MIDI note number for
// unmapped notes) is drawn in the bottom ~35% of each pad using the
// 3×5 bitmap font, auto-scaled to fit.
// Text colour is chosen by background luminance (white on dark, black on light).
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

    // Gap between pads (proportional; at least 2 px)
    const float minDim = std::min(W / cols, H / rows);
    const float gap    = std::max(2.0f, minDim * 0.05f);
    const float padW   = (W - gap * (cols + 1)) / cols;
    const float padH   = (H - gap * (rows + 1)) / rows;
    if (padW < 2.0f || padH < 2.0f) return;

    gs_effect_t    *solid      = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t    *colorParam = gs_effect_get_param_by_name(solid, "color");
    gs_technique_t *tech       = gs_effect_get_technique(solid, "Solid");
    size_t          passes     = gs_technique_begin(tech);

    auto drawRect = [&](float x, float y, float w, float h, uint32_t argb) {
        if (w < 1.0f || h < 1.0f) return;
        struct vec4 col;
        vec4_set(&col,
            ((argb >> 16) & 0xFF) / 255.0f,
            ((argb >>  8) & 0xFF) / 255.0f,
            ( argb        & 0xFF) / 255.0f,
            ((argb >> 24) & 0xFF) / 255.0f);
        gs_effect_set_vec4(colorParam, &col);
        gs_matrix_push();
        gs_matrix_translate3f(x, y, 0.0f);
        gs_draw_sprite(nullptr, 0, (uint32_t)w, (uint32_t)h);
        gs_matrix_pop();
    };

    for (size_t p = 0; p < passes; ++p) {
        gs_technique_begin_pass(tech, p);

        const int total = cols * rows;
        for (int i = 0; i < total && i < DrumSource::MAX_PADS; ++i) {
            const int   col = i % cols;
            const int   row = i / cols;
            const float px  = gap + col * (padW + gap);
            const float py  = gap + row * (padH + gap);

            // ── Pad background ────────────────────────────────────────────
            const float t = s->padFlash[i];
            const uint32_t padColor = lerp_argb(s->colorIdle, s->colorHit, t);
            drawRect(px, py, padW, padH, padColor);

            // ── Label ─────────────────────────────────────────────────────
            if (!s->showLabels || s->padNote[i] < 0) continue;

            char label[16] = {};
            int  note = s->padNote[i];
            if (note < 128 && GM_DRUM_NAMES[note])
                snprintf(label, sizeof(label), "%s", GM_DRUM_NAMES[note]);
            else
                snprintf(label, sizeof(label), "%d", note);
            int len = (int)strlen(label);
            if (len == 0) continue;

            // Pick scale so the label fits in 80% width × 35% height of pad
            float maxW = padW * 0.80f;
            float maxH = padH * 0.35f;
            // Each character cell: 3px glyph + 1px gap = 4px wide; 5px tall
            float scaleW  = maxW / (len * 4.0f);
            float scaleH  = maxH / 5.0f;
            float scale   = std::floor(std::min(scaleW, scaleH));
            if (scale < 1.0f) scale = 1.0f;

            float cellW  = 4.0f * scale;  // glyph (3) + gap (1)
            float textW  = len  * cellW - scale; // remove trailing gap
            float textH  = 5.0f * scale;

            // Centre horizontally; bottom ~35% of pad vertically
            float tx = px + (padW - textW) * 0.5f;
            float ty = py + padH * 0.62f;
            // Clamp so text doesn't spill outside pad
            if (ty + textH > py + padH) ty = py + padH - textH - 1.0f;

            // Text colour: white on dark pads, black on bright pads
            float lr = ((padColor >> 16) & 0xFF) / 255.0f;
            float lg = ((padColor >>  8) & 0xFF) / 255.0f;
            float lb = ( padColor        & 0xFF) / 255.0f;
            float lum = 0.299f*lr + 0.587f*lg + 0.114f*lb;
            uint32_t textColor = (lum > 0.45f) ? 0xFF000000u : 0xFFFFFFFFu;

            // Draw each character
            for (int ci = 0; ci < len; ++ci) {
                int fi = fontIdx(label[ci]);
                float cx = tx + ci * cellW;
                for (int r = 0; r < 5; ++r) {
                    uint8_t bits = kFont3x5[fi][r];
                    for (int c = 0; c < 3; ++c) {
                        if (bits & (1u << (2 - c))) {
                            drawRect(cx + c * scale,
                                     ty + r * scale,
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
