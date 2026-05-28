// waveform-source.cpp — Real-time stereo rolling waveform display for OBS Studio
//
// Audio source strategy — "intercept on the bus to the soundcard":
//   Uses audio_output_connect() to tap OBS's own primary output mix (mix 0).
//   This captures exactly what is going to the encoder/stream — no external
//   audio library, no device picker, no ASIO/WASAPI/CoreAudio code required.
//   Works with any audio that OBS is mixing: Rekordbox routed via virtual cable,
//   Ableton via Voicemeeter/BlackHole, Desktop Audio capture, microphones, etc.
//
// Display:
//   Rolling waveform (right = "now", scrolling left). Time window configurable
//   0.5 – 8 seconds (default 2 s).  Both channels visible:
//     Stereo mode  — L channel fills upward from centre, R fills downward.
//     Mono mode    — L+R averaged, mirrored symmetrically above and below.
//   Bars are colour-graded: green (quiet) → amber (moderate) → red (loud).
//
// Thread model:
//   audio_output_connect() callback runs on OBS's audio thread.  It writes
//   PCM samples into a power-of-2 ring buffer using an atomic write cursor;
//   no locking needed for the typical single-producer / single-consumer case.
//   video_tick() (video thread) reads the ring buffer and populates a small
//   per-column display buffer.  video_render() reads only the display buffer.
//
// Properties:
//   canvas_w / canvas_h   Source dimensions   (default 1280 × 200)
//   time_window           Seconds of history  (0.5 – 8.0, default 2.0)
//   display_mode          0 = Stereo, 1 = Mono
//   color_low             Amplitude 0 %  gradient stop  (default green)
//   color_mid             Amplitude 60 % gradient stop  (default amber)
//   color_high            Amplitude 100 % gradient stop (default red)
//   color_bg              Background colour              (default near-black)
//   show_center           Draw faint horizontal centre line

#include "waveform-source.hpp"
#include "../plugin-support.h"
#include <obs-module.h>
#include <media-io/audio-io.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Ring-buffer geometry
// 2^19 = 524 288 samples ≈ 10.9 s at 48 000 Hz — well above the 8 s max window.
// Power-of-2 lets us index with a cheap bitmask instead of modulo.
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int    kWfRingBits = 19;
static constexpr size_t kWfRingSize = 1ull << kWfRingBits;
static constexpr size_t kWfRingMask = kWfRingSize - 1ull;

// Maximum columns in the per-column display buffer (capped regardless of canvas width).
static constexpr int kWfMaxDispW = 1920;

// ─────────────────────────────────────────────────────────────────────────────
// Per-instance state
// ─────────────────────────────────────────────────────────────────────────────
struct WaveformSource {
    // Audio ring buffer — audio thread writes, video thread reads.
    std::vector<float>  ringL, ringR;   // kWfRingSize elements each
    std::atomic<size_t> writePos{0};
    uint32_t            sampleRate = 48000;

    // Per-column display values updated by video_tick().
    float dispL[kWfMaxDispW] = {};  // peak |amplitude| per column, left channel
    float dispR[kWfMaxDispW] = {};  // right channel

