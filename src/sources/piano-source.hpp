#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Registers the "MIDI Piano Roll" source type with OBS.
// Called once from obs_module_load().
void piano_source_register(void);

#ifdef __cplusplus
}
#endif
