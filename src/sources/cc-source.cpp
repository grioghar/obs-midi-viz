#include "cc-source.hpp"
#include "../midi-engine.hpp"
#include "../plugin-support.h"
#include <obs-module.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// 3×5 pixel bitmap font (same table as drum-source / dj-source)
// Each byte = one row; bit2=left, bit1=centre, bit0=right.
// ─────────────────────────────────────────────────────────────────────────────
static const uint8_t kCcFont[38][5] = {
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

static int ccFontIdx(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    char u = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    if (u >= 'A' && u <= 'Z') return 10 + (u - 'A');
    if (c == '-') return 36;
    return 37;
}

// ─────────────────────────────────────────────────────────────────────────────
// Short display names for well-known MIDI CC numbers (≤5 chars recommended)
// Returns nullptr for unknown CCs; caller falls back to "CC N" formatting.
// ─────────────────────────────────────────────────────────────────────────────
static const char *cc_display_name(int cc)
{
    switch (cc) {
        case  1: return "Mod";
        case  2: return "Brth";
        case  4: return "Foot";
        case  5: return "Prta";
        case  7: return "Vol";
        case  8: return "Bal";
        case 10: return "Pan";
        case 11: return "Expr";
        case 12: return "FX1";
        case 13: return "FX2";
        case 16: return "GP1";
        case 17: return "GP2";
        case 18: return "GP3";
        case 19: return "GP4";
        case 64: return "Sust";
        case 65: return "Prta";
        case 66: return "Sost";
        case 67: return "Soft";
        case 68: return "Leg";
        case 71: return "Reso";
        case 72: return "Rel";
        case 73: return "Atk";
        case 74: return "Filt";
        case 75: return "Dcy";
        case 76: return "VibR";
        case 77: return "VibD";
        case 91: return "Rev";
        case 92: return "Trem";
        case 93: return "Cho";
        case 94: return "Det";
        case 95: return "Pha";
        default: return nullptr;
    }
}

// Populate the stored label for lane i from ccNumbers[i].
static void cc_refresh_label(char *label, int bufLen, int ccNumber)
{
    const char *name = cc_display_name(ccNumber);
    if (name) {
        snprintf(label, bufLen, "%s", name);
    } else {
        snprintf(label, bufLen, "CC%d", ccNumber);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-instance state
// ─────────────────────────────────────────────────────────────────────────────
struct CcSource {
    static constexpr int MAX_LANES = 8;

    int   ccNumbers[MAX_LANES];       // Which CC each lane tracks (0-127)
    float ccValues [MAX_LANES];       // Smoothed current value  (0.0–1.0)
    float ccTargets[MAX_LANES];       // Raw latest value from MIDI event
    float ccPeak   [MAX_LANES];       // Peak-hold value         (0.0–1.0)
    float ccPeakAge[MAX_LANES];       // Seconds since peak was last raised
    char  ccLabels [MAX_LANES][32];   // Display label (auto from CC name)

    uint32_t colorBar  = 0xFF00AAFF;  // bar fill base colour
    uint32_t colorBg   = 0xFF111111;  // canvas / trough background
    bool     showValue = true;        // numeric 0-127 overlay above each bar
    bool     showPeak  = true;        // peak-hold line
    float    smoothing = 0.12f;       // lag coefficient: 0=instant, 1=frozen
    int      numLanes  = 8;           // how many lanes to display (1-8)

    uint32_t midiHandle = 0;
    int      midiPort   = -1;
    uint32_t cx         = 560;
    uint32_t cy         = 200;

    CcSource()
    {
        const int     defCC [MAX_LANES] = {1, 2, 11, 64, 7, 10, 74, 71};
        for (int i = 0; i < MAX_LANES; ++i) {
            ccNumbers[i] = defCC[i];
            ccValues [i] = 0.0f;
            ccTargets[i] = 0.0f;
            ccPeak   [i] = 0.0f;
            ccPeakAge[i] = 999.0f;
            cc_refresh_label(ccLabels[i], sizeof(ccLabels[i]), ccNumbers[i]);
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static void apply_midi_port(int portIdx)
{
    if (portIdx >= 0)
        MidiEngine::instance().openPort(portIdx);
}

static uint32_t lerp_argb_cc(uint32_t a, uint32_t b, float t)
{
    auto l8 = [t](uint32_t ca, uint32_t cb) -> uint32_t {
        return (uint32_t)((float)ca + ((float)cb - (float)ca) * t);
    };
    return (l8((a>>24)&0xFF,(b>>24)&0xFF)<<24)
         | (l8((a>>16)&0xFF,(b>>16)&0xFF)<<16)
         | (l8((a>> 8)&0xFF,(b>> 8)&0xFF)<< 8)
         |  l8( a     &0xFF, b     &0xFF);
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings load / defaults
// ─────────────────────────────────────────────────────────────────────────────
static const char *kCcKeys[CcSource::MAX_LANES] = {
    "cc0","cc1","cc2","cc3","cc4","cc5","cc6","cc7"
};

static void cc_load_settings(CcSource *s, obs_data_t *settings)
{
    s->midiPort  = (int)obs_data_get_int(settings,  "midi_port");
    s->cx        = (uint32_t)std::max(80LL,  obs_data_get_int(settings, "canvas_w"));
    s->cy        = (uint32_t)std::max(40LL,  obs_data_get_int(settings, "canvas_h"));
    s->numLanes  = (int)std::max(1LL, std::min(8LL, obs_data_get_int(settings, "num_lanes")));
    s->colorBar  = (uint32_t)obs_data_get_int(settings,  "color_bar");
    s->colorBg   = (uint32_t)obs_data_get_int(settings,  "color_bg");
    s->showValue = obs_data_get_bool(settings, "show_value");
    s->showPeak  = obs_data_get_bool(settings, "show_peak");
    s->smoothing = (float)obs_data_get_double(settings, "smoothing");

    for (int i = 0; i < CcSource::MAX_LANES; ++i) {
        s->ccNumbers[i] = (int)std::max(0LL, std::min(127LL,
            obs_data_get_int(settings, kCcKeys[i])));
        cc_refresh_label(s->ccLabels[i], sizeof(s->ccLabels[i]), s->ccNumbers[i]);
    }
}

static void cc_defaults(obs_data_t *settings)
{
    obs_data_set_default_int(   settings, "midi_port",  -1);
    obs_data_set_default_int(   settings, "canvas_w",  560);
    obs_data_set_default_int(   settings, "canvas_h",  200);
    obs_data_set_default_int(   settings, "num_lanes",   8);
    obs_data_set_default_int(   settings, "color_bar",   0xFF00AAFF);
    obs_data_set_default_int(   settings, "color_bg",    0xFF111111);
    obs_data_set_default_bool(  settings, "show_value",  true);
    obs_data_set_default_bool(  settings, "show_peak",   true);
    obs_data_set_default_double(settings, "smoothing",   0.12);

    const int defCC[CcSource::MAX_LANES] = {1, 2, 11, 64, 7, 10, 74, 71};
    for (int i = 0; i < CcSource::MAX_LANES; ++i)
        obs_data_set_default_int(settings, kCcKeys[i], defCC[i]);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
static const char *cc_get_name(void *) { return obs_module_text("CcSource.Name"); }

static void *cc_create(obs_data_t *settings, obs_source_t *source)
{
    (void)source;
    auto *s = new CcSource{};
    cc_load_settings(s, settings);
    apply_midi_port(s->midiPort);

    s->midiHandle = MidiEngine::instance().subscribe([s](const MidiEvent &ev) {
        if (s->midiPort >= 0 && (int)ev.portIndex != s->midiPort) return;
        if (ev.type != MidiEventType::ControlChange) return;
        for (int i = 0; i < CcSource::MAX_LANES; ++i) {
            if (s->ccNumbers[i] == (int)ev.param1)
                s->ccTargets[i] = ev.param2 / 127.0f;
        }
    });

    return s;
}

static void cc_destroy(void *data)
{
    auto *s = static_cast<CcSource *>(data);
    MidiEngine::instance().unsubscribe(s->midiHandle);
    delete s;
}

static void cc_update(void *data, obs_data_t *settings)
{
    auto *s = static_cast<CcSource *>(data);
    cc_load_settings(s, settings);
    apply_midi_port(s->midiPort);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick — smoothing + peak-hold logic
// ─────────────────────────────────────────────────────────────────────────────
static void cc_tick(void *data, float seconds)
{
    auto *s = static_cast<CcSource *>(data);
    MidiEngine::instance().drainQueue();

    // Exponential smoothing: alpha = 1 − smoothing^(seconds×60)
    // smoothing=0 → alpha=1 (instant), smoothing=1 → alpha=0 (frozen)
    const float alpha = 1.0f - powf(s->smoothing, seconds * 60.0f);

    constexpr float kPeakHold  = 1.5f;  // hold at peak for 1.5 s
    constexpr float kPeakDecay = 0.6f;  // fall at 0.6 units/s after hold expires

    for (int i = 0; i < CcSource::MAX_LANES; ++i) {
        // Smooth value toward target
        s->ccValues[i] += (s->ccTargets[i] - s->ccValues[i]) * alpha;
        s->ccValues[i]  = std::max(0.0f, std::min(1.0f, s->ccValues[i]));

        // Peak hold: raise peak instantly, hold, then decay under gravity
        if (s->ccValues[i] >= s->ccPeak[i]) {
            s->ccPeak   [i] = s->ccValues[i];
            s->ccPeakAge[i] = 0.0f;
        } else {
            s->ccPeakAge[i] += seconds;
            if (s->ccPeakAge[i] > kPeakHold) {
                s->ccPeak[i] = std::max(0.0f, s->ccPeak[i] - kPeakDecay * seconds);
            }
        }
    }
}

static uint32_t cc_get_width (void *data) { return static_cast<CcSource*>(data)->cx; }
static uint32_t cc_get_height(void *data) { return static_cast<CcSource*>(data)->cy; }

// ─────────────────────────────────────────────────────────────────────────────
// Render
//
// Canvas layout (H = canvas height):
//
//   ┌────────────────────────────────────────────────┐  ← y=0
//   │  value row  (showValue: 0-127 numerics)        │  ← valueH px
//   ├────────────────────────────────────────────────┤
//   │  bar area   (trough + filled bar + peak line)  │  ← barAreaH px
//   ├────────────────────────────────────────────────┤
//   │  label row  (CC name)                          │  ← labelH px
//   └────────────────────────────────────────────────┘  ← y=H
//
// Each of the N active lanes is W/N wide with gap padding.
// Bar fill uses a two-tone gradient (brighter top cap).
// Above 88 % the bar hot-zones to orange then red.
// Peak-hold: white hairline at peak position (held 1.5 s, then gravity decay).
// Tick marks at 25/50/75 % — faint, always visible.
//
// Uses gs_technique_begin/end — never gs_effect_loop.
// ─────────────────────────────────────────────────────────────────────────────
static void cc_render(void *data, gs_effect_t *effect)
{
    (void)effect;
    auto *s = static_cast<CcSource *>(data);

    const float W = (float)s->cx;
    const float H = (float)s->cy;
    const int   N = std::max(1, std::min(s->numLanes, CcSource::MAX_LANES));

    gs_effect_t    *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t    *cp    = gs_effect_get_param_by_name(solid, "color");
    gs_technique_t *tech  = gs_effect_get_technique(solid, "Solid");
    size_t          passes= gs_technique_begin(tech);

    // ── Filled rectangle ────────────────────────────────────────────────────
    auto drawRect = [&](float x, float y, float w, float h, uint32_t argb) {
        if (w < 1.0f || h < 1.0f) return;
        struct vec4 c4;
        vec4_set(&c4,
            ((argb >> 16) & 0xFF) / 255.0f,
            ((argb >>  8) & 0xFF) / 255.0f,
            ( argb        & 0xFF) / 255.0f,
            ((argb >> 24) & 0xFF) / 255.0f);
        gs_effect_set_vec4(cp, &c4);
        gs_matrix_push();
        gs_matrix_translate3f(x, y, 0.0f);
        gs_draw_sprite(nullptr, 0, (uint32_t)w, (uint32_t)h);
        gs_matrix_pop();
    };

    // ── 3×5 bitmap text ─────────────────────────────────────────────────────
    auto drawText = [&](float x, float y, float fscale,
                        uint32_t color, const char *text) {
        if (!text || !text[0]) return;
        int len = (int)strlen(text);
        for (int ci = 0; ci < len; ++ci) {
            int fi = ccFontIdx(text[ci]);
            float charX = x + (float)ci * 4.0f * fscale;
            for (int row5 = 0; row5 < 5; ++row5) {
                uint8_t bits = kCcFont[fi][row5];
                for (int bit = 0; bit < 3; ++bit) {
                    if (bits & (1u << (2 - bit))) {
                        drawRect(charX + (float)bit * fscale,
                                 y     + (float)row5 * fscale,
                                 fscale, fscale, color);
                    }
                }
            }
        }
    };

    // ── Colour helpers ───────────────────────────────────────────────────────
    auto lerp = [](uint32_t a, uint32_t b, float t) -> uint32_t {
        return lerp_argb_cc(a, b, t);
    };

    for (size_t p = 0; p < passes; ++p) {
        gs_technique_begin_pass(tech, p);

        // Background
        drawRect(0.0f, 0.0f, W, H, s->colorBg);

        // ── Layout constants ─────────────────────────────────────────────────
        // Value row: top of canvas. Label row: bottom.
        const float labelH   = std::max(14.0f, std::min(H * 0.20f, 26.0f));
        const float valueH   = s->showValue
            ? std::max(12.0f, std::min(H * 0.16f, 22.0f)) : 0.0f;
        const float barAreaH = H - labelH - valueH;

        if (barAreaH < 4.0f) {
            gs_technique_end_pass(tech);
            continue;
        }

        const float laneW = W / (float)N;
        const float gap   = std::max(1.5f, laneW * 0.06f);

        // Font scales — floor to integer pixel size for crisp rendering
        const float lblScale = std::max(1.0f, std::floor(
            std::min((laneW - gap * 2.0f) / 16.0f,
                     (labelH - 4.0f)      /  5.0f)));
        const float valScale = s->showValue
            ? std::max(1.0f, std::floor(
                std::min((laneW - gap * 2.0f) / 12.0f,
                         (valueH - 4.0f)      /  5.0f)))
            : 1.0f;

        // Thin separator line between bar area and label strip
        drawRect(0.0f, H - labelH - 1.0f, W, 1.0f,
                 lerp(s->colorBg, 0xFF888888, 0.22f));

        // ── Per-lane drawing ─────────────────────────────────────────────────
        for (int i = 0; i < N; ++i) {
            const float lx  = (float)i * laneW;
            const float bx  = lx + gap;
            const float bw  = laneW - gap * 2.0f;
            if (bw < 1.0f) continue;

            const float val   = std::max(0.0f, std::min(1.0f, s->ccValues[i]));
            const float barH  = val * barAreaH;
            const float barTop= valueH + barAreaH - barH;   // y-coord of bar top

            // ── Trough (dark well behind bar) ────────────────────────────────
            uint32_t troughColor = lerp(s->colorBg, 0xFF000000, 0.50f);
            drawRect(bx, valueH, bw, barAreaH, troughColor);

            // ── Tick marks at 25 / 50 / 75 % ────────────────────────────────
            uint32_t tickColor = lerp(troughColor, 0xFF888888, 0.22f);
            for (int t = 1; t <= 3; ++t) {
                float ty = valueH + barAreaH - (float)t * barAreaH * 0.25f;
                drawRect(bx, ty, bw, 1.0f, tickColor);
            }

            // ── Bar fill ─────────────────────────────────────────────────────
            if (barH >= 1.0f) {
                // Compute base and bright-cap colours
                uint32_t cBot = s->colorBar;
                uint32_t cTop = lerp(s->colorBar, 0xFFFFFFFF, 0.45f);

                // Hot zone: above 88 % → shift toward orange then red
                if (val > 0.88f) {
                    float hot = (val - 0.88f) / 0.12f;
                    cTop = lerp(cTop, 0xFFFF3300, hot);
                    cBot = lerp(cBot, 0xFFCC2200, hot * 0.55f);
                }

                // Bottom 70 % of bar in base colour
                const float splitH = barH * 0.30f;
                const float mainH  = barH - splitH;
                if (mainH  >= 1.0f) drawRect(bx, barTop + splitH, bw, mainH,  cBot);
                if (splitH >= 1.0f) drawRect(bx, barTop,           bw, splitH, cTop);

                // Left-edge highlight: 2 px of brighter colour for a 3-D bevel
                drawRect(bx, barTop, 2.0f, barH,
                         lerp(cTop, 0xFFFFFFFF, 0.25f));
            }

            // ── Peak-hold hairline ───────────────────────────────────────────
            if (s->showPeak && s->ccPeak[i] > 0.02f) {
                float peakY = valueH + barAreaH - s->ccPeak[i] * barAreaH;
                uint32_t peakCol = (s->ccPeak[i] > 0.88f) ? 0xDDFF4400 : 0xDDFFFFFF;
                drawRect(bx, peakY - 1.5f, bw, 3.0f, peakCol);
            }

            // ── Value overlay (top row) ───────────────────────────────────────
            if (s->showValue && valueH >= 12.0f) {
                int  raw = (int)roundf(val * 127.0f);
                char numStr[8];
                snprintf(numStr, sizeof(numStr), "%d", raw);
                int   nlen = (int)strlen(numStr);
                float nw   = ((float)nlen * 4.0f - 1.0f) * valScale;
                float nx   = bx + (bw - nw) * 0.5f;
                float ny   = (valueH - 5.0f * valScale) * 0.5f;
                uint32_t numCol = val > 0.88f ? 0xFFFF8800
                                : val > 0.01f ? 0xFFCCCCCC
                                :               0xFF555555;
                drawText(nx, ny, valScale, numCol, numStr);
            }

            // ── Lane label (bottom row) ───────────────────────────────────────
            {
                const char *lbl  = s->ccLabels[i];
                int   llen  = (int)strlen(lbl);
                float lw    = ((float)llen * 4.0f - 1.0f) * lblScale;
                float llx   = bx + (bw - lw) * 0.5f;
                float lly   = H - labelH + (labelH - 5.0f * lblScale) * 0.5f;
                uint32_t lc = val > 0.01f ? 0xFFCCCCCC : 0xFF505050;
                drawText(llx, lly, lblScale, lc, lbl);
            }

            // ── Lane separator (faint right edge, not after last lane) ────────
            if (i < N - 1) {
                drawRect(lx + laneW - 1.0f, 0.0f, 1.0f, H,
                         lerp(s->colorBg, 0xFF666666, 0.14f));
            }
        }

        gs_technique_end_pass(tech);
    }

    gs_technique_end(tech);
}

// ─────────────────────────────────────────────────────────────────────────────
// Properties panel
// ─────────────────────────────────────────────────────────────────────────────
static obs_properties_t *cc_properties(void *)
{
    obs_properties_t *props = obs_properties_create();

    // Canvas size
    obs_properties_add_int(props, "canvas_w", "Canvas width",   80, 3840, 1);
    obs_properties_add_int(props, "canvas_h", "Canvas height",  40, 2160, 1);
    obs_properties_add_int(props, "num_lanes","Active lanes",    1,    8, 1);

    // MIDI port
    obs_property_t *port_list = obs_properties_add_list(props, "midi_port",
        "MIDI Input Device", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(port_list, "Disabled", -1);
    for (int i = 0; i < (int)MidiEngine::instance().portNames().size(); ++i)
        obs_property_list_add_int(port_list,
            MidiEngine::instance().portNames()[i].c_str(), i);

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

    // Per-lane CC numbers
    static const char *laneNames[CcSource::MAX_LANES] = {
        "Lane 1 CC", "Lane 2 CC", "Lane 3 CC", "Lane 4 CC",
        "Lane 5 CC", "Lane 6 CC", "Lane 7 CC", "Lane 8 CC",
    };
    for (int i = 0; i < CcSource::MAX_LANES; ++i)
        obs_properties_add_int(props, kCcKeys[i], laneNames[i], 0, 127, 1);

    // Visuals
    obs_properties_add_color(props,        "color_bar",  "Bar colour");
    obs_properties_add_color(props,        "color_bg",   "Background colour");
    obs_properties_add_bool (props,        "show_value", "Show numeric value (0-127)");
    obs_properties_add_bool (props,        "show_peak",  "Show peak-hold line");
    obs_properties_add_float_slider(props, "smoothing",  "Smoothing (0=instant, 1=frozen)",
                                    0.0, 0.99, 0.01);
    return props;
}

// ─────────────────────────────────────────────────────────────────────────────
// Source info + registration
// ─────────────────────────────────────────────────────────────────────────────
void cc_source_register(void)
{
    static obs_source_info info{};
    info.id             = "midi_cc_source";
    info.type           = OBS_SOURCE_TYPE_INPUT;
    info.output_flags   = OBS_SOURCE_VIDEO;
    info.get_name       = cc_get_name;
    info.create         = cc_create;
    info.destroy        = cc_destroy;
    info.get_defaults   = cc_defaults;
    info.get_properties = cc_properties;
    info.update         = cc_update;
    info.video_tick     = cc_tick;
    info.video_render   = cc_render;
    info.get_width      = cc_get_width;
    info.get_height     = cc_get_height;
    obs_register_source(&info);
}
