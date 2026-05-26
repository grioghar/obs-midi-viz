#include "cc-source.hpp"
#include "../midi-engine.hpp"
#include "../plugin-support.h"
#include <obs-module.h>
#include <array>

// Visualises up to 8 MIDI CC lanes as vertical bars (mod wheel, expression, etc.)
struct CcSource {
    static constexpr int MAX_LANES = 8;

    int     ccNumbers[MAX_LANES];   // Which CC number each lane tracks
    float   ccValues[MAX_LANES];    // Current value 0.0–1.0 (smoothed)
    float   ccTargets[MAX_LANES];   // Raw target from incoming events
    char    ccLabels[MAX_LANES][32];// Display name per lane

    uint32_t colorBar  = 0xFF00AAFF;
    uint32_t colorBg   = 0xFF111111;
    bool     showValue = true;
    float    smoothing = 0.1f;  // lerp factor per tick (0=instant, 1=frozen)

    uint32_t midiHandle = 0;
    uint32_t cx = 400;
    uint32_t cy = 120;

    CcSource()
    {
        // Defaults: mod wheel, breath, expression, sustain, volume, pan, filter, resonance
        int defaults[MAX_LANES] = {1, 2, 11, 64, 7, 10, 74, 71};
        const char *labels[MAX_LANES] = {"Mod","Breath","Expr","Sustain","Vol","Pan","Filter","Reso"};
        for (int i = 0; i < MAX_LANES; ++i) {
            ccNumbers[i] = defaults[i];
            ccValues[i]  = 0.0f;
            ccTargets[i] = 0.0f;
            snprintf(ccLabels[i], sizeof(ccLabels[i]), "%s", labels[i]);
        }
    }
};

static const char *cc_get_name(void *) { return obs_module_text("CcSource.Name"); }

static void *cc_create(obs_data_t *settings, obs_source_t *source)
{
    (void)source;
    auto *s = new CcSource{};
    s->colorBar  = (uint32_t)obs_data_get_int(settings,  "color_bar");
    s->colorBg   = (uint32_t)obs_data_get_int(settings,  "color_bg");
    s->showValue = obs_data_get_bool(settings, "show_value");
    s->smoothing = (float)obs_data_get_double(settings,  "smoothing");

    s->midiHandle = MidiEngine::instance().subscribe([s](const MidiEvent &ev) {
        if (ev.type != MidiEventType::ControlChange) return;
        for (int i = 0; i < CcSource::MAX_LANES; ++i) {
            if (s->ccNumbers[i] == ev.param1)
                s->ccTargets[i] = ev.param2 / 127.0f;
        }
    });

    MIDI_LOG_INFO("CC source created");
    return s;
}

static void cc_destroy(void *data)
{
    auto *s = static_cast<CcSource *>(data);
    MidiEngine::instance().unsubscribe(s->midiHandle);
    delete s;
}

static void cc_defaults(obs_data_t *settings)
{
    obs_data_set_default_int(settings,    "color_bar",  0xFF00AAFF);
    obs_data_set_default_int(settings,    "color_bg",   0xFF111111);
    obs_data_set_default_bool(settings,   "show_value", true);
    obs_data_set_default_double(settings, "smoothing",  0.1);
}

static obs_properties_t *cc_properties(void *)
{
    obs_properties_t *props = obs_properties_create();
    obs_properties_add_color(props,  "color_bar",  "Bar colour");
    obs_properties_add_color(props,  "color_bg",   "Background colour");
    obs_properties_add_bool(props,   "show_value", "Show numeric value");
    obs_properties_add_float_slider(props, "smoothing", "Smoothing", 0.0, 1.0, 0.01);
    return props;
}

static void cc_tick(void *data, float seconds)
{
    auto *s = static_cast<CcSource *>(data);
    MidiEngine::instance().drainQueue();
    // Smooth toward target each tick
    float alpha = 1.0f - powf(s->smoothing, seconds * 60.0f);
    for (int i = 0; i < CcSource::MAX_LANES; ++i)
        s->ccValues[i] += (s->ccTargets[i] - s->ccValues[i]) * alpha;
}

static uint32_t cc_get_width(void  *data) { return static_cast<CcSource*>(data)->cx; }
static uint32_t cc_get_height(void *data) { return static_cast<CcSource*>(data)->cy; }

static void cc_render(void *data, gs_effect_t *effect)
{
    // TODO (Phase 5): draw actual bar lanes with labels + values
    (void)effect;
    auto *s = static_cast<CcSource *>(data);
    float avg = 0;
    for (int i = 0; i < CcSource::MAX_LANES; ++i) avg += s->ccValues[i];
    avg /= CcSource::MAX_LANES;

    uint32_t c = (avg > 0.01f) ? s->colorBar : s->colorBg;
    gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t *col   = gs_effect_get_param_by_name(solid, "color");
    struct vec4 v4;
    vec4_set(&v4,
        ((c >> 16) & 0xFF) / 255.0f,
        ((c >>  8) & 0xFF) / 255.0f,
        ( c        & 0xFF) / 255.0f,
        ((c >> 24) & 0xFF) / 255.0f);
    gs_effect_set_vec4(col, &v4);
    while (gs_effect_loop(solid, "Solid"))
        gs_draw_sprite(nullptr, 0, s->cx, s->cy);
}

void cc_source_register(void)
{
    static obs_source_info info{};
    info.id             = "midi_cc_source";
    info.type           = OBS_SOURCE_TYPE_INPUT;
    info.output_flags   = OBS_SOURCE_VIDEO;
    info.get_name       = cc_get_name;
    info.create         = cc_create;
    info.destroy        = cc_destroy;
    info.get_defaults   = cc_defaults;
    info.get_properties = cc_properties;
    info.update         = [](void *d, obs_data_t *s){ cc_destroy(d); cc_create(s, nullptr); };
    info.video_tick     = cc_tick;
    info.video_render   = cc_render;
    info.get_width      = cc_get_width;
    info.get_height     = cc_get_height;
    obs_register_source(&info);
    MIDI_LOG_INFO("Registered source: midi_cc_source");
}
