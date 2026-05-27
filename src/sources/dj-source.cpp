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
// Default MIDI CC assignments — Pioneer DDJ-FLX4
// All values are 0-indexed CC numbers; can be changed via properties.
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int kCC_EqHigh  = 19;
static constexpr int kCC_EqMid   = 23;
static constexpr int kCC_EqLow   = 27;
static constexpr int kCC_Gain    = 8;
static constexpr int kCC_Filter  = 21;
static constexpr int kCC_Volume  = 7;
static constexpr int kCC_Tempo   = 0;
static constexpr int kCC_Jog     = 32;  // relative: 1–63=CW, 65–127=CCW
static constexpr int kCC_Xfader  = 10;

static constexpr int kNote_Play  = 11;
static constexpr int kNote_Cue   = 12;
static constexpr int kNote_Sync  = 13;
static constexpr int kNote_Loop  = 16;
static constexpr int kNote_HCue0 = 40;  // hot cues 1–4 = notes 40–43

// ─────────────────────────────────────────────────────────────────────────────
// Per-deck state
// ─────────────────────────────────────────────────────────────────────────────
struct DeckState {
    float eqHigh     = 0.50f;  // 0=min, 1=max; EQ knobs use center-detent
    float eqMid      = 0.50f;
    float eqLow      = 0.50f;
    float gain       = 0.50f;
    float filter     = 0.50f;
    float volume     = 0.75f;
    float tempo      = 0.50f;  // 0.5 = 0% pitch
    float jogAngle   = 0.0f;   // degrees, 0-360
    bool  playing    = false;
    bool  cueActive  = false;
    bool  syncActive = false;
    bool  loopActive = false;
    float hotCueFlash[4] = {};
};

// ─────────────────────────────────────────────────────────────────────────────
// Plugin source instance
// ─────────────────────────────────────────────────────────────────────────────
struct DjSource {
    DeckState deck[2];
    float     xfader        = 0.50f;

    int       midiPort      = -1;
    int       midiChDeck0   = 0;   // 0-indexed MIDI channels
    int       midiChDeck1   = 1;
    int       midiChMaster  = 5;
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
    obs_data_set_default_int(settings, "deck0_ch",     1);
    obs_data_set_default_int(settings, "deck1_ch",     2);
    obs_data_set_default_int(settings, "master_ch",    6);
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

            // ── CC → knob / fader / jog ──────────────────────────────────
            if (ev.type == MidiEventType::ControlChange) {
                int deckIdx = -1;
                if (ch == s->midiChDeck0)  deckIdx = 0;
                else if (ch == s->midiChDeck1)  deckIdx = 1;

                const float val = ev.param2 / 127.0f;

                if (deckIdx >= 0) {
                    DeckState &dk = s->deck[deckIdx];
                    switch ((int)ev.param1) {
                        case kCC_EqHigh:  dk.eqHigh  = val; break;
                        case kCC_EqMid:   dk.eqMid   = val; break;
                        case kCC_EqLow:   dk.eqLow   = val; break;
                        case kCC_Gain:    dk.gain     = val; break;
                        case kCC_Filter:  dk.filter   = val; break;
                        case kCC_Volume:  dk.volume   = val; break;
                        case kCC_Tempo:   dk.tempo    = val; break;
                        case kCC_Jog: {
                            // Relative encoder: 1–63=CW (+delta), 65–127=CCW (-(128-raw))
                            int raw = (int)ev.param2;
                            float delta = (raw <= 63) ? (float)raw
                                                      : -(float)(128 - raw);
                            // 128 ticks ≈ one full visual revolution
                            dk.jogAngle = fmodf(dk.jogAngle + delta * 2.8125f, 360.0f);
                            if (dk.jogAngle < 0.0f) dk.jogAngle += 360.0f;
                            break;
                        }
                        default: break;
                    }
                } else if (ch == s->midiChMaster) {
                    if ((int)ev.param1 == kCC_Xfader) s->xfader = val;
                }
            }

