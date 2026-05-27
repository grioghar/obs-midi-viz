// daw-source.cpp — Ableton Live Session View (OSC/AbletonOSC) for OBS Studio
// Phase 7: DAW Integration
//
// Displays a live grid of scenes × tracks sourced from AbletonOSC over UDP.
// Each track column shows: name, clip slots (highlighted when playing), level meter.
// Header shows BPM, beat position, and transport state.
//
// AbletonOSC setup:
//   1. Install the AbletonOSC Max for Live device into any MIDI track in Ableton.
//   2. Ensure its "Port" matches the "AbletonOSC Receive Port" setting here (default 11001).
//   3. This source listens on "OSC Listen Port" (default 11000) and sends queries to
//      "AbletonOSC Host:Port" to refresh metadata.
//
// OSC paths handled (AbletonOSC protocol):
//   /live/song/get/tempo                      → f:bpm
//   /live/song/get/is_playing                 → i:playing
//   /live/song/get/beat                       → f:beat_position
//   /live/track/get/name                      → i:track_id  s:name
//   /live/track/get/output_meter_level        → i:track_id  f:level
//   /live/scene/get/name                      → i:scene_id  s:name
//   /live/clip/get/name                       → i:track_id  i:clip_id  s:name
//   /live/clip/get/is_playing                 → i:track_id  i:clip_id  i:is_playing
//   /live/clip_slot/get/has_clip              → i:track_id  i:clip_id  i:has_clip
//   /live/song/get/num_tracks                 → i:count
//   /live/song/get/num_scenes                 → i:count

#include "daw-source.hpp"
#include "../osc-receiver.hpp"
#include "../plugin-support.h"
#include <obs-module.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

static const float kPi = 3.14159265f;

// ─────────────────────────────────────────────────────────────────────────────
// 3×5 pixel bitmap font (same table as all other obs-midi-viz sources)
// ─────────────────────────────────────────────────────────────────────────────
static const uint8_t kDawFont[38][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,6},{7,4,7,5,7},{7,1,2,4,4},{7,5,7,5,7},{7,5,7,1,7},
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},
    {7,4,6,4,4},{3,4,7,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},
    {5,5,6,5,5},{4,4,4,4,7},{5,7,5,5,5},{5,5,7,5,5},{2,5,5,5,2},
    {6,5,6,4,4},{2,5,5,7,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
    {5,5,5,5,7},{5,5,5,2,2},{5,5,7,5,5},{5,5,2,5,5},{5,5,2,2,2},
    {7,1,2,4,7},{0,0,7,0,0},{0,0,0,0,0},
};

static int dawFontIdx(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    char u = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    if (u >= 'A' && u <= 'Z') return 10 + (u - 'A');
    if (c == '-') return 36;
    return 37; // space
}

// ─────────────────────────────────────────────────────────────────────────────
// State structs
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int kMaxTracks = 8;
static constexpr int kMaxScenes = 8;

struct DawClip {
    char name[24]   = {};
    bool hasClip    = false;
    bool isPlaying  = false;
    float flashAge  = 0.0f;   // seconds since last play-start (for flash animation)
};

struct DawTrack {
    char  name[24]        = {};
    float level           = 0.0f;
    float peakLevel       = 0.0f;
    float peakAge         = 999.0f;
    bool  valid           = false;
    DawClip clips[kMaxScenes];
};

struct DawSource {
    // Transport state
    float bpm       = 120.0f;
    bool  playing   = false;
    float beat      = 0.0f;   // current beat position (fractional bar)

    DawTrack tracks[kMaxTracks];
    char     sceneNames[kMaxScenes][24];
    int      numTracks = 0;
    int      numScenes = 0;

    // AbletonOSC connection settings
    char     remoteHost[64] = "127.0.0.1";
    uint16_t remotePort  = 11001;  // AbletonOSC listens here
    uint16_t listenPort  = 11000;  // we listen here

    // Query timer — refreshes track levels at ~4 Hz; metadata on demand
    float    levelPollTimer   = 0.0f;
    float    metaPollTimer    = 999.0f; // refresh metadata on first tick

