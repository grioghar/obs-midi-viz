#include <obs-module.h>
#include "plugin-support.h"
#include "sources/piano-source.hpp"
#include "sources/drum-source.hpp"
#include "sources/cc-source.hpp"
#include "sources/dj-source.hpp"
#include "sources/synth-source.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
    return "MIDI Visualizer — piano roll, drum pads, and CC lanes as OBS sources";
}

bool obs_module_load(void)
{
    blog(LOG_INFO, "obs-midi-viz %s loading", PLUGIN_VERSION);

    piano_source_register();
    drum_source_register();
    cc_source_register();
    dj_source_register();
    synth_source_register();

    blog(LOG_INFO, "obs-midi-viz loaded successfully");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "obs-midi-viz unloaded");
}