            // ── Note On/Off → transport buttons + hot cues ───────────────
            if (ev.type == MidiEventType::NoteOn ||
                ev.type == MidiEventType::NoteOff)
            {
                int deckIdx = -1;
                if (ch == s->midiChDeck0)  deckIdx = 0;
                else if (ch == s->midiChDeck1)  deckIdx = 1;
                if (deckIdx < 0) return;

                DeckState &dk = s->deck[deckIdx];
                const bool on = (ev.type == MidiEventType::NoteOn &&
                                 ev.param2 > 0);
                const int note = (int)ev.param1;

                switch (note) {
                    case kNote_Play: dk.playing    = on; break;
                    case kNote_Cue:  dk.cueActive  = on; break;
                    case kNote_Sync: dk.syncActive = on; break;
                    case kNote_Loop: dk.loopActive = on; break;
                    default: break;
                }
                if (note >= kNote_HCue0 && note < kNote_HCue0 + 4) {
                    int hc = note - kNote_HCue0;
                    dk.hotCueFlash[hc] = on ? (ev.param2 / 127.0f) : 0.0f;
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

    // Decay hot-cue flash (fast — purely cosmetic "hit" indicator)
    constexpr float kHCDecay = 8.0f;
    for (int d = 0; d < 2; ++d)
        for (int h = 0; h < 4; ++h)
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
    // xOff   : left edge of the 560-px deck zone in canvas coordinates
    // mirrored: deck 1 swaps knob/fader sides
    auto drawDeck = [&](float xOff, const DeckState &dk, bool mirrored) {
        const float DW  = 560.0f;

        // Deck background
        drawRect(xOff, 0.0f, DW, H, s->colorDeck);

        // Separator line on the mixer-facing edge
        if (mirrored)
            drawRect(xOff, 0.0f, 2.0f, H, 0xFF3A3A3A);
        else
            drawRect(xOff + DW - 2.0f, 0.0f, 2.0f, H, 0xFF3A3A3A);

        // ── Jog wheel ─────────────────────────────────────────────────────
        // Deck 0: jog slightly left of centre so controls fit on the right.
        // Deck 1: mirrored — jog slightly right of centre.
        const float jogCx = mirrored ? (xOff + 330.0f) : (xOff + 230.0f);
        const float jogCy = H * 0.47f;
        const float jogR  = std::min(H * 0.40f, 130.0f);
        drawJog(jogCx, jogCy, jogR, dk.jogAngle, dk.playing);

        // ── Knob column ───────────────────────────────────────────────────
        // Deck 0: knobs on the right. Deck 1: knobs on the left.
        const float knobX  = mirrored ? (xOff + 136.0f) : (xOff + DW - 136.0f);
        const float knobR  = 22.0f;
        const float knobR2 = 18.0f;
        const float lblS   = 1.0f;

        // EQ High
        drawKnob(knobX, 82.0f, knobR, dk.eqHigh, s->colorAccent, true);
        drawText(knobX - 4.0f, 56.0f, lblS, s->colorText, "HI");

        // EQ Mid
        drawKnob(knobX, 157.0f, knobR, dk.eqMid, s->colorAccent, true);
        drawText(knobX - 7.0f, 131.0f, lblS, s->colorText, "MID");

        // EQ Low
        drawKnob(knobX, 232.0f, knobR, dk.eqLow, s->colorAccent, true);
        drawText(knobX - 4.0f, 206.0f, lblS, s->colorText, "LO");

        // Trim
        drawKnob(knobX, 302.0f, knobR2, dk.gain, 0xFFFFDD00, false);
        drawText(knobX - 9.0f, 278.0f, lblS, s->colorText, "TRIM");

        // Filter
        drawKnob(knobX, 367.0f, knobR2, dk.filter, 0xFF00AAFF, false);
        drawText(knobX - 10.0f, 343.0f, lblS, s->colorText, "FILT");

        // ── Volume fader ──────────────────────────────────────────────────
        // Deck 0: far right. Deck 1: far left.
        const float volX = mirrored ? (xOff + 52.0f) : (xOff + DW - 50.0f);
        drawVFader(volX, 48.0f, 360.0f, dk.volume, s->colorAccent);
        drawText(volX - 6.0f, 368.0f, lblS, s->colorText, "VOL");

        // ── Tempo fader ───────────────────────────────────────────────────
        // Deck 0: far left. Deck 1: far right.
        const float tmpX = mirrored ? (xOff + DW - 50.0f) : (xOff + 52.0f);
        drawVFader(tmpX, 48.0f, 360.0f, dk.tempo, 0xFF888888);
        drawText(tmpX - 14.0f, 368.0f, lblS, s->colorText, "TEMPO");

        // ── Transport buttons (row below jog) ─────────────────────────────
        const float btnW  = 50.0f;
        const float btnH  = 27.0f;
        const float btnSp = 5.0f;
        const float rowW  = 4.0f * btnW + 3.0f * btnSp;
        const float bx0   = jogCx - rowW * 0.5f;
        const float btnY  = jogCy + jogR + 12.0f;

        drawButton(bx0,                          btnY, btnW, btnH, dk.playing,    s->colorPlay,  "PLAY");
        drawButton(bx0 +     (btnW + btnSp),     btnY, btnW, btnH, dk.cueActive,  s->colorCue,   "CUE");
        drawButton(bx0 + 2.0f*(btnW + btnSp),    btnY, btnW, btnH, dk.syncActive, s->colorSync,  "SYNC");
        drawButton(bx0 + 3.0f*(btnW + btnSp),    btnY, btnW, btnH, dk.loopActive, s->colorLoop,  "LOOP");

        // ── Hot cue pads (bottom strip) ───────────────────────────────────
        const float padW  = 50.0f;
        const float padH  = 30.0f;
        const float padSp = 5.0f;
        const float padRW = 4.0f * padW + 3.0f * padSp;
        const float px0   = jogCx - padRW * 0.5f;
        const float padY  = btnY + btnH + 8.0f;

        for (int p = 0; p < 4; ++p) {
            const float flash   = dk.hotCueFlash[p];
            const uint32_t hc   = s->hotCueColors[p];
            // Base colour dimmed by 75 %, brightened by flash
            const float  bright = 0.25f + flash * 0.75f;
            const uint32_t pc   = 0xFF000000
                | ((uint32_t)(((hc>>16)&0xFF) * bright) << 16)
                | ((uint32_t)(((hc>> 8)&0xFF) * bright) <<  8)
                | ((uint32_t)(( hc     &0xFF) * bright));

            const float px = px0 + (float)p * (padW + padSp);
            drawRect(px,            padY,            padW, padH, pc);
            drawRect(px,            padY,            padW, 1.0f, 0xFF555555);
            drawRect(px,            padY + padH-1.f, padW, 1.0f, 0xFF555555);
            drawRect(px,            padY,            1.0f, padH, 0xFF555555);
            drawRect(px + padW-1.f, padY,            1.0f, padH, 0xFF555555);

            char num[4];
            snprintf(num, sizeof(num), "%d", p + 1);
            float nw = (strlen(num) * 4.0f - 1.0f);
            drawText(px + (padW - nw) * 0.5f, padY + (padH - 5.0f) * 0.5f,
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