    // Properties
    uint32_t cx          = 1280;
    uint32_t cy          = 200;
    float    timeWindow  = 2.0f;    // seconds
    int      displayMode = 0;       // 0 = stereo, 1 = mono
    uint32_t colorLow    = 0xFF00CC44;  // green
    uint32_t colorMid    = 0xFFDD8800;  // amber
    uint32_t colorHigh   = 0xFFFF2200;  // red
    uint32_t colorBg     = 0xFF0A0A0A;  // near-black
    bool     showCenter  = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// Colour helpers
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t wf_lerp_color(uint32_t a, uint32_t b, float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    auto l8 = [t](uint32_t ca, uint32_t cb) -> uint32_t {
        return (uint32_t)((float)ca + ((float)cb - (float)ca) * t);
    };
    return (l8((a>>24)&0xFF,(b>>24)&0xFF)<<24)
         | (l8((a>>16)&0xFF,(b>>16)&0xFF)<<16)
         | (l8((a>> 8)&0xFF,(b>> 8)&0xFF)<< 8)
         |  l8( a     &0xFF, b     &0xFF);
}

static uint32_t wf_amp_color(const WaveformSource *s, float amp)
{
    // Continuous gradient: green (0 %) → amber (60 %) → red (100 %)
    if (amp < 0.6f)
        return wf_lerp_color(s->colorLow, s->colorMid, amp / 0.6f);
    else
        return wf_lerp_color(s->colorMid, s->colorHigh, (amp - 0.6f) / 0.4f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Audio callback — OBS audio thread
//
// OBS supplies PCM as AUDIO_FORMAT_FLOAT_PLANAR (one float[] per channel).
// We write left into ringL and right into ringR using an atomic cursor so the
// video thread can read without holding a lock.  Single-producer (audio thread)
// / single-reader (video thread) means the atomic store/load is sufficient.
// ─────────────────────────────────────────────────────────────────────────────
static void wf_audio_cb(void *param, size_t /*mix_idx*/, struct audio_data *data)
{
    auto *s = static_cast<WaveformSource *>(param);
    if (!data || !data->frames || !data->data[0]) return;

    const float *L = reinterpret_cast<const float *>(data->data[0]);
    const float *R = data->data[1]
        ? reinterpret_cast<const float *>(data->data[1]) : L;

    // Relaxed load is safe: we only need the value we stored ourselves last time.
    size_t wp = s->writePos.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < data->frames; ++i) {
        s->ringL[(wp + i) & kWfRingMask] = L[i];
        s->ringR[(wp + i) & kWfRingMask] = R[i];
    }
    // Release store makes the new samples visible to the acquire load in tick().
    s->writePos.store(wp + data->frames, std::memory_order_release);
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────
static void wf_load_settings(WaveformSource *s, obs_data_t *settings)
{
    s->cx          = (uint32_t)std::max((long long)80,  obs_data_get_int(settings,    "canvas_w"));
    s->cy          = (uint32_t)std::max((long long)40,  obs_data_get_int(settings,    "canvas_h"));
    s->timeWindow  = (float)std::max(0.5,  std::min(8.0, obs_data_get_double(settings,"time_window")));
    s->displayMode = (int)obs_data_get_int(settings,    "display_mode");
    s->colorLow    = (uint32_t)obs_data_get_int(settings,"color_low");
    s->colorMid    = (uint32_t)obs_data_get_int(settings,"color_mid");
    s->colorHigh   = (uint32_t)obs_data_get_int(settings,"color_high");
    s->colorBg     = (uint32_t)obs_data_get_int(settings,"color_bg");
    s->showCenter  = obs_data_get_bool(settings,         "show_center");
}

static void wf_defaults(obs_data_t *settings)
{
    obs_data_set_default_int(   settings, "canvas_w",     1280);
    obs_data_set_default_int(   settings, "canvas_h",     200);
    obs_data_set_default_double(settings, "time_window",  2.0);
    obs_data_set_default_int(   settings, "display_mode", 0);
    obs_data_set_default_int(   settings, "color_low",    0xFF00CC44);
    obs_data_set_default_int(   settings, "color_mid",    0xFFDD8800);
    obs_data_set_default_int(   settings, "color_high",   0xFFFF2200);
    obs_data_set_default_int(   settings, "color_bg",     0xFF0A0A0A);
    obs_data_set_default_bool(  settings, "show_center",  true);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
static const char *wf_get_name(void *) { return obs_module_text("WaveformSource.Name"); }

static void *wf_create(obs_data_t *settings, obs_source_t *source)
{
    (void)source;
    auto *s = new WaveformSource{};
    s->ringL.assign(kWfRingSize, 0.0f);
    s->ringR.assign(kWfRingSize, 0.0f);
    wf_load_settings(s, settings);

    // Query the native OBS audio sample rate to accurately map time → samples.
    audio_t *oa = obs_get_audio();
    if (oa) {
        const struct audio_output_info *ai = audio_output_get_info(oa);
        if (ai && ai->samples_per_sec > 0)
            s->sampleRate = ai->samples_per_sec;
    }

    // Tap OBS primary output mix (mix index 0).
    // The convert_info requests float-planar stereo at the native sample rate
    // so OBS does not need to resample before calling our callback.
    if (oa) {
        struct audio_convert_info aci = {};
        aci.samples_per_sec = s->sampleRate;
        aci.format          = AUDIO_FORMAT_FLOAT_PLANAR;
        aci.speakers        = SPEAKERS_STEREO;
        audio_output_connect(oa, 0, &aci, wf_audio_cb, s);
    }

    return s;
}

static void wf_destroy(void *data)
{
    auto *s = static_cast<WaveformSource *>(data);
    audio_t *oa = obs_get_audio();
    if (oa) audio_output_disconnect(oa, 0, wf_audio_cb, s);
    delete s;
}

static void wf_update(void *data, obs_data_t *settings)
{
    wf_load_settings(static_cast<WaveformSource *>(data), settings);
}

// ─────────────────────────────────────────────────────────────────────────────
// video_tick — rebuild per-column peak values each video frame
//
// For each pixel column x we find the range of ring-buffer samples that map to
// that column and take the peak absolute value (max-absolute downsampling).
// This preserves transients while keeping the computation O(totalSamples) per
// frame regardless of canvas width.
// ─────────────────────────────────────────────────────────────────────────────
static void wf_tick(void *data, float /*seconds*/)
{
    auto *s = static_cast<WaveformSource *>(data);

    const int    W     = std::min((int)s->cx, kWfMaxDispW);
    const size_t nSam  = (size_t)(s->timeWindow * (float)s->sampleRate);
    // Acquire load: sees all ring-buffer writes that preceded the release store.
    const size_t wp    = s->writePos.load(std::memory_order_acquire);
    // rp = first (oldest) sample in the display window.
    const size_t rp    = (wp >= nSam) ? (wp - nSam) : 0;
    const float  spx   = (float)nSam / (float)W;  // samples per pixel column

    for (int x = 0; x < W; ++x) {
        const size_t s0 = rp + (size_t)((float)x       * spx);
        const size_t s1 = rp + (size_t)((float)(x + 1) * spx);
        const size_t sEnd = (s1 > s0) ? s1 : s0 + 1;

        float pkL = 0.0f, pkR = 0.0f;
        for (size_t si = s0; si < sEnd; ++si) {
            const float sL = s->ringL[si & kWfRingMask];
            const float sR = s->ringR[si & kWfRingMask];
            const float aL = sL < 0.0f ? -sL : sL;
            const float aR = sR < 0.0f ? -sR : sR;
            if (aL > pkL) pkL = aL;
            if (aR > pkR) pkR = aR;
        }
        s->dispL[x] = pkL < 1.0f ? pkL : 1.0f;
        s->dispR[x] = pkR < 1.0f ? pkR : 1.0f;
    }
}

static uint32_t wf_get_width (void *d) { return static_cast<WaveformSource*>(d)->cx; }
static uint32_t wf_get_height(void *d) { return static_cast<WaveformSource*>(d)->cy; }

// ─────────────────────────────────────────────────────────────────────────────
// Render
//
// Canvas layout (H = canvas height, W = canvas width):
//
//   Stereo mode:
//     ┌─────────────────────────────────────────────────────────────┐ y=0
//     │  Left channel  ↑  (bars grow upward from centre)           │
//     ├─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ centre line  ─ ─ ─ ─ ─ ─ ─ ─ ─ ─│ y=H/2
//     │  Right channel ↓  (bars grow downward from centre)         │
//     └─────────────────────────────────────────────────────────────┘ y=H
//
//   Mono mode: L+R averaged, reflected symmetrically above and below.
//
// One drawRect call per column per channel — 1280 columns × 2 channels = 2560
// rectangles at default settings; well within OBS GPU budget.
//
// Uses gs_technique_begin/end — never gs_effect_loop.
// ─────────────────────────────────────────────────────────────────────────────
static void wf_render(void *data, gs_effect_t *effect)
{
    (void)effect;
    auto *s = static_cast<WaveformSource *>(data);

    const float W  = (float)s->cx;
    const float H  = (float)s->cy;
    const float cY = H * 0.5f;

    gs_effect_t    *solid  = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t    *cp     = gs_effect_get_param_by_name(solid, "color");
    gs_technique_t *tech   = gs_effect_get_technique(solid, "Solid");
    size_t          passes = gs_technique_begin(tech);

    auto setColor = [&](uint32_t argb) {
        struct vec4 c4;
        vec4_set(&c4,
            ((argb >> 16) & 0xFF) / 255.0f,
            ((argb >>  8) & 0xFF) / 255.0f,
            ( argb        & 0xFF) / 255.0f,
            ((argb >> 24) & 0xFF) / 255.0f);
        gs_effect_set_vec4(cp, &c4);
    };

    auto drawRect = [&](float x, float y, float w, float h, uint32_t argb) {
        if (w < 0.5f || h < 0.5f) return;
        setColor(argb);
        gs_matrix_push();
        gs_matrix_translate3f(x, y, 0.0f);
        gs_draw_sprite(nullptr, 0,
            (uint32_t)(w < 1.0f ? 1 : (uint32_t)w),
            (uint32_t)(h < 1.0f ? 1 : (uint32_t)h));
        gs_matrix_pop();
    };

    const int   W_i = std::min((int)s->cx, kWfMaxDispW);
    const float pW  = W / (float)W_i;  // pixel width per display column

    for (size_t p = 0; p < passes; ++p) {
        gs_technique_begin_pass(tech, p);

        // Background
        drawRect(0.0f, 0.0f, W, H, s->colorBg);

        // Faint centre line
        if (s->showCenter)
            drawRect(0.0f, cY - 0.5f, W, 1.0f,
                     wf_lerp_color(s->colorBg, 0xFF808080, 0.28f));

        // Waveform columns
        for (int x = 0; x < W_i; ++x) {
            const float px = (float)x * pW;

            if (s->displayMode == 1) {
                // ── Mono: average L+R, mirror above and below centre ──────────
                const float amp = std::min(1.0f, (s->dispL[x] + s->dispR[x]) * 0.5f);
                const float bH  = amp * cY;
                if (bH >= 0.5f) {
                    const uint32_t col = wf_amp_color(s, amp);
                    drawRect(px, cY - bH, pW, bH, col);  // up
                    drawRect(px, cY,      pW, bH, col);  // down (mirror)
                }
            } else {
                // ── Stereo: L up from centre, R down from centre ──────────────
                const float ampL = s->dispL[x];
                const float ampR = s->dispR[x];
                const float bHL  = ampL * cY;
                const float bHR  = ampR * cY;

                if (bHL >= 0.5f)
                    drawRect(px, cY - bHL, pW, bHL, wf_amp_color(s, ampL));
                if (bHR >= 0.5f)
                    drawRect(px, cY,       pW, bHR, wf_amp_color(s, ampR));
            }
        }

        gs_technique_end_pass(tech);
    }
    gs_technique_end(tech);
}

// ─────────────────────────────────────────────────────────────────────────────
// Properties panel
// ─────────────────────────────────────────────────────────────────────────────
static obs_properties_t *wf_get_properties(void *)
{
    obs_properties_t *props = obs_properties_create();

    obs_properties_add_int(props, "canvas_w", "Canvas Width",  80, 3840, 1);
    obs_properties_add_int(props, "canvas_h", "Canvas Height", 40, 2160, 1);

    obs_properties_add_float_slider(props, "time_window",
        "Time Window (seconds)", 0.5, 8.0, 0.5);

    obs_property_t *mode = obs_properties_add_list(props, "display_mode",
        "Display Mode", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(mode, "Stereo (L above / R below centre)", 0);
    obs_property_list_add_int(mode, "Mono (L+R averaged, mirrored)",     1);

    obs_properties_add_color(props, "color_low",   "Color — Low amplitude");
    obs_properties_add_color(props, "color_mid",   "Color — Mid amplitude");
    obs_properties_add_color(props, "color_high",  "Color — High amplitude");
    obs_properties_add_color(props, "color_bg",    "Background Color");
    obs_properties_add_bool (props, "show_center", "Show centre line");

    return props;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────
void waveform_source_register(void)
{
    struct obs_source_info info = {};
    info.id             = "waveform_source";
    info.type           = OBS_SOURCE_TYPE_INPUT;
    info.output_flags   = OBS_SOURCE_VIDEO;
    info.get_name       = wf_get_name;
    info.create         = wf_create;
    info.destroy        = wf_destroy;
    info.update         = wf_update;
    info.video_tick     = wf_tick;
    info.video_render   = wf_render;
    info.get_width      = wf_get_width;
    info.get_height     = wf_get_height;
    info.get_defaults   = wf_defaults;
    info.get_properties = wf_get_properties;
    obs_register_source(&info);
}
