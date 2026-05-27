#include "piano-source.hpp"
#include "../midi-engine.hpp"
#include "../plugin-support.h"
#include <obs-module.h>
#include <algorithm>
#include <array>
#include <deque>

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard layout
// ─────────────────────────────────────────────────────────────────────────────
// semitone 0=C 1=C# 2=D 3=D# 4=E 5=F 6=F# 7=G 8=G# 9=A 10=A# 11=B
static const bool kIsBlack[12] = {
    false, true,  false, true,  false,
    false, true,  false, true,  false,
    true,  false
};

static constexpr float kBlackW       = 0.58f;  // fraction of white key width
static constexpr float kBlackH       = 0.62f;  // fraction of keyboard height
static constexpr int   kWaterfallMax = 180;    // rows of history

// ─────────────────────────────────────────────────────────────────────────────
// Per-instance state
// ─────────────────────────────────────────────────────────────────────────────
struct PianoSource {
    std::array<uint8_t, 128> noteVelocity{};
    std::array<float,   128> noteDecay{};

    int      keyMin        = 21;   // A0
    int      keyMax        = 108;  // C8
    bool     showWaterfall = false;
    uint32_t colorOn       = 0xFF44AAFF;
    uint32_t colorOff      = 0xFF222222;
    uint32_t cx            = 1280;
    uint32_t cy            = 200;
    uint32_t midiHandle    = 0;
    int      midiPort      = -1;   // -1 = disabled

    // Waterfall: index 0 = oldest (top), back = newest (bottom / closest to keys)
    std::deque<std::array<float, 128>> waterfall;
};

// ─────────────────────────────────────────────────────────────────────────────
// Colour helpers  (OBS stores color properties as ARGB uint32)
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

