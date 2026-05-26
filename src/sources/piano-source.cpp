#include "piano-source.hpp"
#include "../midi-engine.hpp"
#include "../plugin-support.h"
#include <obs-module.h>
#include <array>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Per-instance state
// ─────────────────────────────────────────────────────────────────────────────
struct PianoSource {
    // Active note state: velocity (1–127) means on, 0 means off
    std::array<uint8_t, 128> noteVelocity{};

    // Decay tracker for visual fade-out after note-off
    std::array<float, 128> noteDecay{};

    // Configurable key range (MIDI note numbers)
    int  keyMin     = 21;   // A0
    int  keyMax     = 108;  // C8

    // Visual options
    bool showWaterfall = false;
    uint32_t colorOn  = 0xFF44AAFF;  // RGBA active key colour
    uint32_t colorOff = 0xFF222222;  // RGBA resting key colour

    // Engine subscription handle (for cleanup)
    uint32_t midiHandle = 0;

    // OBS source dimensions (set by video_render)
    uint32_t cx = 1280;
    uint32_t cy = 200;
};

// ─────────────────────────────────────────────────────────────────────────────
// OBS source callbacks
// ─────────────────────────────────────────────────────────────────────────────
static const char *piano_get_name(void *)
{
    return obs_module_text("PianoSource.Name");
}

static void *piano_create(obs_data_t *settings, obs_source_t *source)
{
    (void)source;
    auto *s = new PianoSource{};
    s->keyMin       = (int)obs_data_get_int(settings, "key_min");
    s->keyMax       = (int)obs_data_get_int(settings, "key_max");
    s->showWaterfall = obs_data_get_bool(settings, "waterfall");
    s->colorOn      = (uint32_t)obs_data_get_int(settings, "color_on");
    s->colorOff     = (uint32_t)obs_data_get_int(settings, "color_off");

    s->midiHandle = MidiEngine::instance().subscribe([s](const MidiEvent &ev) {
        if (ev.type == MidiEventType::NoteOn) {
            s->noteVelocity[ev.param1] = ev.param2;
            s->noteDecay[ev.param1]    = 1.0f;
        } else if (ev.type == MidiEventType::NoteOff) {
            s->noteVelocity[ev.param1] = 0;
            // decay continues naturally in video_tick
        }
    });

    MIDI_LOG_INFO("Piano source created (range %d–%d)", s->keyMin, s->keyMax);
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
    obs_data_set_default_int(settings,  "key_min",   21);
    obs_data_set_default_int(settings,  "key_max",   108);
    obs_data_set_default_bool(settings, "waterfall", false);
    obs_data_set_default_int(settings,  "color_on",  0xFF44AAFF);
    obs_data_set_default_int(settings,  "color_off", 0xFF222222);
}

static obs_properties_t *piano_properties(void *)
{
    obs_properties_t *props = obs_properties_create();
    obs_properties_add_int(props, "key_min",   "Lowest key (MIDI #)",  0,  127, 1);
    obs_properties_add_int(props, "key_max",   "Highest key (MIDI #)", 0,  127, 1);
    obs_properties_add_bool(props, "waterfall", "Falling note waterfall");
    obs_properties_add_color(props, "color_on",  "Active key colour");
    obs_properties_add_color(props, "color_off", "Resting key colour");
    return props;
}

static void piano_update(void *data, obs_data_t *settings)
{
    auto *s = static_cast<PianoSource *>(data);
    s->keyMin        = (int)obs_data_get_int(settings, "key_min");
    s->keyMax        = (int)obs_data_get_int(settings, "key_max");
    s->showWaterfall = obs_data_get_bool(settings, "waterfall");
    s->colorOn       = (uint32_t)obs_data_get_int(settings, "color_on");
    s->colorOff      = (uint32_t)obs_data_get_int(settings, "color_off");
}

static void piano_tick(void *data, float seconds)
{
    auto *s = static_cast<PianoSource *>(data);

    // Pump the MIDI queue once per frame
    MidiEngine::instance().drainQueue();

    // Decay released notes
    constexpr float kDecayRate = 3.0f; // full decay in ~1/3 second
    for (int i = 0; i < 128; ++i) {
        if (s->noteVelocity[i] == 0 && s->noteDecay[i] > 0.0f)
            s->noteDecay[i] = std::max(0.0f, s->noteDecay[i] - kDecayRate * seconds);
    }
}

static uint32_t piano_get_width(void  *data) { return static_cast<PianoSource*>(data)->cx; }
static uint32_t piano_get_height(void *data) { return static_cast<PianoSource*>(data)->cy; }

static void piano_render(void *data, gs_effect_t *effect)
{
    // TODO (Phase 3): replace this placeholder with the full piano renderer.
    // The renderer will:
    //   1. Calculate white/black key rects across [keyMin, keyMax]
    //   2. For each active note, lerp colour by velocity + decay
    //   3. If showWaterfall, draw a scrolling note history strip above the keys
    (void)effect;
    auto *s = static_cast<PianoSource *>(data);

    // Placeholder: draw a solid bar that changes colour when any note is on
    bool anyOn = false;
    for (int i = s->keyMin; i <= s->keyMax; ++i)
        if (s->noteVelocity[i] > 0) { anyOn = true; break; }

    gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
    struct vec4 col;
    uint32_t c = anyOn ? s->colorOn : s->colorOff;
    vec4_set(&col,
        ((c >> 16) & 0xFF) / 255.0f,
        ((c >>  8) & 0xFF) / 255.0f,
        ( c        & 0xFF) / 255.0f,
        ((c >> 24) & 0xFF) / 255.0f);
    gs_effect_set_vec4(color, &col);

    while (gs_effect_loop(solid, "Solid"))
        gs_draw_sprite(nullptr, 0, s->cx, s->cy);
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
