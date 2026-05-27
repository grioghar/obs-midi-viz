#include "dj-source.hpp"
#include "../midi-engine.hpp"
#include "../plugin-support.h"
#include <obs-module.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// 3×5 pixel bitmap font (digits 0-9, A-Z, '-', space)
// Each byte is one row; bit2=left, bit1=centre, bit0=right.
// ─────────────────────────────────────────────────────────────────────────────
static const uint8_t kDjFont[38][5] = {
    {7,5,5,5,7}, // 0
    {2,6,2,2,7}, // 1
    {7,1,7,4,7}, // 2
    {7,1,7,1,7}, // 3
    {5,5,7,1,1}, // 4
    {7,4,7,1,6}, // 5
    {7,4,7,5,7}, // 6
    {7,1,2,4,4}, // 7
    {7,5,7,5,7}, // 8
    {7,5,7,1,7}, // 9
    {2,5,7,5,5}, // A
    {6,5,6,5,6}, // B
    {3,4,4,4,3}, // C
    {6,5,5,5,6}, // D
    {7,4,6,4,7}, // E
    {7,4,6,4,4}, // F
    {3,4,7,5,3}, // G
    {5,5,7,5,5}, // H
    {7,2,2,2,7}, // I
    {1,1,1,5,2}, // J
    {5,5,6,5,5}, // K
    {4,4,4,4,7}, // L
    {5,7,5,5,5}, // M
    {5,5,7,5,5}, // N
    {2,5,5,5,2}, // O
    {6,5,6,4,4}, // P
    {2,5,5,7,3}, // Q
    {6,5,6,5,5}, // R
    {3,4,2,1,6}, // S
    {7,2,2,2,2}, // T
    {5,5,5,5,7}, // U
    {5,5,5,2,2}, // V
    {5,5,7,5,5}, // W
    {5,5,2,5,5}, // X
    {5,5,2,2,2}, // Y
    {7,1,2,4,7}, // Z
    {0,0,7,0,0}, // -
    {0,0,0,0,0}, // space
};