// ─────────────────────────────────────────────────────────────────────────────
// MIDI port helper — shared logic for create/update
// Open the selected port; close if disabled (-1).
// Skips the reopen if the port is already open and matches.
// ─────────────────────────────────────────────────────────────────────────────
static void apply_midi_port(int portIdx)
{
    auto &eng = MidiEngine::instance();
    if (portIdx < 0) {
        if (eng.isOpen()) eng.closePort();
    } else if (portIdx != eng.currentPort() || !eng.isOpen()) {
        eng.openPort(portIdx);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OBS callbacks
// ─────────────────────────────────────────────────────────────────────────────
static const char *piano_get_name(void *) { return obs_module_text("PianoSource.Name"); }

static void *piano_create(obs_data_t *settings, obs_source_t *source)
{
    (void)source;
    auto *s          = new PianoSource{};
    s->keyMin        = (int)obs_data_get_int( settings, "key_min");
    s->keyMax        = (int)obs_data_get_int( settings, "key_max");
    s->showWaterfall = obs_data_get_bool(     settings, "waterfall");
    s->colorOn       = (uint32_t)obs_data_get_int(settings, "color_on");
    s->colorOff      = (uint32_t)obs_data_get_int(settings, "color_off");
    s->cx            = (uint32_t)obs_data_get_int(settings, "width");
    s->cy            = (uint32_t)obs_data_get_int(settings, "height");
    s->midiPort      = (int)obs_data_get_int(settings, "midi_port");

    apply_midi_port(s->midiPort);

    s->midiHandle = MidiEngine::instance().subscribe([s](const MidiEvent &ev) {
        if (ev.type == MidiEventType::NoteOn) {
            s->noteVelocity[ev.param1] = ev.param2;
            s->noteDecay   [ev.param1] = 1.0f;
        } else if (ev.type == MidiEventType::NoteOff) {
            s->noteVelocity[ev.param1] = 0;
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
    obs_data_set_default_int( settings, "midi_port",  -1);
    obs_data_set_default_int( settings, "key_min",    21);
    obs_data_set_default_int( settings, "key_max",    108);
    obs_data_set_default_bool(settings, "waterfall",  false);
    obs_data_set_default_int( settings, "color_on",   0xFF44AAFF);
    obs_data_set_default_int( settings, "color_off",  0xFF222222);
    obs_data_set_default_int( settings, "width",      1280);
    obs_data_set_default_int( settings, "height",     200);
}

static obs_properties_t *piano_properties(void *)
{
    obs_properties_t *p = obs_properties_create();

    // MIDI device selection — always at the top
    obs_property_t *port_list = obs_properties_add_list(p, "midi_port",
        "MIDI Input Device", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(port_list, "Disabled", -1);
    auto ports = MidiEngine::instance().portNames();
    for (int i = 0; i < (int)ports.size(); ++i)
        obs_property_list_add_int(port_list, ports[i].c_str(), i);

    obs_properties_add_button(p, "midi_refresh", "Refresh Devices",
        [](obs_properties_t *pp, obs_property_t *, void *) -> bool {
            obs_property_t *lst = obs_properties_get(pp, "midi_port");
            obs_property_list_clear(lst);
            obs_property_list_add_int(lst, "Disabled", -1);
            auto ps = MidiEngine::instance().portNames();
            for (int i = 0; i < (int)ps.size(); ++i)
                obs_property_list_add_int(lst, ps[i].c_str(), i);
            return true;
        });

    obs_properties_add_int(  p, "key_min",   "Lowest key (MIDI #)",  0, 127, 1);
    obs_properties_add_int(  p, "key_max",   "Highest key (MIDI #)", 0, 127, 1);
    obs_properties_add_bool( p, "waterfall", "Falling note waterfall");
    obs_properties_add_color(p, "color_on",  "Active key colour");
    obs_properties_add_color(p, "color_off", "Resting key colour");
    obs_properties_add_int(  p, "width",     "Width (px)",  64, 7680, 1);
    obs_properties_add_int(  p, "height",    "Height (px)", 32, 4320, 1);
    return p;
}

static void piano_update(void *data, obs_data_t *settings)
{
    auto *s          = static_cast<PianoSource *>(data);
    s->keyMin        = (int)obs_data_get_int( settings, "key_min");
    s->keyMax        = (int)obs_data_get_int( settings, "key_max");
    s->showWaterfall = obs_data_get_bool(     settings, "waterfall");
    s->colorOn       = (uint32_t)obs_data_get_int(settings, "color_on");
    s->colorOff      = (uint32_t)obs_data_get_int(settings, "color_off");
    s->cx            = (uint32_t)obs_data_get_int(settings, "width");
    s->cy            = (uint32_t)obs_data_get_int(settings, "height");
    s->midiPort      = (int)obs_data_get_int(settings, "midi_port");
    apply_midi_port(s->midiPort);
}

static void piano_tick(void *data, float seconds)
{
    auto *s = static_cast<PianoSource *>(data);
    MidiEngine::instance().drainQueue();

    constexpr float kDecayRate = 3.0f;
    for (int i = 0; i < 128; ++i) {
        if (s->noteVelocity[i] == 0 && s->noteDecay[i] > 0.0f)
            s->noteDecay[i] = std::max(0.0f, s->noteDecay[i] - kDecayRate * seconds);
    }

    if (s->showWaterfall) {
        std::array<float, 128> row{};
        for (int n = 0; n < 128; ++n)
            row[n] = (s->noteVelocity[n] > 0) ? 1.0f : s->noteDecay[n];
        s->waterfall.push_back(row);
        while ((int)s->waterfall.size() > kWaterfallMax)
            s->waterfall.pop_front();
    }
}

static uint32_t piano_get_width (void *d) { return static_cast<PianoSource*>(d)->cx; }
static uint32_t piano_get_height(void *d) { return static_cast<PianoSource*>(d)->cy; }

// ─────────────────────────────────────────────────────────────────────────────
// Render
//
// NOTE: OBS calls video_render from within its own compositing pass, so an
// effect is already active when we arrive here. gs_effect_loop() checks an
// "effect_in_use" guard and prints "An effect is already active" if you nest
// it. Use gs_technique_begin/end directly to bypass that guard — perfectly
// legal from a render callback and how many built-in OBS sources work.
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

    // Geometry
    const float kbH = s->showWaterfall ? H * 0.38f : H;  // keyboard height
    const float kbY = H - kbH;                            // keyboard top y
    const float wkw = W / (float)numWhite;                // white key width
    const float bkw = wkw * kBlackW;
    const float bkh = kbH * kBlackH;

    // Precompute each key's x and width once
    struct KeyGeom { float x, w; bool valid; };
    KeyGeom geom[128]{};
    {
        int wki = 0;
        for (int n = keyMin; n <= keyMax; ++n) {
            if (!kIsBlack[n % 12]) {
                geom[n] = { (float)wki * wkw, wkw - 1.0f, true };
                ++wki;
            } else {
                // Center on boundary between left and right white keys.
                if (wki == 0) {
                    geom[n] = { 0, 0, false };  // no left sibling in range
                } else {
                    geom[n] = { (float)wki * wkw - bkw * 0.5f, bkw, true };
                }
            }
        }
    }

    // Begin the SOLID effect technique directly (no gs_effect_loop nesting)
    gs_effect_t    *solid      = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t    *colorParam = gs_effect_get_param_by_name(solid, "color");
    gs_technique_t *tech       = gs_effect_get_technique(solid, "Solid");
    size_t          passes     = gs_technique_begin(tech);

    // drawRect: set color uniform and draw; called within the outer pass loop
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

        // ── Waterfall (falling notes above keyboard) ─────────────────────────
        if (s->showWaterfall && !s->waterfall.empty()) {
            const int   rows = (int)s->waterfall.size();
            const float rowH = kbY / (float)rows;

            for (int n = keyMin; n <= keyMax; ++n) {
                if (!geom[n].valid) continue;
                for (int i = 0; i < rows; ++i) {
                    float t = s->waterfall[i][n];
                    if (t < 0.005f) continue;
                    // i=0 oldest → top (y=0); i=rows-1 newest → bottom (y≈kbY)
                    float ry = (float)i * rowH;
                    drawRect(geom[n].x, ry, geom[n].w, rowH + 1.0f,
                             lerp_argb(0xFF000000u, s->colorOn, t));
                }
            }
        }

        // ── White keys ───────────────────────────────────────────────────────
        for (int n = keyMin; n <= keyMax; ++n) {
            if (kIsBlack[n % 12] || !geom[n].valid) continue;
            drawRect(geom[n].x, kbY, geom[n].w, kbH,
                     lerp_argb(s->colorOff, s->colorOn, s->noteDecay[n]));
        }

        // ── Black keys (on top of white keys) ────────────────────────────────
        const uint32_t blackOff = lerp_argb(s->colorOff, 0xFF000000u, 0.55f);
        for (int n = keyMin; n <= keyMax; ++n) {
            if (!kIsBlack[n % 12] || !geom[n].valid) continue;
            drawRect(geom[n].x, kbY, geom[n].w, bkh,
                     lerp_argb(blackOff, s->colorOn, s->noteDecay[n]));
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
