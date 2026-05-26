#pragma once

#include <util/bmem.h>
#include <util/platform.h>
#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

// Convenience wrappers around OBS blog() so source files don't need to
// repeat the plugin name prefix every time.
#define obs_midi_log(level, fmt, ...)  \
    blog(level, "[obs-midi-viz] " fmt, ##__VA_ARGS__)

#define MIDI_LOG_INFO(fmt, ...)  obs_midi_log(LOG_INFO,    fmt, ##__VA_ARGS__)
#define MIDI_LOG_WARN(fmt, ...)  obs_midi_log(LOG_WARNING, fmt, ##__VA_ARGS__)
#define MIDI_LOG_ERR(fmt, ...)   obs_midi_log(LOG_ERROR,   fmt, ##__VA_ARGS__)
#define MIDI_LOG_DBG(fmt, ...)   obs_midi_log(LOG_DEBUG,   fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
