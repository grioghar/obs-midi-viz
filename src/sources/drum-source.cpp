#include "drum-source.hpp"
#include "../midi-engine.hpp"
#include "../plugin-support.h"
#include <obs-module.h>
#include <algorithm>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
// Default GM drum note → pad label mapping (General MIDI percussion map)
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

struct DrumSource {
    static constexpr int MAX_PADS = 64;

    int     padNote[MAX_PADS];   // MIDI note number; -1 = unused
    float   padFlash[MAX_PADS];  // 0.0–1.0 brightness (decays each tick)

    int  columns    = 4;
    int  rows       = 4;
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
    auto &eng = MidiEngine::instance();
    if (portIdx < 0) {
        if (eng.isOpen()) eng.closePort();
    } else if (portIdx != eng.currentPort() || !eng.isOpen()) {
        eng.openPort(portIdx);
    }
}

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
    s->midiPort   = (int)obs_data_get_int(settings, "midi_port");

    apply_midi_port(s->midiPort);

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
    obs_data_set_default_int( settings, "midi_port",    -1);
    obs_data_set_default_int( settings, "columns",      4);
    obs_data_set_default_int( settings, "rows",         4);
    obs_data_set_default_bool(settings, "show_labels",  true);
    obs_data_set_default_int( settings, "color_hit",    0xFFFF6600);
    obs_data_set_default_int( settings, "color_idle",   0xFF1A1A2E);
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

    obs_properties_add_int( props, "columns",     "Grid columns", 1, 8, 1);
    obs_properties_add_int( props, "rows",        "Grid rows",    1, 8, 1);
    obs_properties_add_bool(props, "show_labels", "Show pad labels");
    obs_properties_add_color(props, "color_hit",  "Hit colour");
    obs_properties_add_color(props, "color_idle", "Idle colour");
    return props;
}

// Proper in-place update — do NOT destroy+recreate (the returned pointer from
// create would be discarded, leaving OBS with a dangling pointer).
static void drum_update(void *data, obs_data_t *settings)
{
    auto *s = static_cast<DrumSource *>(data);
    s->columns    = (int)obs_data_get_int(settings, "columns");
    s->rows       = (int)obs_data_get_int(settings, "rows");
    s->showLabels = obs_data_get_bool(settings, "show_labels");
    s->colorHit   = (uint32_t)obs_data_get_int(settings, "color_hit");
    s->colorIdle  = (uint32_t)obs_data_get_int(settings, "color_idle");
    s->midiPort   = (int)obs_data_get_int(settings, "midi_port");
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
// Render — placeholder solid-colour flash (Phase 4 will draw the actual grid)
// Uses gs_technique_begin/end directly; gs_effect_loop cannot be nested inside
// OBS's own compositing pass (would log "An effect is already active" at 60fps).
// ─────────────────────────────────────────────────────────────────────────────
static void drum_render(void *data, gs_effect_t *effect)
{
    (void)effect;
    auto *s = static_cast<DrumSource *>(data);

    float maxFlash = 0.0f;
    for (int i = 0; i < DrumSource::MAX_PADS; ++i)
        maxFlash = std::max(maxFlash, s->padFlash[i]);

    uint32_t c = maxFlash > 0.01f ? s->colorHit : s->colorIdle;

    gs_effect_t    *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t    *col   = gs_effect_get_param_by_name(solid, "color");
    gs_technique_t *tech  = gs_effect_get_technique(solid, "Solid");

    struct vec4 v4;
    vec4_set(&v4,
        ((c >> 16) & 0xFF) / 255.0f,
        ((c >>  8) & 0xFF) / 255.0f,
        ( c        & 0xFF) / 255.0f,
        ((c >> 24) & 0xFF) / 255.0f);
    gs_effect_set_vec4(col, &v4);

    size_t passes = gs_technique_begin(tech);
    for (size_t i = 0; i < passes; i++) {
        gs_technique_begin_pass(tech, i);
        gs_draw_sprite(nullptr, 0, s->cx, s->cy);
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
