#include "drum-source.hpp"
#include "../midi-engine.hpp"
#include "../plugin-support.h"
#include <obs-module.h>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
// Default GM drum note → pad label mapping (General MIDI percussion map)
// Index = MIDI note number (35–81 covers standard GM kit)
// ─────────────────────────────────────────────────────────────────────────────
static const char *GM_DRUM_NAMES[128] = {
    // 0–34 unused in GM
    [35] = "Bass2",  [36] = "Bass1",  [37] = "Rimshot", [38] = "Snare",
    [39] = "Clap",   [40] = "Snare2", [41] = "Tom1",    [42] = "HH-C",
    [43] = "Tom2",   [44] = "HH-P",   [45] = "Tom3",    [46] = "HH-O",
    [47] = "Tom4",   [48] = "Tom5",   [49] = "Crash1",  [50] = "Tom6",
    [51] = "Ride1",  [52] = "China",  [53] = "RideBell",[54] = "Tamb",
    [55] = "Splash", [56] = "Cowbell",[57] = "Crash2",  [58] = "Vibraslap",
    [59] = "Ride2",  [60] = "HiBongo",[61] = "LoBongo", [62] = "Conga1",
    [63] = "Conga2", [64] = "Conga3", [65] = "HiTimb",  [66] = "LoTimb",
    [67] = "HiAgogo",[68] = "LoAgogo",[69] = "Cabasa",  [70] = "Maracas",
    [71] = "SWhistle",[72]= "LWhistle",[73]= "ShortGui", [74]= "LongGui",
    [75] = "Claves", [76] = "HiWood", [77] = "LoWood",  [78] = "MuteTriangle",
    [79] = "OpenTri",[80] = "MuteHH", [81] = "OpenHH",
};

struct DrumSource {
    static constexpr int MAX_PADS = 64;

    // Per-pad state: which MIDI note it listens to + current flash level
    int     padNote[MAX_PADS];   // MIDI note number; -1 = unused
    float   padFlash[MAX_PADS];  // 0.0–1.0 brightness (decays each tick)

    int  columns    = 4;   // grid columns (4×4, 4×8, etc.)
    int  rows       = 4;
    bool showLabels = true;

    uint32_t colorHit  = 0xFFFF6600;
    uint32_t colorIdle = 0xFF1A1A2E;

    uint32_t midiHandle = 0;
    uint32_t cx = 400;
    uint32_t cy = 400;

    DrumSource()
    {
        // Initialise pads with GM defaults (4×4 = 16 pads starting at note 36)
        for (int i = 0; i < MAX_PADS; ++i) {
            padNote[i]  = (i < 16) ? 36 + i : -1;
            padFlash[i] = 0.0f;
        }
    }
};

static const char *drum_get_name(void *) { return obs_module_text("DrumSource.Name"); }

static void *drum_create(obs_data_t *settings, obs_source_t *source)
{
    (void)source;
    auto *s = new DrumSource{};
    s->columns    = (int)obs_data_get_int(settings, "columns");
    s->rows       = (int)obs_data_get_int(settings, "rows");
    s->showLabels = obs_data_get_bool(settings, "show_labels");
    s->colorHit   = (uint32_t)obs_data_get_int(settings, "color_hit");
    s->colorIdle  = (uint32_t)obs_data_get_int(settings, "color_idle");

    s->midiHandle = MidiEngine::instance().subscribe([s](const MidiEvent &ev) {
        if (ev.type != MidiEventType::NoteOn) return;
        for (int i = 0; i < DrumSource::MAX_PADS; ++i) {
            if (s->padNote[i] == ev.param1) {
                s->padFlash[i] = ev.param2 / 127.0f;
                break;
            }
        }
    });

    MIDI_LOG_INFO("Drum source created (%dx%d grid)", s->columns, s->rows);
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
    obs_data_set_default_int(settings,  "columns",     4);
    obs_data_set_default_int(settings,  "rows",        4);
    obs_data_set_default_bool(settings, "show_labels", true);
    obs_data_set_default_int(settings,  "color_hit",   0xFFFF6600);
    obs_data_set_default_int(settings,  "color_idle",  0xFF1A1A2E);
}

static obs_properties_t *drum_properties(void *)
{
    obs_properties_t *props = obs_properties_create();
    obs_properties_add_int(props, "columns",     "Grid columns", 1, 8, 1);
    obs_properties_add_int(props, "rows",        "Grid rows",    1, 8, 1);
    obs_properties_add_bool(props, "show_labels","Show pad labels");
    obs_properties_add_color(props, "color_hit",  "Hit colour");
    obs_properties_add_color(props, "color_idle", "Idle colour");
    return props;
}

static void drum_tick(void *data, float seconds)
{
    auto *s = static_cast<DrumSource *>(data);
    MidiEngine::instance().drainQueue();
    constexpr float kDecayRate = 5.0f; // ~200ms full decay
    for (int i = 0; i < DrumSource::MAX_PADS; ++i)
        if (s->padFlash[i] > 0.0f)
            s->padFlash[i] = std::max(0.0f, s->padFlash[i] - kDecayRate * seconds);
}

static uint32_t drum_get_width(void  *data) { return static_cast<DrumSource*>(data)->cx; }
static uint32_t drum_get_height(void *data) { return static_cast<DrumSource*>(data)->cy; }

static void drum_render(void *data, gs_effect_t *effect)
{
    // TODO (Phase 4): draw the actual grid with lit pads
    // For now: placeholder that flashes when any pad is hit
    (void)effect;
    auto *s = static_cast<DrumSource *>(data);
    float maxFlash = 0.0f;
    for (int i = 0; i < DrumSource::MAX_PADS; ++i)
        maxFlash = std::max(maxFlash, s->padFlash[i]);

    uint32_t c = maxFlash > 0.01f ? s->colorHit : s->colorIdle;
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
    info.update         = [](void *d, obs_data_t *s){ drum_destroy(d); drum_create(s, nullptr); };
    info.video_tick     = drum_tick;
    info.video_render   = drum_render;
    info.get_width      = drum_get_width;
    info.get_height     = drum_get_height;
    obs_register_source(&info);
    MIDI_LOG_INFO("Registered source: midi_drum_source");
}
