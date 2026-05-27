#include "piano-source.hpp"
#include "../midi-engine.hpp"
#include "../plugin-support.h"
#include <obs-module.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard layout helpers
// ─────────────────────────────────────────────────────────────────────────────
static const bool kIsBlack[12] = {
    false, true,  false, true,  false,
    false, true,  false, true,  false,
    true,  false
};

static constexpr float kBlackW       = 0.58f;
static constexpr float kBlackH       = 0.62f;
static constexpr int   kWaterfallMax = 240;

// ─────────────────────────────────────────────────────────────────────────────
// Per-instance state
// ─────────────────────────────────────────────────────────────────────────────
struct PianoSource {
    static constexpr int MAX_CONTROLLERS = 4;

    // Per-controller note state
    struct Controller {
        int      portIndex = -1;
        uint32_t color     = 0xFF44AAFF;
    };
    Controller controllers[MAX_CONTROLLERS];

    // [controller][note]: held velocity (>0 = key down) and visual decay
    std::array<uint8_t, 128> noteHeld [MAX_CONTROLLERS] = {};
    std::array<float,   128> noteDecay[MAX_CONTROLLERS] = {};

    int      keyMin         = 21;
    int      keyMax         = 108;
    bool     showWaterfall  = false;
    uint32_t colorOff       = 0xFF222222;
    uint32_t cx             = 1280;
    uint32_t cy             = 200;
    int      keyboardHeight = 200;   // keyboard height in px (used when waterfall on)
    uint32_t midiHandle     = 0;

    // Waterfall rows — [controller][note] brightness per historical frame
    struct WFRow {
        std::array<std::array<float, 128>, MAX_CONTROLLERS> b = {};
    };
    std::deque<WFRow> waterfall;
};

// ─────────────────────────────────────────────────────────────────────────────
// Property key arrays (used in defaults / create / update / properties)
// ─────────────────────────────────────────────────────────────────────────────
static const char *kPortKeys[4]  = { "slot_0_port",  "slot_1_port",  "slot_2_port",  "slot_3_port"  };
static const char *kColorKeys[4] = { "slot_0_color", "slot_1_color", "slot_2_color", "slot_3_color" };

static const uint32_t kDefaultColors[4] = {
    0xFF44AAFF,   // slot 0 — blue
    0xFFFF4444,   // slot 1 — red
    0xFF44FF88,   // slot 2 — green
    0xFFFFAA00,   // slot 3 — orange
};