static int djFontIdx(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    char u = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    if (u >= 'A' && u <= 'Z') return 10 + (u - 'A');
    if (c == '-') return 36;
    return 37;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pioneer DDJ-FLX4 MIDI assignments — verified against official MIDI message
// list (DDJ-FLX4_MIDI_message_List_E1.pdf) and Mixxx mapping XML.
//
// MIDI channel structure (0-indexed):
//   ch 0  (MIDI 1)  — Deck 1 transport / EQ / faders  (status 0x90/0xB0)
//   ch 1  (MIDI 2)  — Deck 2 transport / EQ / faders  (status 0x91/0xB1)
//   ch 6  (MIDI 7)  — Mixer: crossfader, filter, head  (status 0xB6)
//   ch 7  (MIDI 8)  — Pads Deck 1 press               (status 0x97)
//   ch 8  (MIDI 9)  — Pads Deck 1 +SHIFT              (status 0x98)
//   ch 9  (MIDI 10) — Pads Deck 2 press               (status 0x99)
//   ch 10 (MIDI 11) — Pads Deck 2 +SHIFT              (status 0x9A)
//
// All 14-bit (MSB+LSB) controls: we track only MSB for display (7-bit resolution)
// ─────────────────────────────────────────────────────────────────────────────

// ── Deck channel CCs (0x00–0x1F = MSB, 0x20–0x3F = matching LSB) ────────────
static constexpr int kCC_Tempo     = 0x00;  //  0 — Tempo fader MSB
static constexpr int kCC_Gain      = 0x04;  //  4 — Trim/Pregain MSB
static constexpr int kCC_EqHigh    = 0x07;  //  7 — EQ HI MSB  (centre detent)
static constexpr int kCC_EqMid     = 0x0B;  // 11 — EQ MID MSB (centre detent)
static constexpr int kCC_EqLow     = 0x0F;  // 15 — EQ LOW MSB (centre detent)
static constexpr int kCC_Volume    = 0x13;  // 19 — Channel fader MSB
// Jog wheel CCs (relative; val ≤ 0x3F = +delta, val ≥ 0x41 = –delta)
static constexpr int kCC_JogVinyl  = 0x22;  // 34 — Jog platter (vinyl/scratch mode)
static constexpr int kCC_JogPitch  = 0x23;  // 35 — Jog platter (pitch-bend mode)
static constexpr int kCC_JogSide   = 0x21;  // 33 — Jog side strip (pitch nudge)

// ── Master channel CCs (all on ch 6 / MIDI 7, status 0xB6) ─────────────────
static constexpr int kCC_FilterCh1 = 0x17;  // 23 — Quick Effect / Filter CH1
static constexpr int kCC_FilterCh2 = 0x18;  // 24 — Quick Effect / Filter CH2
static constexpr int kCC_Xfader    = 0x1F;  // 31 — Crossfader MSB

// ── Transport note numbers (on deck channels 0 / 1) ─────────────────────────
static constexpr int kNote_Play    = 0x0B;  // 11 — PLAY / PAUSE
static constexpr int kNote_Cue     = 0x0C;  // 12 — CUE (set/goto cue point)
static constexpr int kNote_Sync    = 0x58;  // 88 — BEAT SYNC
static constexpr int kNote_LoopIn  = 0x10;  // 16 — LOOP IN
static constexpr int kNote_LoopOut = 0x11;  // 17 — LOOP OUT
static constexpr int kNote_Reloop  = 0x4D;  // 77 — RELOOP / EXIT

// ── Performance pads (on pad channels; notes 0x00–0x07 in Hot Cue mode) ─────
static constexpr int kNote_HCue0   = 0x00;  //  0 — Hot Cue 1 (pads 1–8 = 0–7)

// ─────────────────────────────────────────────────────────────────────────────
// Per-deck state
// ─────────────────────────────────────────────────────────────────────────────
struct DeckState {
    float eqHigh        = 0.50f;   // centre-detent EQ knobs
    float eqMid         = 0.50f;
    float eqLow         = 0.50f;
    float gain          = 0.50f;   // Trim / Pregain
    float filter        = 0.50f;   // Quick Filter (from master ch)
    float volume        = 0.75f;   // Channel fader
    float tempo         = 0.50f;   // 0.5 = 0% pitch-shift
    float jogAngle      = 0.0f;    // degrees 0-360
    bool  playing       = false;
    bool  cueActive     = false;
    bool  syncActive    = false;
    bool  loopInActive  = false;   // LOOP IN button
    bool  loopOutActive = false;   // LOOP OUT button
    bool  reloopActive  = false;   // RELOOP / EXIT button
    int   padMode       = 0;       // 0=HotCue 1=BeatLoop 2=BeatJump 3=Sampler
    float hotCueFlash[8] = {};     // FLX4 has 8 performance pads
};

// ─────────────────────────────────────────────────────────────────────────────
// Plugin source instance
// ─────────────────────────────────────────────────────────────────────────────
struct DjSource {
    DeckState deck[2];
    float     xfader        = 0.50f;

    int       midiPort      = -1;
    // Transport / EQ / fader channels (0-indexed MIDI channels)
    int       midiChDeck0   = 0;   // Deck 1: MIDI ch 1 → ch 0 (status 0x90/0xB0)
    int       midiChDeck1   = 1;   // Deck 2: MIDI ch 2 → ch 1 (status 0x91/0xB1)
    int       midiChMaster  = 6;   // Mixer:  MIDI ch 7 → ch 6 (status 0xB6)
    // Pad channels — fixed by FLX4 hardware (not user-configurable)
    int       midiChPad0    = 7;   // Pads Deck 1 press: MIDI ch 8  (status 0x97)
    int       midiChPad1    = 9;   // Pads Deck 2 press: MIDI ch 10 (status 0x99)
    uint32_t  midiHandle    = 0;

    uint32_t  cx = 1280;
    uint32_t  cy = 480;

    // Pioneer-inspired palette
    uint32_t  colorPanel     = 0xFF111111;
    uint32_t  colorDeck      = 0xFF1E1E1E;
    uint32_t  colorAccent    = 0xFFFF8800;  // Pioneer orange
    uint32_t  colorKnobBody  = 0xFF2E2E2E;
    uint32_t  colorKnobCap   = 0xFF484848;
    uint32_t  colorKnobOff   = 0xFF3A3A3A;
    uint32_t  colorText      = 0xFFBBBBBB;
    uint32_t  colorPlay      = 0xFF00CC44;
    uint32_t  colorCue       = 0xFF0088FF;
    uint32_t  colorSync      = 0xFFFF8800;
    uint32_t  colorLoop      = 0xFF00CCFF;
    uint32_t  hotCueColors[4]= {0xFFFF2244,0xFF2266FF,0xFF22AA22,0xFFFF8800};
};

// ─────────────────────────────────────────────────────────────────────────────
// MIDI port management (open the requested port in MidiEngine)
// ─────────────────────────────────────────────────────────────────────────────
static void dj_apply_port(int portIndex)
{
    if (portIndex < 0) return;
    auto &eng = MidiEngine::instance();
    auto names = eng.portNames();
    if (portIndex < (int)names.size())
        eng.openPort(portIndex);
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings helpers
// ─────────────────────────────────────────────────────────────────────────────
static void dj_load_settings(DjSource *s, obs_data_t *settings)
{
    s->cx          = (uint32_t)std::max(320LL, obs_data_get_int(settings, "canvas_w"));
    s->cy          = (uint32_t)std::max(200LL, obs_data_get_int(settings, "canvas_h"));
    s->midiPort    = (int)obs_data_get_int(settings, "midi_port");
    s->midiChDeck0 = std::max(0, std::min(15, (int)obs_data_get_int(settings, "deck0_ch") - 1));
    s->midiChDeck1 = std::max(0, std::min(15, (int)obs_data_get_int(settings, "deck1_ch") - 1));
    s->midiChMaster= std::max(0, std::min(15, (int)obs_data_get_int(settings, "master_ch") - 1));
}

static void dj_set_defaults(obs_data_t *settings)
{
    obs_data_set_default_int(settings, "canvas_w",  1280);
    obs_data_set_default_int(settings, "canvas_h",   480);
    obs_data_set_default_int(settings, "midi_port",   -1);
    obs_data_set_default_int(settings, "deck0_ch",     1);  // MIDI ch 1
    obs_data_set_default_int(settings, "deck1_ch",     2);  // MIDI ch 2
    obs_data_set_default_int(settings, "master_ch",    7);  // MIDI ch 7 (crossfader/filter)
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
static const char *dj_get_name(void *) { return obs_module_text("DjSource.Name"); }

static void *dj_create(obs_data_t *settings, obs_source_t *source)
{
    (void)source;
    auto *s = new DjSource();
    dj_load_settings(s, settings);
    dj_apply_port(s->midiPort);

    // MIDI subscription: captures raw pointer (source outlives handle)
    s->midiHandle = MidiEngine::instance().subscribe(
        [s](const MidiEvent &ev) {
            if (s->midiPort >= 0 && (int)ev.portIndex != s->midiPort) return;

            const int ch = (int)ev.channel;

            // ── CC → knobs / faders / jog ─────────────────────────────────
            if (ev.type == MidiEventType::ControlChange) {
                const float val = ev.param2 / 127.0f;

                // Deck-specific CCs (transport channel for each deck)
                int deckIdx = (ch == s->midiChDeck0) ? 0
                            : (ch == s->midiChDeck1) ? 1 : -1;
                if (deckIdx >= 0) {
                    DeckState &dk = s->deck[deckIdx];
                    switch ((int)ev.param1) {
                        case kCC_EqHigh: dk.eqHigh  = val; break;
                        case kCC_EqMid:  dk.eqMid   = val; break;
                        case kCC_EqLow:  dk.eqLow   = val; break;
                        case kCC_Gain:   dk.gain     = val; break;
                        case kCC_Volume: dk.volume   = val; break;
                        case kCC_Tempo:  dk.tempo    = val; break;
                        // Jog wheel — relative encoder (value ≤ 0x3F = CW, ≥ 0x41 = CCW)
                        case kCC_JogVinyl:
                        case kCC_JogPitch:
                        case kCC_JogSide: {
                            int raw   = (int)ev.param2;
                            float delta = (raw <= 0x3F) ? (float)raw
                                                        : -(float)(0x80 - raw);
                            // ~128 ticks per visual revolution
                            dk.jogAngle = fmodf(dk.jogAngle + delta * 2.8125f, 360.0f);
                            if (dk.jogAngle < 0.0f) dk.jogAngle += 360.0f;
                            break;
                        }
                        default: break;
                    }
                }
                // Master-channel CCs (crossfader, per-channel filter)
                else if (ch == s->midiChMaster) {
                    switch ((int)ev.param1) {
                        case kCC_FilterCh1: s->deck[0].filter = val; break;
                        case kCC_FilterCh2: s->deck[1].filter = val; break;
                        case kCC_Xfader:    s->xfader          = val; break;
                        default: break;
                    }
                }
            }

            // ── Note On/Off → transport buttons ───────────────────────────
            if (ev.type == MidiEventType::NoteOn ||
                ev.type == MidiEventType::NoteOff)
            {
                const bool on   = (ev.type == MidiEventType::NoteOn && ev.param2 > 0);
                const int  note = (int)ev.param1;

                // Transport channel notes (play, cue, sync, loop controls)
                int deckIdx = (ch == s->midiChDeck0) ? 0
                            : (ch == s->midiChDeck1) ? 1 : -1;
                if (deckIdx >= 0) {
                    DeckState &dk = s->deck[deckIdx];
                    switch (note) {
                        case kNote_Play:    dk.playing       = on; break;
                        case kNote_Cue:     dk.cueActive     = on; break;
                        case kNote_Sync:    dk.syncActive    = on; break;
                        case kNote_LoopIn:  dk.loopInActive  = on; break;
                        case kNote_LoopOut: dk.loopOutActive = on; break;
                        case kNote_Reloop:  dk.reloopActive  = on; break;
                        default: break;
                    }
                    return;
                }

                // Pad channel notes (Hot Cues / Beat Loop / Beat Jump / Sampler)
                // FLX4 uses dedicated pad channels (ch7/ch9) separate from transport
                int padDeck = (ch == s->midiChPad0) ? 0
                            : (ch == s->midiChPad1) ? 1 : -1;
                if (padDeck >= 0) {
                    DeckState &dk = s->deck[padDeck];
                    // Hot cue pad range: notes 0x00–0x07 (pads 1–8)
                    if (note >= kNote_HCue0 && note < kNote_HCue0 + 8) {
                        int hc = note - kNote_HCue0;
                        dk.hotCueFlash[hc] = on ? (ev.param2 / 127.0f) : 0.0f;
                    }
                    // Beat loop pads: notes 0x60–0x67 (pads 1–8 in loop mode)
                    else if (note >= 0x60 && note <= 0x67) {
                        int hc = note - 0x60;
                        dk.hotCueFlash[hc] = on ? 0.8f : 0.0f;
                        if (on) dk.padMode = 1;
                    }
                    // Beat jump pads: notes 0x20–0x27
                    else if (note >= 0x20 && note <= 0x27) {
                        int hc = note - 0x20;
                        dk.hotCueFlash[hc] = on ? 0.6f : 0.0f;
                        if (on) dk.padMode = 2;
                    }
                    // Sampler pads: notes 0x30–0x37
                    else if (note >= 0x30 && note <= 0x37) {
                        int hc = note - 0x30;
                        dk.hotCueFlash[hc] = on ? 0.7f : 0.0f;
                        if (on) dk.padMode = 3;
                    }
                }
            }
        });

    return s;
}

static void dj_destroy(void *data)
{
    auto *s = static_cast<DjSource *>(data);
    MidiEngine::instance().unsubscribe(s->midiHandle);
    delete s;
}

static void dj_update(void *data, obs_data_t *settings)
{
    auto *s = static_cast<DjSource *>(data);
    dj_load_settings(s, settings);
    dj_apply_port(s->midiPort);
}

static void dj_tick(void *data, float seconds)
{
    auto *s = static_cast<DjSource *>(data);
    MidiEngine::instance().drainQueue();

    // Decay hot-cue / pad flash (fast cosmetic "hit" indicator)
    constexpr float kHCDecay = 8.0f;
    for (int d = 0; d < 2; ++d)
        for (int h = 0; h < 8; ++h)   // FLX4 has 8 pads
            if (s->deck[d].hotCueFlash[h] > 0.0f)
                s->deck[d].hotCueFlash[h] = std::max(
                    0.0f, s->deck[d].hotCueFlash[h] - kHCDecay * seconds);
}

static uint32_t dj_width (void *data) { return static_cast<DjSource*>(data)->cx; }
static uint32_t dj_height(void *data) { return static_cast<DjSource*>(data)->cy; }

// ─────────────────────────────────────────────────────────────────────────────
// Render
//
// Canvas is split into three zones:
//   Deck 0  : x=0      .. 559   (560 px wide)
//   Mixer   : x=560    .. 719   (160 px wide)
//   Deck 1  : x=720    .. 1279  (560 px wide, mirrored layout)
//
// Each deck contains: jog wheel (dominant center), EQ knobs (Hi/Mid/Lo),
// Trim, Filter, volume fader, tempo fader, transport buttons, hot-cue pads.
//
// Uses gs_technique_begin/end — never gs_effect_loop (must not be nested
// inside OBS's compositing pass).
// ─────────────────────────────────────────────────────────────────────────────
static void dj_render(void *data, gs_effect_t *effect)
{
    (void)effect;
    auto *s = static_cast<DjSource *>(data);

    const float W = (float)s->cx;
    const float H = (float)s->cy;

    // ── OBS render scaffolding ────────────────────────────────────────────────
    gs_effect_t    *solid      = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t    *colorParam = gs_effect_get_param_by_name(solid, "color");
    gs_technique_t *tech       = gs_effect_get_technique(solid, "Solid");
    size_t          passes     = gs_technique_begin(tech);

    // ── Drawing primitives ────────────────────────────────────────────────────

    // Filled rectangle
    auto drawRect = [&](float x, float y, float w, float h, uint32_t argb) {
        if (w < 1.0f || h < 1.0f) return;
        struct vec4 col;
        vec4_set(&col,
            ((argb >> 16) & 0xFF) / 255.0f,
            ((argb >>  8) & 0xFF) / 255.0f,
            ( argb        & 0xFF) / 255.0f,
            ((argb >> 24) & 0xFF) / 255.0f);
        gs_effect_set_vec4(colorParam, &col);
        gs_matrix_push();
        gs_matrix_translate3f(x, y, 0.0f);
        gs_draw_sprite(nullptr, 0, (uint32_t)w, (uint32_t)h);
        gs_matrix_pop();
    };

    // Filled circle via horizontal scan strips (stride-2 for GPU perf)
    auto fillCircle = [&](float cx, float cy, float r, uint32_t argb) {
        const int iy0 = (int)(cy - r);
        const int iy1 = (int)(cy + r + 1.0f);
        const float r2 = r * r;
        for (int iy = iy0; iy < iy1; iy += 2) {
            float dy  = (float)iy + 0.5f - cy;
            float dx2 = r2 - dy * dy;
            if (dx2 <= 0.0f) continue;
            float dx = sqrtf(dx2);
            drawRect(cx - dx, (float)iy, dx * 2.0f, 2.0f, argb);
        }
    };

    // Bitmap text (3×5 font, fscale = pixel size per logical pixel)
    auto drawText = [&](float x, float y, float fscale,
                        uint32_t color, const char *text) {
        if (!text || !text[0]) return;
        int len = (int)strlen(text);
        for (int ci = 0; ci < len; ++ci) {
            int fi = djFontIdx(text[ci]);
            float charX = x + (float)ci * 4.0f * fscale;
            for (int row5 = 0; row5 < 5; ++row5) {
                uint8_t bits = kDjFont[fi][row5];
                for (int bit = 0; bit < 3; ++bit) {
                    if (bits & (1u << (2 - bit))) {
                        drawRect(charX + (float)bit * fscale,
                                 y    + (float)row5 * fscale,
                                 fscale, fscale, color);
                    }
                }
            }
        }
    };

    // ── Knob with 27-dot LED arc (Pioneer style) ──────────────────────────────
    // value  : 0.0–1.0
    // cDet   : if true, arc lights from centre (0.5) outward (EQ-style detent)
    auto drawKnob = [&](float cx, float cy, float r, float value,
                        uint32_t colorOn, bool cDet) {
        // Body
        fillCircle(cx, cy, r,          s->colorKnobBody);
        fillCircle(cx, cy, r * 0.65f,  s->colorKnobCap);

        // Pointer dot
        float pDeg = 225.0f + value * 270.0f;
        float pRad = pDeg * ((float)M_PI / 180.0f);
        float px   = cx + (r * 0.42f) * sinf(pRad);
        float py   = cy - (r * 0.42f) * cosf(pRad);
        drawRect(px - 2.0f, py - 2.0f, 4.0f, 4.0f, 0xFFDDDDDD);

        // LED arc: 27 dots, 225° → 495° (270° sweep), radius = r + 6
        const float arcR = r + 6.0f;
        for (int i = 0; i < 27; ++i) {
            float deg = 225.0f + (float)i * (270.0f / 26.0f);
            float rad = deg * ((float)M_PI / 180.0f);
            float dx  = cx + arcR * sinf(rad);
            float dy  = cy - arcR * cosf(rad);
            float frac = (float)i / 26.0f;

            bool lit;
            if (cDet) {
                // Light arc from midpoint toward value
                if (value >= 0.5f) lit = (frac >= 0.5f - 0.02f) && (frac <= value + 0.02f);
                else               lit = (frac >= value - 0.02f) && (frac <= 0.5f + 0.02f);
            } else {
                lit = frac <= value + 0.02f;
            }

            drawRect(dx - 1.5f, dy - 1.5f, 3.0f, 3.0f,
                     lit ? colorOn : s->colorKnobOff);
        }
    };

    // ── Vertical fader ────────────────────────────────────────────────────────
    // value 0=bottom, 1=top
    auto drawVFader = [&](float cx, float y1, float y2,
                          float value, uint32_t colorFill) {
        const float trackW = 7.0f;
        const float hW     = 22.0f;
        const float hH     = 12.0f;
        const float trackH = y2 - y1;
        const float hy     = y2 - value * trackH;

        // Track
        drawRect(cx - trackW * 0.5f, y1, trackW, trackH, 0xFF333333);
        // Fill below handle (represents level)
        if (y2 - hy > 0.0f)
            drawRect(cx - 3.0f, hy, 6.0f, y2 - hy, colorFill);
        // Handle cap
        drawRect(cx - hW * 0.5f, hy - hH * 0.5f, hW, hH, 0xFF777777);
        drawRect(cx - hW * 0.5f + 2.0f, hy - 1.5f, hW - 4.0f, 3.0f, 0xFFCCCCCC);
    };

    // ── Horizontal fader (crossfader) ─────────────────────────────────────────
    auto drawHFader = [&](float x1, float x2, float y, float value) {
        const float trackH = 7.0f;
        const float hW     = 14.0f;
        const float hH     = 26.0f;
        const float trackW = x2 - x1;
        const float hx     = x1 + value * trackW;

        drawRect(x1, y - trackH * 0.5f, trackW, trackH, 0xFF333333);
        drawRect(x1, y - 3.0f, hx - x1, 6.0f, s->colorAccent);
        drawRect(hx - hW * 0.5f, y - hH * 0.5f, hW, hH, 0xFF777777);
        drawRect(hx - 1.5f, y - hH * 0.5f + 2.0f, 3.0f, hH - 4.0f, 0xFFCCCCCC);
    };

    // ── Transport / function button ───────────────────────────────────────────
    auto drawButton = [&](float x, float y, float w, float h,
                          bool active, uint32_t colorAct, const char *label) {
        uint32_t bg  = active ? colorAct : 0xFF2A2A2A;
        uint32_t brd = active ? colorAct : 0xFF4A4A4A;
        drawRect(x,           y,           w,    h,    bg);
        drawRect(x,           y,           w,    1.0f, brd);
        drawRect(x,           y + h - 1.f, w,    1.0f, brd);
        drawRect(x,           y,           1.0f, h,    brd);
        drawRect(x + w - 1.f, y,           1.0f, h,    brd);
        if (!label || !label[0]) return;
        int   len    = (int)strlen(label);
        float fscale = std::max(1.0f,
            std::floor(std::min(w / ((float)len * 4.0f + 2.0f), h / 7.0f)));
        float tw = ((float)len * 4.0f - 1.0f) * fscale;
        float th = 5.0f * fscale;
        drawText(x + (w - tw) * 0.5f, y + (h - th) * 0.5f, fscale,
                 active ? 0xFFFFFFFF : 0xFF888888, label);
    };

    // ── Jog wheel ─────────────────────────────────────────────────────────────
    auto drawJog = [&](float cx, float cy, float r, float angleDeg, bool playing) {
        // Outer rim (silver-grey)
        fillCircle(cx, cy, r,            0xFF606060);
        // Groove ring
        fillCircle(cx, cy, r - 7.0f,    0xFF141414);
        // Platter disc
        fillCircle(cx, cy, r - 11.0f,   0xFF282828);
        // Hub outer
        fillCircle(cx, cy, r * 0.25f,   0xFF5A5A5A);
        // Hub inner
        fillCircle(cx, cy, r * 0.16f,   0xFF383838);

        // Rotating platter mark (one dot on the disc)
        float dotR   = r - 24.0f;
        float dotRad = angleDeg * ((float)M_PI / 180.0f);
        float dox    = cx + dotR * sinf(dotRad);
        float doy    = cy - dotR * cosf(dotRad);
        fillCircle(dox, doy, 4.5f, 0xFF999999);

        // Playing indicator: thin green ring drawn as 36 dots around the groove
        if (playing) {
            float ringR = r - 4.5f;
            for (int i = 0; i < 36; ++i) {
                float a  = (float)i * (10.0f * ((float)M_PI / 180.0f));
                float rx = cx + ringR * sinf(a);
                float ry = cy - ringR * cosf(a);
                drawRect(rx - 2.0f, ry - 2.0f, 4.0f, 4.0f, 0xFF00CC44);
            }
        }
    };

    // ── Per-deck drawing (called for both decks) ──────────────────────────────
    // xOff    : left edge of the 560-px deck zone
    // mirrored: deck 1 flips knob/fader sides (mirrors deck 0 layout)
    //
    // Accurate DDJ-FLX4 layout:
    //   TEMPO fader — outer edge (far left for deck 0, far right for deck 1)
    //   JOG WHEEL  — dominant centre
    //   KNOB COLUMN — inner side of jog (EQ Hi/Mid/Low + Trim + Filter)
    //   CHANNEL FADER — between knob column and mixer
    //   Below jog: [LOOP-IN | RELOOP | LOOP-OUT] then [PLAY | CUE | SYNC]
    //   Pad mode row: [HC | LOOP | JUMP | SAMP]
    //   8 performance pads in 2×4 grid
    auto drawDeck = [&](float xOff, const DeckState &dk, bool mirrored) {
        const float DW = 560.0f;

        // ── Background & border ────────────────────────────────────────────
        drawRect(xOff, 0.0f, DW, H, s->colorDeck);
        if (mirrored) drawRect(xOff,            0.0f, 2.0f, H, 0xFF3A3A3A);
        else          drawRect(xOff + DW - 2.0f, 0.0f, 2.0f, H, 0xFF3A3A3A);

        // ── Jog wheel — slightly smaller than original to fit controls below ─
        // FLX4 has a 5.7-inch jog; deck 0 jog centre at x=230, deck 1 at x=330
        const float jogCx = mirrored ? (xOff + 330.0f) : (xOff + 230.0f);
        const float jogCy = H * 0.395f;   // ~190 px at H=480
        const float jogR  = std::min(H * 0.228f, 110.0f);
        drawJog(jogCx, jogCy, jogR, dk.jogAngle, dk.playing);

        // ── Knob column ────────────────────────────────────────────────────
        // Deck 0: knobs right-of-jog (inner side). Deck 1: mirrored left.
        const float knobX  = mirrored ? (xOff + 136.0f) : (xOff + DW - 136.0f);
        const float knobR  = 22.0f;
        const float knobR2 = 18.0f;
        const float lblS   = 1.0f;

        drawKnob(knobX,  76.0f, knobR,  dk.eqHigh, s->colorAccent, true);
        drawText(knobX -  4.0f, 50.0f, lblS, s->colorText, "HI");

        drawKnob(knobX, 150.0f, knobR,  dk.eqMid,  s->colorAccent, true);
        drawText(knobX -  7.0f,124.0f, lblS, s->colorText, "MID");

        drawKnob(knobX, 224.0f, knobR,  dk.eqLow,  s->colorAccent, true);
        drawText(knobX -  4.0f,198.0f, lblS, s->colorText, "LO");

        drawKnob(knobX, 288.0f, knobR2, dk.gain,   0xFFFFDD00, false);
        drawText(knobX -  9.0f,264.0f, lblS, s->colorText, "TRIM");

        drawKnob(knobX, 342.0f, knobR2, dk.filter, 0xFF00AAFF, false);
        drawText(knobX - 10.0f,318.0f, lblS, s->colorText, "FILT");

        // ── Volume fader (channel, inner edge) ────────────────────────────
        const float volX = mirrored ? (xOff + 50.0f) : (xOff + DW - 50.0f);
        drawVFader(volX, 48.0f, 368.0f, dk.volume, s->colorAccent);
        drawText(volX - 6.0f, 374.0f, lblS, s->colorText, "VOL");

        // ── Tempo fader (outer edge) ───────────────────────────────────────
        const float tmpX = mirrored ? (xOff + DW - 50.0f) : (xOff + 50.0f);
        drawVFader(tmpX, 48.0f, 368.0f, dk.tempo, 0xFF888888);
        drawText(tmpX - 14.0f, 374.0f, lblS, s->colorText, "TEMPO");

        // ── Loop controls (above transport row) ───────────────────────────
        // FLX4 has LOOP IN | RELOOP/EXIT | LOOP OUT above the transport
        const float lbW  = 58.0f, lbH  = 20.0f, lbSp = 4.0f;
        const float lbRW = 3.0f * lbW + 2.0f * lbSp;
        const float lbx0 = jogCx - lbRW * 0.5f;
        const float lbY  = jogCy + jogR + 8.0f;

        drawButton(lbx0,                   lbY, lbW, lbH, dk.loopInActive,  0xFF00CCFF, "IN");
        drawButton(lbx0 + lbW + lbSp,      lbY, lbW, lbH, dk.reloopActive,  0xFF00CC44, "RELOOP");
        drawButton(lbx0 + 2*(lbW + lbSp),  lbY, lbW, lbH, dk.loopOutActive, 0xFF00CCFF, "OUT");

        // ── Transport buttons (PLAY / CUE / BEAT SYNC) ────────────────────
        const float tbW1 = 70.0f, tbW2 = 57.0f, tbH  = 26.0f, tbSp = 4.0f;
        const float tbRW = tbW1 + 2.0f * tbW2 + 2.0f * tbSp;
        const float tbx0 = jogCx - tbRW * 0.5f;
        const float tbY  = lbY + lbH + 4.0f;

        drawButton(tbx0,                        tbY, tbW1, tbH, dk.playing,    s->colorPlay, "PLAY");
        drawButton(tbx0 + tbW1 + tbSp,          tbY, tbW2, tbH, dk.cueActive,  s->colorCue,  "CUE");
        drawButton(tbx0 + tbW1 + tbSp + tbW2 + tbSp, tbY, tbW2, tbH, dk.syncActive, s->colorSync, "SYNC");

        // ── Pad mode selector ─────────────────────────────────────────────
        // HC = Hot Cue  LOOP = Beat Loop  JUMP = Beat Jump  SAMP = Sampler
        static const char     *kModeLabels[4]  = { "HC", "LOOP", "JUMP", "SAMP" };
        static const uint32_t  kModeColors[4]  = { 0xFFFF4444,0xFF00CCFF,0xFFFFAA00,0xFFAA44FF };
        const float mbW  = 46.0f, mbH  = 14.0f, mbSp = 3.0f;
        const float mbRW = 4.0f * mbW + 3.0f * mbSp;
        const float mbx0 = jogCx - mbRW * 0.5f;
        const float mbY  = tbY + tbH + 4.0f;

        for (int m = 0; m < 4; ++m) {
            const float   mx = mbx0 + (float)m * (mbW + mbSp);
            const bool    ma = (dk.padMode == m);
            const uint32_t mc = kModeColors[m];
            drawRect(mx, mbY, mbW, mbH, ma ? mc : 0xFF222222);
            drawRect(mx, mbY, mbW, 1.0f, ma ? mc : 0xFF444444);
            drawRect(mx, mbY, 1.0f, mbH, ma ? mc : 0xFF444444);
            drawRect(mx + mbW - 1.0f, mbY, 1.0f, mbH, ma ? mc : 0xFF444444);
            drawRect(mx, mbY + mbH - 1.0f, mbW, 1.0f, ma ? mc : 0xFF444444);
            const int   mlen = (int)strlen(kModeLabels[m]);
            const float mts  = std::max(1.0f, std::floor(
                std::min(mbW / ((float)mlen * 4.0f + 2.0f), mbH / 7.0f)));
            const float mtw  = ((float)mlen * 4.0f - 1.0f) * mts;
            const float mth  = 5.0f * mts;
            drawText(mx + (mbW - mtw) * 0.5f, mbY + (mbH - mth) * 0.5f,
                     mts, ma ? 0xFFFFFFFF : 0xFF666666, kModeLabels[m]);
        }

        // ── Performance pads — 8 pads in 2×4 grid ────────────────────────
        // FLX4 has 8 RGB pads; top row = pads 1–4, bottom row = pads 5–8
        const float padW  = 46.0f, padH  = 26.0f, padSp = 3.0f;
        const float padRW = 4.0f * padW + 3.0f * padSp;
        const float px0   = jogCx - padRW * 0.5f;
        const float padY1 = mbY + mbH + 3.0f;
        const float padY2 = padY1 + padH + 3.0f;

        for (int p2 = 0; p2 < 8; ++p2) {
            const int   row   = p2 / 4;
            const int   col   = p2 % 4;
            const float flash = dk.hotCueFlash[p2];
            const uint32_t hc = s->hotCueColors[p2 % 4];  // cycle through 4 accent colours
            const float bright = 0.22f + flash * 0.78f;
            const uint32_t pc  = 0xFF000000
                | ((uint32_t)(((hc>>16)&0xFF) * bright) << 16)
                | ((uint32_t)(((hc>> 8)&0xFF) * bright) <<  8)
                | ((uint32_t)(( hc     &0xFF) * bright));

            const float px = px0 + (float)col * (padW + padSp);
            const float py = (row == 0) ? padY1 : padY2;

            drawRect(px,            py,            padW, padH, pc);
            drawRect(px,            py,            padW, 1.0f, 0xFF555555);
            drawRect(px,            py + padH-1.f, padW, 1.0f, 0xFF555555);
            drawRect(px,            py,            1.0f, padH, 0xFF555555);
            drawRect(px + padW-1.f, py,            1.0f, padH, 0xFF555555);

            char num[4];
            snprintf(num, sizeof(num), "%d", p2 + 1);
            const float nw = ((float)strlen(num) * 4.0f - 1.0f);
            drawText(px + (padW - nw) * 0.5f, py + (padH - 5.0f) * 0.5f,
                     1.0f, 0xFFFFFFFF, num);
        }
    };

    // ── Main render loop ──────────────────────────────────────────────────────
    for (size_t p = 0; p < passes; ++p) {
        gs_technique_begin_pass(tech, p);

        // Canvas background
        drawRect(0.0f, 0.0f, W, H, s->colorPanel);

        // Deck 0 (left, x=0..559)
        drawDeck(0.0f, s->deck[0], false);

        // Mixer (center, x=560..719)
        {
            const float mx = 560.0f;
            const float mw = 160.0f;
            drawRect(mx, 0.0f, mw, H, 0xFF191919);
            // Header
            drawText(mx + 42.0f, 14.0f, 2.0f, s->colorAccent, "MIX");
            // Separator lines
            drawRect(mx,            0.0f, 1.0f, H, 0xFF3A3A3A);
            drawRect(mx + mw - 1.f, 0.0f, 1.0f, H, 0xFF3A3A3A);
            // Crossfader
            drawHFader(mx + 18.0f, mx + mw - 18.0f, H - 52.0f, s->xfader);
            drawText(mx + 30.0f, H - 38.0f, 1.0f, s->colorText, "XFADER");
        }

        // Deck 1 (right, x=720..1279)
        drawDeck(720.0f, s->deck[1], true);

        gs_technique_end_pass(tech);
    }

    gs_technique_end(tech);
}

// ─────────────────────────────────────────────────────────────────────────────
// Properties panel
// ─────────────────────────────────────────────────────────────────────────────
static obs_properties_t *dj_properties(void *)
{
    obs_properties_t *props = obs_properties_create();

    obs_properties_add_int(props, "canvas_w", "Canvas width",  320, 3840, 1);
    obs_properties_add_int(props, "canvas_h", "Canvas height", 200, 2160, 1);

    // MIDI port selector
    obs_property_t *port_list = obs_properties_add_list(props, "midi_port",
        "MIDI Port", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(port_list, "Disabled", -1);
    auto ps = MidiEngine::instance().portNames();
    for (int i = 0; i < (int)ps.size(); ++i)
        obs_property_list_add_int(port_list, ps[i].c_str(), i);

    obs_properties_add_button(props, "midi_refresh", "Refresh Devices",
        [](obs_properties_t *pp, obs_property_t *, void *) -> bool {
            obs_property_t *lst = obs_properties_get(pp, "midi_port");
            obs_property_list_clear(lst);
            obs_property_list_add_int(lst, "Disabled", -1);
            auto names = MidiEngine::instance().portNames();
            for (int i = 0; i < (int)names.size(); ++i)
                obs_property_list_add_int(lst, names[i].c_str(), i);
            return true;
        });

    obs_properties_add_int(props, "deck0_ch",  "Deck 1 MIDI Channel (1–16)", 1, 16, 1);
    obs_properties_add_int(props, "deck1_ch",  "Deck 2 MIDI Channel (1–16)", 1, 16, 1);
    obs_properties_add_int(props, "master_ch", "Mixer MIDI Channel  (1–16)", 1, 16, 1);

    return props;
}

// ─────────────────────────────────────────────────────────────────────────────
// Source info struct + registration
// ─────────────────────────────────────────────────────────────────────────────
void dj_source_register(void)
{
    struct obs_source_info info = {};
    info.id             = "dj_source";
    info.type           = OBS_SOURCE_TYPE_INPUT;
    info.output_flags   = OBS_SOURCE_VIDEO;
    info.get_name       = dj_get_name;
    info.create         = dj_create;
    info.destroy        = dj_destroy;
    info.update         = dj_update;
    info.video_tick     = dj_tick;
    info.video_render   = dj_render;
    info.get_width      = dj_width;
    info.get_height     = dj_height;
    info.get_defaults   = dj_set_defaults;
    info.get_properties = dj_properties;
    obs_register_source(&info);
}