    uint32_t oscHandle   = 0;
    uint32_t cx = 720;
    uint32_t cy = 400;

    DawSource() { memset(sceneNames, 0, sizeof(sceneNames)); }
};

// ─────────────────────────────────────────────────────────────────────────────
// AbletonOSC query helpers
// ─────────────────────────────────────────────────────────────────────────────
static void daw_query(DawSource *s, const char *address,
                      const std::vector<OscArg> &args = {})
{
    OscReceiver::instance().send(s->remoteHost, s->remotePort, address, args);
}

static void daw_query_transport(DawSource *s)
{
    daw_query(s, "/live/song/get/tempo");
    daw_query(s, "/live/song/get/is_playing");
    daw_query(s, "/live/song/get/beat");
}

static void daw_query_levels(DawSource *s)
{
    for (int t = 0; t < kMaxTracks; ++t)
        daw_query(s, "/live/track/get/output_meter_level",
                  { OscArg::fromInt(t) });
}

static void daw_query_metadata(DawSource *s)
{
    daw_query(s, "/live/song/get/num_tracks");
    daw_query(s, "/live/song/get/num_scenes");
    for (int t = 0; t < kMaxTracks; ++t)
        daw_query(s, "/live/track/get/name", { OscArg::fromInt(t) });
    for (int sc = 0; sc < kMaxScenes; ++sc)
        daw_query(s, "/live/scene/get/name", { OscArg::fromInt(sc) });
    for (int t = 0; t < kMaxTracks; ++t) {
        for (int sc = 0; sc < kMaxScenes; ++sc) {
            daw_query(s, "/live/clip_slot/get/has_clip",
                      { OscArg::fromInt(t), OscArg::fromInt(sc) });
            daw_query(s, "/live/clip/get/name",
                      { OscArg::fromInt(t), OscArg::fromInt(sc) });
            daw_query(s, "/live/clip/get/is_playing",
                      { OscArg::fromInt(t), OscArg::fromInt(sc) });
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OSC message handler — maps AbletonOSC responses into DawSource state
// ─────────────────────────────────────────────────────────────────────────────
static void daw_handle_osc(DawSource *s, const OscMessage &msg)
{
    const char *addr = msg.address.c_str();

    if (strcmp(addr, "/live/song/get/tempo") == 0) {
        if (msg.has(0)) s->bpm = msg.floatAt(0, s->bpm);

    } else if (strcmp(addr, "/live/song/get/is_playing") == 0) {
        s->playing = (msg.intAt(0) != 0);

    } else if (strcmp(addr, "/live/song/get/beat") == 0) {
        s->beat = msg.floatAt(0, s->beat);

    } else if (strcmp(addr, "/live/song/get/num_tracks") == 0) {
        s->numTracks = std::max(0, std::min(kMaxTracks, msg.intAt(0)));

    } else if (strcmp(addr, "/live/song/get/num_scenes") == 0) {
        s->numScenes = std::max(0, std::min(kMaxScenes, msg.intAt(0)));

    } else if (strcmp(addr, "/live/track/get/name") == 0) {
        int ti = msg.intAt(0);
        if (ti >= 0 && ti < kMaxTracks) {
            snprintf(s->tracks[ti].name, sizeof(s->tracks[ti].name),
                     "%s", msg.strAt(1));
            s->tracks[ti].valid = true;
            if (ti + 1 > s->numTracks) s->numTracks = ti + 1;
        }

    } else if (strcmp(addr, "/live/track/get/output_meter_level") == 0) {
        int ti = msg.intAt(0);
        if (ti >= 0 && ti < kMaxTracks) {
            float lv = msg.floatAt(1, 0.0f);
            auto &tr = s->tracks[ti];
            tr.level = lv;
            if (lv >= tr.peakLevel) { tr.peakLevel = lv; tr.peakAge = 0.0f; }
        }

    } else if (strcmp(addr, "/live/scene/get/name") == 0) {
        int si = msg.intAt(0);
        if (si >= 0 && si < kMaxScenes) {
            snprintf(s->sceneNames[si], sizeof(s->sceneNames[si]),
                     "%s", msg.strAt(1));
            if (si + 1 > s->numScenes) s->numScenes = si + 1;
        }

    } else if (strcmp(addr, "/live/clip_slot/get/has_clip") == 0) {
        int ti = msg.intAt(0), si = msg.intAt(1);
        if (ti>=0&&ti<kMaxTracks && si>=0&&si<kMaxScenes)
            s->tracks[ti].clips[si].hasClip = (msg.intAt(2) != 0);

    } else if (strcmp(addr, "/live/clip/get/name") == 0) {
        int ti = msg.intAt(0), si = msg.intAt(1);
        if (ti>=0&&ti<kMaxTracks && si>=0&&si<kMaxScenes) {
            snprintf(s->tracks[ti].clips[si].name,
                     sizeof(s->tracks[ti].clips[si].name), "%s", msg.strAt(2));
        }

    } else if (strcmp(addr, "/live/clip/get/is_playing") == 0) {
        int ti = msg.intAt(0), si = msg.intAt(1);
        if (ti>=0&&ti<kMaxTracks && si>=0&&si<kMaxScenes) {
            bool wasPlaying = s->tracks[ti].clips[si].isPlaying;
            bool nowPlaying = (msg.intAt(2) != 0);
            if (nowPlaying && !wasPlaying)
                s->tracks[ti].clips[si].flashAge = 0.0f;
            s->tracks[ti].clips[si].isPlaying = nowPlaying;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────
static void daw_load_settings(DawSource *s, obs_data_t *settings)
{
    snprintf(s->remoteHost, sizeof(s->remoteHost), "%s",
             obs_data_get_string(settings, "remote_host"));
    s->remotePort  = (uint16_t)obs_data_get_int(settings, "remote_port");
    s->listenPort  = (uint16_t)obs_data_get_int(settings, "listen_port");
    s->cx          = (uint32_t)std::max(200LL, obs_data_get_int(settings, "canvas_w"));
    s->cy          = (uint32_t)std::max(100LL, obs_data_get_int(settings, "canvas_h"));
}

static void daw_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, "remote_host", "127.0.0.1");
    obs_data_set_default_int   (settings, "remote_port", 11001);
    obs_data_set_default_int   (settings, "listen_port", 11000);
    obs_data_set_default_int   (settings, "canvas_w",   720);
    obs_data_set_default_int   (settings, "canvas_h",   400);
}

// ─────────────────────────────────────────────────────────────────────────────
// OBS lifecycle
// ─────────────────────────────────────────────────────────────────────────────
static const char *daw_get_name(void *) { return obs_module_text("DawSource.Name"); }

static void *daw_create(obs_data_t *settings, obs_source_t *src)
{
    (void)src;
    auto *s = new DawSource{};
    daw_load_settings(s, settings);

    OscReceiver::instance().start(s->listenPort);

    s->oscHandle = OscReceiver::instance().subscribe([s](const OscMessage &msg) {
        daw_handle_osc(s, msg);
    });

    // Seed default track and scene names while waiting for AbletonOSC
    for (int t = 0; t < kMaxTracks; ++t)
        snprintf(s->tracks[t].name, sizeof(s->tracks[t].name), "TRK %d", t+1);
    for (int sc = 0; sc < kMaxScenes; ++sc)
        snprintf(s->sceneNames[sc], sizeof(s->sceneNames[sc]), "Sc %d", sc+1);
    s->numTracks = kMaxTracks;
    s->numScenes = kMaxScenes;

    return s;
}

static void daw_destroy(void *data)
{
    auto *s = static_cast<DawSource *>(data);
    OscReceiver::instance().unsubscribe(s->oscHandle);
    delete s;
}

static void daw_update(void *data, obs_data_t *settings)
{
    auto *s = static_cast<DawSource *>(data);
    uint16_t prevListen = s->listenPort;
    daw_load_settings(s, settings);
    if (s->listenPort != prevListen) {
        OscReceiver::instance().stop();
        OscReceiver::instance().start(s->listenPort);
    }
    s->metaPollTimer = 999.0f; // trigger metadata refresh
}

static void daw_tick(void *data, float seconds)
{
    auto *s = static_cast<DawSource *>(data);
    OscReceiver::instance().drainQueue();

    // Animate clip flash and peak decay
    constexpr float kFlashDecay = 3.0f, kPeakHold = 1.5f, kPeakDecay = 0.6f;
    for (int t = 0; t < kMaxTracks; ++t) {
        auto &tr = s->tracks[t];
        tr.peakAge += seconds;
        if (tr.peakAge > kPeakHold)
            tr.peakLevel = std::max(0.0f, tr.peakLevel - kPeakDecay * seconds);
        for (int sc = 0; sc < kMaxScenes; ++sc) {
            if (tr.clips[sc].flashAge < 1.0f)
                tr.clips[sc].flashAge = std::min(1.0f, tr.clips[sc].flashAge + kFlashDecay * seconds);
        }
    }

    // Level polling ~4 Hz
    s->levelPollTimer += seconds;
    if (s->levelPollTimer >= 0.25f) {
        s->levelPollTimer = 0.0f;
        daw_query_transport(s);
        daw_query_levels(s);
    }

    // Metadata polling on first tick then every 30 s
    s->metaPollTimer += seconds;
    if (s->metaPollTimer >= 30.0f) {
        s->metaPollTimer = 0.0f;
        daw_query_metadata(s);
    }
}

static uint32_t daw_get_width (void *data) { return static_cast<DawSource*>(data)->cx; }
static uint32_t daw_get_height(void *data) { return static_cast<DawSource*>(data)->cy; }

// ─────────────────────────────────────────────────────────────────────────────
// Render — Ableton Session View
//
// Canvas layout (720×400 default):
//
//   ┌─────────────────────────────────────────────────────────────────────┐
//   │ HEADER  ▶ ABLETON LIVE  |  ♩ 128.0 BPM  |  Bar 1.3  |  :11000   │  h=36
//   ├────────┬────────────────────────────────────────────────────────────┤
//   │ SCENE  │  TRK 1  │  TRK 2  │  TRK 3  │  TRK 4  │  ...           │  h=24  (track names)
//   │ NAMES  ├─────────┴─────────┴─────────┴─────────┴───────────────────┤
//   │        │[clip]   │         │[clip ▶] │         │  ...              │  }
//   │  Sc 1  ├─────────┴─────────┴─────────┴─────────┴───────────────────┤  } kMaxScenes rows
//   │  Sc 2  │         │[clip]   │         │[clip]   │  ...              │  }
//   │  ...   │ ...                                                        │  } h=(H-36-24-40)/8 each
//   ├────────┴────────────────────────────────────────────────────────────┤
//   │ LEVEL METERS  (8 vertical bars, one per track)                     │  h=40
//   └─────────────────────────────────────────────────────────────────────┘
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t lerp_argb_daw(uint32_t a, uint32_t b, float t)
{
    auto l = [t](uint32_t ca, uint32_t cb) -> uint32_t {
        return (uint32_t)((float)ca + ((float)cb - (float)ca) * t);
    };
    return (l((a>>24)&0xFF,(b>>24)&0xFF)<<24)
         | (l((a>>16)&0xFF,(b>>16)&0xFF)<<16)
         | (l((a>> 8)&0xFF,(b>> 8)&0xFF)<< 8)
         |  l( a     &0xFF, b     &0xFF);
}

static void daw_render(void *data, gs_effect_t *effect)
{
    (void)effect;
    auto *s = static_cast<DawSource *>(data);

    const float W  = (float)s->cx;
    const float H  = (float)s->cy;
    const int   NT = std::max(1, std::min(s->numTracks, kMaxTracks));
    const int   NS = std::max(1, std::min(s->numScenes, kMaxScenes));

    gs_effect_t    *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t    *cp    = gs_effect_get_param_by_name(solid, "color");
    gs_technique_t *tech  = gs_effect_get_technique(solid, "Solid");
    size_t          passes= gs_technique_begin(tech);

    auto drawRect = [&](float x, float y, float w, float h, uint32_t argb) {
        if (w < 1.0f || h < 1.0f) return;
        struct vec4 c4;
        vec4_set(&c4,
            ((argb>>16)&0xFF)/255.0f, ((argb>>8)&0xFF)/255.0f,
            (argb&0xFF)/255.0f,       ((argb>>24)&0xFF)/255.0f);
        gs_effect_set_vec4(cp, &c4);
        gs_matrix_push();
        gs_matrix_translate3f(x, y, 0.0f);
        gs_draw_sprite(nullptr, 0, (uint32_t)w, (uint32_t)h);
        gs_matrix_pop();
    };

    auto drawText = [&](float x, float y, float sc, uint32_t col, const char *text) {
        if (!text || !text[0]) return;
        int len = (int)strlen(text);
        for (int ci = 0; ci < len; ++ci) {
            int fi = dawFontIdx(text[ci]);
            float cx2 = x + (float)ci * 4.0f * sc;
            for (int row = 0; row < 5; ++row) {
                uint8_t bits = kDawFont[fi][row];
                for (int bit = 0; bit < 3; ++bit) {
                    if (bits & (1u << (2 - bit)))
                        drawRect(cx2 + (float)bit * sc, y + (float)row * sc, sc, sc, col);
                }
            }
        }
    };

    auto lerp = [](uint32_t a, uint32_t b, float t) -> uint32_t {
        return lerp_argb_daw(a, b, t);
    };

    // Ableton accent: orange-ish amber (#FF8800) for transport, green (#00CC44) for playing
    const uint32_t kAccent  = 0xFFFF8800;  // Ableton orange
    const uint32_t kPlaying = 0xFF00CC44;  // playing clip green
    const uint32_t kBg      = 0xFF0A0A0F;
    const uint32_t kPanel   = 0xFF141418;
    const uint32_t kSep     = 0xFF282830;

    // Layout constants
    const float HEADER_H   = 36.0f;
    const float TRKNAME_H  = 22.0f;
    const float METER_H    = 42.0f;
    const float SCENE_W    = 72.0f;
    const float GRID_Y     = HEADER_H + TRKNAME_H;
    const float GRID_H     = H - GRID_Y - METER_H;
    const float TRACK_W    = (W - SCENE_W) / (float)NT;
    const float CLIP_H     = GRID_H / (float)NS;
    const float METER_Y    = H - METER_H;

    for (size_t pass = 0; pass < passes; ++pass) {
        gs_technique_begin_pass(tech, pass);

        // ── Background ───────────────────────────────────────────────────────
        drawRect(0, 0, W, H, kBg);

        // ── Header ───────────────────────────────────────────────────────────
        drawRect(0, 0, W, HEADER_H, lerp(kBg, kAccent, 0.12f));
        drawRect(0, HEADER_H - 1.5f, W, 1.5f, kAccent);

        // Transport state indicator
        {
            const char *sym = s->playing ? "PLAY" : "STOP";
            uint32_t sc2    = s->playing ? kPlaying : 0xFF444444;
            drawText(8.0f, 14.0f, 2.0f, sc2, sym);
        }
        // BPM
        {
            char bpm[16]; snprintf(bpm, sizeof(bpm), "%.1f BPM", s->bpm);
            drawText(70.0f, 14.0f, 2.0f, kAccent, bpm);
        }
        // Beat position
        {
            int bar  = (int)(s->beat / 4.0f) + 1;
            int beat = (int)(fmodf(s->beat, 4.0f)) + 1;
            char bp[16]; snprintf(bp, sizeof(bp), "BAR %d.%d", bar, beat);
            drawText(220.0f, 14.0f, 2.0f, 0xFF888888, bp);
        }
        // Port indicator
        {
            char pt[16]; snprintf(pt, sizeof(pt), ":%d", (int)s->listenPort);
            size_t pl = strlen(pt);
            drawText(W - (float)pl * 4.0f - 8.0f, 14.0f, 1.0f,
                     OscReceiver::instance().isRunning() ? 0xFF44AA44 : 0xFF553333, pt);
        }

        // ── Scene name column ────────────────────────────────────────────────
        drawRect(0, GRID_Y, SCENE_W, H - GRID_Y, lerp(kBg, kPanel, 0.8f));
        drawRect(SCENE_W - 1.0f, GRID_Y, 1.0f, H - GRID_Y, kSep);

        // Track name row
        drawRect(0, HEADER_H, W, TRKNAME_H, lerp(kPanel, kBg, 0.5f));
        drawRect(0, HEADER_H + TRKNAME_H - 1.0f, W, 1.0f, kSep);

        // Per-track name headers
        for (int t = 0; t < NT; ++t) {
            float tx = SCENE_W + (float)t * TRACK_W;
            drawRect(tx, HEADER_H, TRACK_W, TRKNAME_H, 0xFF0A0A0F);
            drawRect(tx + TRACK_W - 1.0f, HEADER_H, 1.0f, TRKNAME_H, kSep);
            // Track name (clipped)
            const char *tn = s->tracks[t].name;
            if (!tn[0]) { char nb[8]; snprintf(nb,sizeof(nb),"T%d",t+1); drawText(tx+3,HEADER_H+8.5f,1.0f,0xFF555555,nb); }
            else { drawText(tx + 3.0f, HEADER_H + 8.5f, 1.0f, 0xFF888888, tn); }
        }

        // ── Clip grid ────────────────────────────────────────────────────────
        for (int sc = 0; sc < NS; ++sc) {
            float sy = GRID_Y + (float)sc * CLIP_H;

            // Scene name
            {
                const char *sn = s->sceneNames[sc];
                size_t sl = strlen(sn);
                float snw = ((float)sl * 4.0f - 1.0f) * 1.0f;
                float snx = (SCENE_W - snw) * 0.5f;
                drawRect(0, sy, SCENE_W - 1.0f, CLIP_H - 1.0f,
                         lerp(kPanel, 0xFF202028, 0.5f));
                drawText(snx < 2.0f ? 2.0f : snx, sy + (CLIP_H - 5.0f) * 0.5f,
                         1.0f, 0xFF666666, sn);
            }
            // Horizontal separator
            drawRect(0, sy + CLIP_H - 1.0f, W, 1.0f, kSep);

            for (int t = 0; t < NT; ++t) {
                float tx  = SCENE_W + (float)t * TRACK_W;
                float cgap = 2.0f;
                float cx2  = tx + cgap;
                float cy2  = sy + cgap;
                float cw   = TRACK_W - cgap * 2.0f - 1.0f;
                float ch   = CLIP_H  - cgap * 2.0f;

                const DawClip &cl = s->tracks[t].clips[sc];

                if (!cl.hasClip) {
                    // Empty slot — faint trough
                    drawRect(cx2, cy2, cw, ch, lerp(kBg, kPanel, 0.3f));
                } else {
                    // Determine colour
                    uint32_t clipBase = cl.isPlaying ? kPlaying : 0xFF2A4A2A;
                    // Flash: full bright for 0.3s then settle
                    float flash = std::max(0.0f, 0.3f - cl.flashAge) / 0.3f;
                    uint32_t clipCol = lerp(clipBase, 0xFFFFFFFF, flash * 0.5f);

                    drawRect(cx2, cy2, cw, ch, clipCol);

                    // Playing indicator: bright left edge stripe
                    if (cl.isPlaying)
                        drawRect(cx2, cy2, 3.0f, ch, 0xFFFFFFFF);

                    // Clip name (tiny text inside)
                    if (ch >= 8.0f && cw >= 12.0f) {
                        uint32_t tc = cl.isPlaying ? 0xFFFFFFFF : 0xFF88AA88;
                        drawText(cx2 + (cl.isPlaying ? 6.0f : 2.0f),
                                 cy2 + (ch - 5.0f) * 0.5f,
                                 1.0f, tc, cl.name);
                    }
                }

                // Vertical track separator
                drawRect(tx + TRACK_W - 1.0f, sy, 1.0f, CLIP_H, kSep);
            }
        }

        // ── Level meters ─────────────────────────────────────────────────────
        drawRect(0, METER_Y, W, METER_H, lerp(kBg, kPanel, 0.6f));
        drawRect(0, METER_Y, W, 1.0f, kSep);

        for (int t = 0; t < NT; ++t) {
            float mx  = SCENE_W + (float)t * TRACK_W + 2.0f;
            float mw  = TRACK_W - 5.0f;
            float my  = METER_Y + 4.0f;
            float mh  = METER_H - 8.0f;
            const auto &tr = s->tracks[t];

            // Trough
            drawRect(mx, my, mw, mh, lerp(kBg, kPanel, 0.3f));

            // Level bar (vertical, fills from bottom)
            float lv  = std::max(0.0f, std::min(1.0f, tr.level));
            float lh  = lv * mh;
            if (lh >= 1.0f) {
                uint32_t lc = (lv > 0.88f) ? 0xFFFF3300 :
                              (lv > 0.70f) ? 0xFFFF8800 :
                                             kPlaying;
                drawRect(mx, my + mh - lh, mw, lh, lc);
            }

            // Peak line
            if (tr.peakLevel > 0.02f) {
                float ph = my + mh - tr.peakLevel * mh - 1.5f;
                drawRect(mx, ph, mw, 2.0f, 0xAAFFFFFF);
            }
        }

        // ── Overlay: "NOT CONNECTED" if OSC not running ───────────────────────
        if (!OscReceiver::instance().isRunning()) {
            drawRect(0, 0, W, H, 0x88000000);
            const char *nc = "OSC NOT RUNNING";
            size_t nl = strlen(nc);
            float  nw = ((float)nl * 4.0f - 1.0f) * 2.0f;
            drawText((W - nw) * 0.5f, (H - 10.0f) * 0.5f, 2.0f, 0xFFFF4444, nc);
            char hint[48];
            snprintf(hint, sizeof(hint), "CHECK PORT %d IN PROPERTIES", (int)s->listenPort);
            size_t hl = strlen(hint);
            float  hw = ((float)hl * 4.0f - 1.0f) * 1.0f;
            drawText((W - hw) * 0.5f, (H - 10.0f) * 0.5f + 18.0f, 1.0f, 0xFF888888, hint);
        }

        gs_technique_end_pass(tech);
    }
    gs_technique_end(tech);
}

// ─────────────────────────────────────────────────────────────────────────────
// Properties
// ─────────────────────────────────────────────────────────────────────────────
static obs_properties_t *daw_properties(void *data)
{
    (void)data;
    obs_properties_t *props = obs_properties_create();

    obs_properties_add_text(props, "remote_host", "AbletonOSC Host", OBS_TEXT_DEFAULT);
    obs_properties_add_int (props, "remote_port", "AbletonOSC Port (send to)", 1, 65535, 1);
    obs_properties_add_int (props, "listen_port", "OSC Listen Port (receive on)", 1, 65535, 1);

    obs_properties_add_button(props, "refresh_meta", "Refresh Track / Clip Names",
        [](obs_properties_t *, obs_property_t *, void *pd) -> bool {
            auto *s = static_cast<DawSource *>(pd);
            daw_query_metadata(s);
            return false;
        });

    obs_properties_add_int(props, "canvas_w", "Canvas width",  200, 3840, 1);
    obs_properties_add_int(props, "canvas_h", "Canvas height", 100, 2160, 1);

    return props;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────
void daw_source_register(void)
{
    static obs_source_info info{};
    info.id             = "midi_daw_source";
    info.type           = OBS_SOURCE_TYPE_INPUT;
    info.output_flags   = OBS_SOURCE_VIDEO;
    info.get_name       = daw_get_name;
    info.create         = daw_create;
    info.destroy        = daw_destroy;
    info.get_defaults   = daw_defaults;
    info.get_properties = daw_properties;
    info.update         = daw_update;
    info.video_tick     = daw_tick;
    info.video_render   = daw_render;
    info.get_width      = daw_get_width;
    info.get_height     = daw_get_height;
    obs_register_source(&info);
}