// ─────────────────────────────────────────────────────────────────────────────
// Colour helpers
// ─────────────────────────────────────────────────────────────────────────────
static void argb_to_vec4(uint32_t c, vec4 *out)
{
    vec4_set(out,
        ((c >> 16) & 0xFF) / 255.0f,
        ((c >>  8) & 0xFF) / 255.0f,
        ( c        & 0xFF) / 255.0f,
        ((c >> 24) & 0xFF) / 255.0f);
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

// Blend all active controller colors for a given note.
// Returns a color between colorOff and the weighted blend of active controllers.
static uint32_t blendKeyColor(const PianoSource *s, int n, uint32_t offColor)
{
    float totalW = 0.0f, r = 0, g = 0, b = 0;
    for (int k = 0; k < PianoSource::MAX_CONTROLLERS; ++k) {
        if (s->controllers[k].portIndex < 0) continue;
        float d = s->noteDecay[k][n];
        if (d < 0.005f) continue;
        uint32_t c = s->controllers[k].color;
        r += ((c >> 16) & 0xFF) * d;
        g += ((c >>  8) & 0xFF) * d;
        b += ( c        & 0xFF) * d;
        totalW += d;
    }
    if (totalW < 0.005f) return offColor;
    float t = std::min(1.0f, totalW);
    uint32_t avg = 0xFF000000u
                 | ((uint32_t)(r / totalW) << 16)
                 | ((uint32_t)(g / totalW) <<  8)
                 |  (uint32_t)(b / totalW);
    return lerp_argb(offColor, avg, t);
}

// Same blend for a waterfall row
static uint32_t blendWFColor(const PianoSource *s, const PianoSource::WFRow &row, int n)
{
    float totalW = 0.0f, r = 0, g = 0, b = 0;
    for (int k = 0; k < PianoSource::MAX_CONTROLLERS; ++k) {
        float t = row.b[k][n];
        if (t < 0.005f) continue;
        uint32_t c = s->controllers[k].color;
        r += ((c >> 16) & 0xFF) * t;
        g += ((c >>  8) & 0xFF) * t;
        b += ( c        & 0xFF) * t;
        totalW += t;
    }
    if (totalW < 0.005f) return 0;  // transparent — skip draw
    float ft = std::min(1.0f, totalW);
    uint32_t avg = 0xFF000000u
                 | ((uint32_t)(r / totalW) << 16)
                 | ((uint32_t)(g / totalW) <<  8)
                 |  (uint32_t)(b / totalW);
    return lerp_argb(0xFF000000u, avg, ft);
}

// ─────────────────────────────────────────────────────────────────────────────
// Open a MIDI port; never closes others — multiple ports coexist.
// ─────────────────────────────────────────────────────────────────────────────
static void apply_midi_port(int portIdx)
{
    if (portIdx >= 0)
        MidiEngine::instance().openPort(portIdx);
}

// ─────────────────────────────────────────────────────────────────────────────
// OBS callbacks
// ─────────────────────────────────────────────────────────────────────────────
static const char *piano_get_name(void *) { return obs_module_text("PianoSource.Name"); }

static void load_settings(PianoSource *s, obs_data_t *settings)
{
    for (int k = 0; k < PianoSource::MAX_CONTROLLERS; ++k) {
        s->controllers[k].portIndex = (int)obs_data_get_int(settings, kPortKeys[k]);
        s->controllers[k].color     = (uint32_t)obs_data_get_int(settings, kColorKeys[k]);
        apply_midi_port(s->controllers[k].portIndex);
    }
    s->keyMin         = (int)obs_data_get_int( settings, "key_min");
    s->keyMax         = (int)obs_data_get_int( settings, "key_max");
    s->showWaterfall  = obs_data_get_bool(      settings, "waterfall");
    s->colorOff       = (uint32_t)obs_data_get_int(settings, "color_off");
    s->cx             = (uint32_t)obs_data_get_int(settings, "width");
    s->cy             = (uint32_t)obs_data_get_int(settings, "height");
    s->keyboardHeight = (int)obs_data_get_int( settings, "keyboard_height");
}

static void *piano_create(obs_data_t *settings, obs_source_t *source)
{
    (void)source;
    auto *s = new PianoSource{};
    load_settings(s, settings);

    s->midiHandle = MidiEngine::instance().subscribe([s](const MidiEvent &ev) {
        if (ev.type != MidiEventType::NoteOn && ev.type != MidiEventType::NoteOff) return;
        if (ev.param1 >= 128) return;
        for (int k = 0; k < PianoSource::MAX_CONTROLLERS; ++k) {
            if (s->controllers[k].portIndex != ev.portIndex) continue;
            if (ev.type == MidiEventType::NoteOn) {
                s->noteHeld [k][ev.param1] = ev.param2;
                s->noteDecay[k][ev.param1] = 1.0f;
            } else {
                s->noteHeld[k][ev.param1] = 0;
            }
        }
    });

    MIDI_LOG_INFO("Piano source created (range %d-%d)", s->keyMin, s->keyMax);
    return s;
}

static void piano_destroy(void *data)
{
    auto *s = static_cast<PianoSource *>(data);
    MidiEngine::instance().unsubscribe(s->midiHandle);
    delete s;
}

static void piano_defaults(obs_data_t *settings)
{
    for (int k = 0; k < 4; ++k) {
        obs_data_set_default_int(settings, kPortKeys[k],  -1);
        obs_data_set_default_int(settings, kColorKeys[k], (int64_t)kDefaultColors[k]);
    }
    obs_data_set_default_int( settings, "key_min",         21);
    obs_data_set_default_int( settings, "key_max",         108);
    obs_data_set_default_bool(settings, "waterfall",       false);
    obs_data_set_default_int( settings, "color_off",       (int64_t)0xFF222222u);
    obs_data_set_default_int( settings, "keyboard_height", 200);
    obs_data_set_default_int( settings, "width",           1280);
    obs_data_set_default_int( settings, "height",          200);
}

// Populate a MIDI port list property from current available ports
static void populate_port_list(obs_property_t *lst)
{
    obs_property_list_clear(lst);
    obs_property_list_add_int(lst, "Disabled", -1);
    auto ports = MidiEngine::instance().portNames();
    for (int i = 0; i < (int)ports.size(); ++i)
        obs_property_list_add_int(lst, ports[i].c_str(), i);
}

static obs_properties_t *piano_properties(void *)
{
    obs_properties_t *p = obs_properties_create();

    static const char *slotLabels[4] = {
        "Controller 1 — Device",
        "Controller 2 — Device",
        "Controller 3 — Device",
        "Controller 4 — Device",
    };
    static const char *colorLabels[4] = {
        "Controller 1 — Colour",
        "Controller 2 — Colour",
        "Controller 3 — Colour",
        "Controller 4 — Colour",
    };

    for (int k = 0; k < 4; ++k) {
        obs_property_t *lst = obs_properties_add_list(p, kPortKeys[k],
            slotLabels[k], OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
        populate_port_list(lst);
        obs_properties_add_color(p, kColorKeys[k], colorLabels[k]);
    }

    obs_properties_add_button(p, "midi_refresh", "Refresh MIDI Devices",
        [](obs_properties_t *pp, obs_property_t *, void *) -> bool {
            for (int k = 0; k < 4; ++k)
                populate_port_list(obs_properties_get(pp, kPortKeys[k]));
            return true;
        });

    obs_properties_add_int(  p, "key_min",         "Lowest key (MIDI #)",      0, 127, 1);
    obs_properties_add_int(  p, "key_max",         "Highest key (MIDI #)",     0, 127, 1);
    obs_properties_add_color(p, "color_off",       "Resting key colour");
    obs_properties_add_bool( p, "waterfall",       "Falling note waterfall");
    obs_properties_add_int(  p, "keyboard_height", "Keyboard height (px)",    32, 800, 8);
    obs_properties_add_int(  p, "width",           "Canvas width (px)",       64, 7680, 1);
    obs_properties_add_int(  p, "height",          "Canvas height (px)",      32, 4320, 1);
    return p;
}

static void piano_update(void *data, obs_data_t *settings)
{
    load_settings(static_cast<PianoSource *>(data), settings);
}

static void piano_tick(void *data, float seconds)
{
    auto *s = static_cast<PianoSource *>(data);
    MidiEngine::instance().drainQueue();

    constexpr float kDecayRate = 3.0f;
    for (int k = 0; k < PianoSource::MAX_CONTROLLERS; ++k) {
        for (int i = 0; i < 128; ++i) {
            if (s->noteHeld[k][i] == 0 && s->noteDecay[k][i] > 0.0f)
                s->noteDecay[k][i] = std::max(0.0f, s->noteDecay[k][i] - kDecayRate * seconds);
        }
    }

    if (s->showWaterfall) {
        PianoSource::WFRow row;
        for (int k = 0; k < PianoSource::MAX_CONTROLLERS; ++k) {
            if (s->controllers[k].portIndex < 0) continue;
            for (int n = 0; n < 128; ++n)
                row.b[k][n] = (s->noteHeld[k][n] > 0) ? 1.0f : s->noteDecay[k][n];
        }
        s->waterfall.push_back(std::move(row));
        while ((int)s->waterfall.size() > kWaterfallMax)
            s->waterfall.pop_front();
    }
}

static uint32_t piano_get_width (void *d) { return static_cast<PianoSource*>(d)->cx; }
static uint32_t piano_get_height(void *d) { return static_cast<PianoSource*>(d)->cy; }

// ─────────────────────────────────────────────────────────────────────────────
// Render
//
// Layout when waterfall is ON:
//   y = 0 .. (H - keyboardHeight)  ← waterfall (falling notes)
//   y = (H - keyboardHeight) .. H  ← keyboard
//
// Layout when waterfall is OFF:
//   y = 0 .. H  ← keyboard fills entire canvas
//
// Set canvas height = scene height and keyboardHeight = desired key strip
// height. The source can then be placed full-screen; the keys sit at the
// bottom and the waterfall extends all the way to the top of the scene.
//
// gs_effect_loop() cannot be called inside video_render — OBS already has
// an effect active when it invokes us. Use gs_technique_begin/end directly.
// ─────────────────────────────────────────────────────────────────────────────
static void piano_render(void *data, gs_effect_t *effect)
{
    (void)effect;
    auto *s = static_cast<PianoSource *>(data);

    const int   keyMin = std::max(0,      std::min(s->keyMin, 127));
    const int   keyMax = std::max(keyMin, std::min(s->keyMax, 127));
    const float W      = (float)s->cx;
    const float H      = (float)s->cy;

    // Count white keys in range
    int numWhite = 0;
    for (int n = keyMin; n <= keyMax; ++n)
        if (!kIsBlack[n % 12]) ++numWhite;
    if (numWhite == 0) return;

    // Layout
    const float kbH = s->showWaterfall
                      ? std::min((float)s->keyboardHeight, H)
                      : H;
    const float kbY = H - kbH;   // top of keyboard strip
    const float wkw = W / (float)numWhite;
    const float bkw = wkw * kBlackW;
    const float bkh = kbH * kBlackH;

    // Precompute key geometry
    struct KeyGeom { float x, w; bool valid; };
    KeyGeom geom[128]{};
    {
        int wki = 0;
        for (int n = keyMin; n <= keyMax; ++n) {
            if (!kIsBlack[n % 12]) {
                geom[n] = { (float)wki * wkw, wkw - 1.0f, true };
                ++wki;
            } else {
                geom[n] = wki == 0
                    ? KeyGeom{ 0, 0, false }
                    : KeyGeom{ (float)wki * wkw - bkw * 0.5f, bkw, true };
            }
        }
    }

    // Begin SOLID technique once; all draws happen within the single pass
    gs_effect_t    *solid      = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t    *colorParam = gs_effect_get_param_by_name(solid, "color");
    gs_technique_t *tech       = gs_effect_get_technique(solid, "Solid");
    size_t          passes     = gs_technique_begin(tech);

    auto drawRect = [&](float x, float y, float w, float h, uint32_t argb) {
        if (w < 1.0f || h < 1.0f) return;
        vec4 col;
        argb_to_vec4(argb, &col);
        gs_effect_set_vec4(colorParam, &col);
        gs_matrix_push();
        gs_matrix_translate3f(x, y, 0.0f);
        gs_draw_sprite(nullptr, 0, (uint32_t)w, (uint32_t)h);
        gs_matrix_pop();
    };

    for (size_t p = 0; p < passes; ++p) {
        gs_technique_begin_pass(tech, p);

        // ── Waterfall ───────────────────────────────────────────────────────
        if (s->showWaterfall && !s->waterfall.empty() && kbY > 1.0f) {
            const int   rows = (int)s->waterfall.size();
            const float rowH = kbY / (float)rows;

            for (int i = 0; i < rows; ++i) {
                float ry = (float)i * rowH;  // i=0 oldest → top
                const auto &row = s->waterfall[i];

                for (int n = keyMin; n <= keyMax; ++n) {
                    if (!geom[n].valid) continue;
                    uint32_t c = blendWFColor(s, row, n);
                    if ((c >> 24) < 4) continue;  // essentially transparent
                    drawRect(geom[n].x, ry, geom[n].w, rowH + 1.0f, c);
                }
            }
        }

        // ── White keys ──────────────────────────────────────────────────────
        for (int n = keyMin; n <= keyMax; ++n) {
            if (kIsBlack[n % 12] || !geom[n].valid) continue;
            drawRect(geom[n].x, kbY, geom[n].w, kbH,
                     blendKeyColor(s, n, s->colorOff));
        }

        // ── Black keys (drawn on top) ────────────────────────────────────────
        const uint32_t blackOff = lerp_argb(s->colorOff, 0xFF000000u, 0.55f);
        for (int n = keyMin; n <= keyMax; ++n) {
            if (!kIsBlack[n % 12] || !geom[n].valid) continue;
            drawRect(geom[n].x, kbY, geom[n].w, bkh,
                     blendKeyColor(s, n, blackOff));
        }

        gs_technique_end_pass(tech);
    }

    gs_technique_end(tech);
}

// ─────────────────────────────────────────────────────────────────────────────
void piano_source_register(void)
{
    static obs_source_info info{};
    info.id             = "midi_piano_source";
    info.type           = OBS_SOURCE_TYPE_INPUT;
    info.output_flags   = OBS_SOURCE_VIDEO;
    info.get_name       = piano_get_name;
    info.create         = piano_create;
    info.destroy        = piano_destroy;
    info.get_defaults   = piano_defaults;
    info.get_properties = piano_properties;
    info.update         = piano_update;
    info.video_tick     = piano_tick;
    info.video_render   = piano_render;
    info.get_width      = piano_get_width;
    info.get_height     = piano_get_height;
    obs_register_source(&info);
    MIDI_LOG_INFO("Registered source: midi_piano_source");
}
