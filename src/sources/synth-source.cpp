// synth-source.cpp — Synth Patch Display (MIDI) for OBS Studio
// Phase 6: Hardware synth panel visualizer
// Models: Behringer DeepMind 12 · Korg DSS-1 · Alesis QS7.1 · Yamaha PSR-540

#include "synth-source.hpp"
#include "../midi-engine.hpp"
#include "../plugin-support.h"
#include <obs-module.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>

static const float kPi = 3.14159265f;

// ─────────────────────────────────────────────────────────────────────────────
// 3×5 pixel bitmap font (same table used across all obs-midi-viz sources)
// Each byte = one row; bit2=left, bit1=centre, bit0=right.
// ─────────────────────────────────────────────────────────────────────────────
static const uint8_t kSynFont[38][5] = {
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

static int synFontIdx(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    char u = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    if (u >= 'A' && u <= 'Z') return 10 + (u - 'A');
    if (c == '-') return 36;
    return 37;
}

// ─────────────────────────────────────────────────────────────────────────────
// Model identifiers
// ─────────────────────────────────────────────────────────────────────────────
enum class SynthModel : int {
    DeepMind12   = 0,
    KorgDSS1     = 1,
    AlesisQS71   = 2,
    YamahaPSR540 = 3,
};

// params[] index layout per model (all normalised 0.0–1.0)
// ── DeepMind 12 ─────
namespace DM12 { enum : int {
    VCO1_OCT=0, VCO1_TUN, VCO1_PW, VCO1_MIX,
    VCO2_OCT,   VCO2_TUN, VCO2_PW, VCO2_MIX,
    FILT_CUT,   FILT_RES, FILT_ENV, FILT_KEY,
    ENV1_A, ENV1_D, ENV1_S, ENV1_R,
    ENV2_A, ENV2_D, ENV2_S, ENV2_R,
    LFO1_RT, LFO1_DP, LFO1_WV,
    LFO2_RT, LFO2_DP, LFO2_WV,
    FX_CHO, FX_REV, FX_DLY,
    ARP_ON, ARP_MD, ARP_RT,
    COUNT
}; }
// ── Korg DSS-1 ──────
// The DSS-1 is a sampling synthesizer — it has no DCOs.
// Sources are PCM samples loaded from disk/cartridge, not analog oscillators.
// MIDI SysEx: F0 42 [0x30|ch] 02 [func] [data] F7  (device ID = 0x02)
namespace DSS1 { enum : int {
    // Source section (sample-based, not DCO)
    SRC_OCT=0,    // octave transpose (-3..+3 stored 0-6, centre=3)
    SRC_TUNE,     // semitone tuning (0-12)
    SRC_WV,       // waveform / sample number (0-63)
    SRC_PORTA,    // portamento time (0-99)
    // VDF — Voltage Dependent Filter (LPF)
    VDF_CUT,      // cutoff frequency (0-99)
    VDF_RES,      // resonance / EG intensity (0-99)
    VDF_ENV,      // EG1→VDF amount, bipolar (0-99, 50=off)
    VDF_KEY,      // keyboard tracking (0-3: 0=off)
    VDF_LVL,      // filter level (0-99)
    // VDA — Voltage Dependent Amplifier
    VDA_LVL,      // output level (0-99)
    VDA_ENV,      // EG2 → VDA amount (0-99)
    // EG1 — Filter Envelope (A/D/S/R in 0-99 range)
    EG1_A, EG1_D, EG1_S, EG1_R,
    // EG2 — Amplifier Envelope
    EG2_A, EG2_D, EG2_S, EG2_R,
    // LFO
    LFO_SPD,      // speed (0-99)
    LFO_DLY,      // onset delay (0-99)
    LFO_SHP,      // shape: 0=tri 1=saw 2=sq 3=random
    LFO_PIT,      // pitch mod depth (0-99)
    LFO_VDF,      // VDF mod depth (0-99)
    LFO_VDA,      // VDA mod depth (0-99)
    COUNT
}; }
// ── Alesis QS7.1 ────
namespace QS71 { enum : int {
    E1_LVL=0, E1_PAN, E1_PITCH, E1_TUN,
    E1_FC, E1_FR, E1_FENV,
    E1_AA, E1_AD, E1_AS, E1_AR,
    E1_MA, E1_MD, E1_MS, E1_MR,
    E1_LRT, E1_LDP,
    E2_LVL=20, E2_PAN, E2_FC, E2_FR, E2_AA, E2_AD, E2_AS, E2_AR,
    E3_LVL=30, E3_PAN, E3_FC, E3_FR, E3_AA, E3_AD, E3_AS, E3_AR,
    E4_LVL=40, E4_PAN, E4_FC, E4_FR, E4_AA, E4_AD, E4_AS, E4_AR,
    FX1_LVL=50, FX2_LVL,
    COUNT=60
}; }
// ── Yamaha PSR-540 ──
namespace PSR540 { enum : int {
    MVOL=0, MPAN, VOICE, REV_T, REV_L, CHO_T, CHO_L, HAR_T, HAR_L,
    TRANSPOSE, TEMPO, STYLE,
    COUNT
}; }

// ─────────────────────────────────────────────────────────────────────────────
// Per-instance state
// ─────────────────────────────────────────────────────────────────────────────
struct SynthSource {
    SynthModel model = SynthModel::DeepMind12;

    float  params[256] = {};
    char   patchName[32] = {};
    bool   paramsValid = false;         // true once a SysEx patch dump parsed

    std::vector<uint8_t> sysexBuf;      // accumulates a live SysEx message

    int      midiPort    = -1;
    int      midiChannel = 0;           // 0–15 (0 = channel 1)
    uint32_t midiHandle  = 0;
    uint32_t cx = 720;
    uint32_t cy = 400;

    uint32_t colorBg = 0xFF111111;

    SynthSource() { snprintf(patchName, sizeof(patchName), "---"); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Default (demo) parameter values — make panels look interesting on first load
// ─────────────────────────────────────────────────────────────────────────────
static void synth_set_defaults(SynthSource *s)
{
    float *p = s->params;
    switch (s->model) {
    case SynthModel::DeepMind12:
        p[DM12::VCO1_OCT]=0.50f; p[DM12::VCO1_TUN]=0.50f; p[DM12::VCO1_PW]=0.50f; p[DM12::VCO1_MIX]=0.75f;
        p[DM12::VCO2_OCT]=0.50f; p[DM12::VCO2_TUN]=0.53f; p[DM12::VCO2_PW]=0.50f; p[DM12::VCO2_MIX]=0.25f;
        p[DM12::FILT_CUT]=0.55f; p[DM12::FILT_RES]=0.30f; p[DM12::FILT_ENV]=0.60f; p[DM12::FILT_KEY]=0.50f;
        p[DM12::ENV1_A]=0.15f;  p[DM12::ENV1_D]=0.45f;  p[DM12::ENV1_S]=0.55f;  p[DM12::ENV1_R]=0.50f;
        p[DM12::ENV2_A]=0.10f;  p[DM12::ENV2_D]=0.00f;  p[DM12::ENV2_S]=1.00f;  p[DM12::ENV2_R]=0.30f;
        p[DM12::LFO1_RT]=0.25f; p[DM12::LFO1_DP]=0.20f; p[DM12::LFO1_WV]=0.00f;
        p[DM12::LFO2_RT]=0.40f; p[DM12::LFO2_DP]=0.15f; p[DM12::LFO2_WV]=0.20f;
        p[DM12::FX_CHO]=0.45f;  p[DM12::FX_REV]=0.60f;  p[DM12::FX_DLY]=0.30f;
        p[DM12::ARP_ON]=0.00f;  p[DM12::ARP_MD]=0.00f;  p[DM12::ARP_RT]=0.35f;
        break;
    case SynthModel::KorgDSS1:
        p[DSS1::SRC_OCT]=0.50f; p[DSS1::SRC_TUNE]=0.50f;
        p[DSS1::SRC_WV]=0.12f;  p[DSS1::SRC_PORTA]=0.00f;
        p[DSS1::VDF_CUT]=0.65f; p[DSS1::VDF_RES]=0.25f;
        p[DSS1::VDF_ENV]=0.55f; p[DSS1::VDF_KEY]=0.33f; p[DSS1::VDF_LVL]=0.80f;
        p[DSS1::VDA_LVL]=0.85f; p[DSS1::VDA_ENV]=0.75f;
        p[DSS1::EG1_A]=0.10f;  p[DSS1::EG1_D]=0.35f;  p[DSS1::EG1_S]=0.65f;  p[DSS1::EG1_R]=0.40f;
        p[DSS1::EG2_A]=0.05f;  p[DSS1::EG2_D]=0.00f;  p[DSS1::EG2_S]=1.00f;  p[DSS1::EG2_R]=0.30f;
        p[DSS1::LFO_SPD]=0.28f; p[DSS1::LFO_DLY]=0.45f; p[DSS1::LFO_SHP]=0.00f;
        p[DSS1::LFO_PIT]=0.12f; p[DSS1::LFO_VDF]=0.18f; p[DSS1::LFO_VDA]=0.00f;
        break;
    case SynthModel::AlesisQS71:
        p[QS71::E1_LVL]=1.00f; p[QS71::E1_PAN]=0.50f; p[QS71::E1_PITCH]=0.50f; p[QS71::E1_TUN]=0.50f;
        p[QS71::E1_FC]=0.70f;  p[QS71::E1_FR]=0.20f;  p[QS71::E1_FENV]=0.40f;
        p[QS71::E1_AA]=0.10f;  p[QS71::E1_AD]=0.20f;  p[QS71::E1_AS]=0.80f;  p[QS71::E1_AR]=0.30f;
        p[QS71::E1_MA]=0.30f;  p[QS71::E1_MD]=0.40f;  p[QS71::E1_MS]=0.30f;  p[QS71::E1_MR]=0.40f;
        p[QS71::E1_LRT]=0.30f; p[QS71::E1_LDP]=0.15f;
        p[QS71::E2_LVL]=0.70f; p[QS71::E2_PAN]=0.30f; p[QS71::E2_FC]=0.60f; p[QS71::E2_FR]=0.15f;
        p[QS71::E2_AA]=0.05f;  p[QS71::E2_AD]=0.25f;  p[QS71::E2_AS]=0.75f; p[QS71::E2_AR]=0.35f;
        p[QS71::E3_LVL]=0.50f; p[QS71::E3_PAN]=0.70f; p[QS71::E3_FC]=0.80f; p[QS71::E3_FR]=0.10f;
        p[QS71::E3_AA]=0.00f;  p[QS71::E3_AD]=0.40f;  p[QS71::E3_AS]=0.50f; p[QS71::E3_AR]=0.45f;
        p[QS71::E4_LVL]=0.30f; p[QS71::E4_PAN]=0.50f; p[QS71::E4_FC]=0.50f; p[QS71::E4_FR]=0.05f;
        p[QS71::E4_AA]=0.20f;  p[QS71::E4_AD]=0.50f;  p[QS71::E4_AS]=0.40f; p[QS71::E4_AR]=0.60f;
        p[QS71::FX1_LVL]=0.50f; p[QS71::FX2_LVL]=0.30f;
        break;
    case SynthModel::YamahaPSR540:
        p[PSR540::MVOL]=0.80f;  p[PSR540::MPAN]=0.50f;  p[PSR540::VOICE]=0.20f;
        p[PSR540::REV_T]=0.20f; p[PSR540::REV_L]=0.40f;
        p[PSR540::CHO_T]=0.00f; p[PSR540::CHO_L]=0.30f;
        p[PSR540::HAR_T]=0.00f; p[PSR540::HAR_L]=0.00f;
        p[PSR540::TRANSPOSE]=0.50f; p[PSR540::TEMPO]=0.375f;
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SysEx parsers — extract normalised params from raw SysEx messages
// ─────────────────────────────────────────────────────────────────────────────
static bool dm12_parse(const std::vector<uint8_t> &m, float *p, char *name, int nameSz)
{
    // Behringer DeepMind 12 bulk program dump:
    // F0 00 20 32 28 [devId] 01 [pname×8] [params…] F7
    if (m.size() < 20 || m[0]!=0xF0 || m[1]!=0x00 || m[2]!=0x20 || m[3]!=0x32 || m[6]!=0x01)
        return false;
    auto b = [&](size_t i) { return (i < m.size()) ? m[i] / 127.0f : 0.5f; };
    // Patch name: 8 chars starting at byte 7
    if (name) {
        int ni = 0;
        for (int i = 7; i < 15 && (size_t)i < m.size()-1; ++i) {
            uint8_t c = m[i];
            if (c >= 32 && c < 127) name[ni++] = (char)c;
        }
        name[ni] = '\0';
    }
    const size_t B = 15; // params start after header + name
    p[DM12::VCO1_OCT]=b(B+0); p[DM12::VCO1_TUN]=b(B+1); p[DM12::VCO1_PW]=b(B+2); p[DM12::VCO1_MIX]=b(B+3);
    p[DM12::VCO2_OCT]=b(B+8); p[DM12::VCO2_TUN]=b(B+9); p[DM12::VCO2_PW]=b(B+10);p[DM12::VCO2_MIX]=b(B+11);
    p[DM12::FILT_CUT]=b(B+16);p[DM12::FILT_RES]=b(B+17);p[DM12::FILT_ENV]=b(B+18);p[DM12::FILT_KEY]=b(B+19);
    p[DM12::ENV1_A]=b(B+24); p[DM12::ENV1_D]=b(B+25); p[DM12::ENV1_S]=b(B+26); p[DM12::ENV1_R]=b(B+27);
    p[DM12::ENV2_A]=b(B+32); p[DM12::ENV2_D]=b(B+33); p[DM12::ENV2_S]=b(B+34); p[DM12::ENV2_R]=b(B+35);
    p[DM12::LFO1_RT]=b(B+40);p[DM12::LFO1_DP]=b(B+41);
    p[DM12::LFO1_WV]=(m.size()>B+42) ? (m[B+42]%6)/5.0f : 0.0f;
    p[DM12::LFO2_RT]=b(B+43);p[DM12::LFO2_DP]=b(B+44);
    p[DM12::LFO2_WV]=(m.size()>B+45) ? (m[B+45]%6)/5.0f : 0.0f;
    p[DM12::FX_CHO]=b(B+50); p[DM12::FX_REV]=b(B+51); p[DM12::FX_DLY]=b(B+52);
    p[DM12::ARP_ON]=(m.size()>B+60 && m[B+60]>0) ? 1.0f : 0.0f;
    p[DM12::ARP_MD]=(m.size()>B+61) ? (m[B+61]%5)/4.0f : 0.0f;
    p[DM12::ARP_RT]=b(B+62);
    return true;
}

static bool dss1_parse(const std::vector<uint8_t> &m, float *p, char *name, int nameSz)
{
    // Korg DSS-1 Program Parameter Data:
    // F0 42 [0x30|ch] 02 40 [name:10] [data...] F7
    //   Byte 0: F0  Byte 1: 42 (Korg)  Byte 2: 0x30+ch  Byte 3: 02 (DSS-1 device ID)
    //   Byte 4: function (0x40 = Program Param Data)
    //   Bytes 5-14: patch name (10 ASCII chars)
    //   Bytes 15+: parameter data (0-99 range per Korg DSS-1 manual, pp.5-8)
    if (m.size() < 20 || m[0]!=0xF0 || m[1]!=0x42 || m[3]!=0x02) return false;

    // normalise: DSS-1 uses 0-99 range for most continuous params
    auto b99 = [&](size_t i) -> float {
        return (i < m.size()-1) ? (float)m[i] / 99.0f : 0.5f;
    };
    auto b63 = [&](size_t i) -> float {
        return (i < m.size()-1) ? (float)m[i] / 63.0f : 0.0f;
    };
    auto b3  = [&](size_t i) -> float {
        return (i < m.size()-1) ? (float)(m[i] % 4) / 3.0f : 0.0f;
    };

    // Program name (10 chars at bytes 5-14)
    if (name) {
        int ni = 0;
        for (int i = 5; i < 15 && (size_t)i < m.size()-1; ++i) {
            uint8_t c = m[i];
            if (c >= 32 && c < 127) name[ni++] = (char)c;
        }
        name[ni] = '\0';
    }

    // Parameter data starts at byte 15 (after 5-byte header + 10-byte name)
    const size_t B = 15;
    // Source section (manual p.5: Octave, Semitone, Waveform, Portamento)
    p[DSS1::SRC_OCT]  = b99(B+0);        // 0-6, centre=3
    p[DSS1::SRC_TUNE] = b99(B+1);        // 0-12 semitones
    p[DSS1::SRC_WV]   = b63(B+3);        // waveform/sample 0-63
    p[DSS1::SRC_PORTA] = b99(B+4);       // portamento time

    // EG1 — Filter envelope (manual p.6: A/D/S/R, each 0-99)
    p[DSS1::EG1_A]=b99(B+5); p[DSS1::EG1_D]=b99(B+6);
    p[DSS1::EG1_S]=b99(B+7); p[DSS1::EG1_R]=b99(B+8);

    // EG2 — Amp envelope (manual p.6)
    p[DSS1::EG2_A]=b99(B+10); p[DSS1::EG2_D]=b99(B+11);
    p[DSS1::EG2_S]=b99(B+12); p[DSS1::EG2_R]=b99(B+13);

    // VDF — Voltage Dependent Filter (manual p.7: Cutoff/Resonance/EG/KeyTrack/Level)
    p[DSS1::VDF_CUT]=b99(B+15); p[DSS1::VDF_RES]=b99(B+16);
    p[DSS1::VDF_ENV]=b99(B+17); // bipolar EG amount; 50=centre
    p[DSS1::VDF_KEY]=b3 (B+18); // 0-3: off/half/full/inverted
    p[DSS1::VDF_LVL]=b99(B+19);

    // VDA — Voltage Dependent Amplifier (manual p.7)
    p[DSS1::VDA_LVL]=b99(B+20); p[DSS1::VDA_ENV]=b99(B+21);

    // LFO (manual p.8: Speed/Delay/Shape/Pit/VDF/VDA depths)
    p[DSS1::LFO_SPD]=b99(B+22); p[DSS1::LFO_DLY]=b99(B+23);
    p[DSS1::LFO_SHP]=b3 (B+24); // 0=tri 1=saw 2=sq 3=random
    p[DSS1::LFO_PIT]=b99(B+25); p[DSS1::LFO_VDF]=b99(B+26);
    p[DSS1::LFO_VDA]=b99(B+27);

    return true;
}

static bool qs71_parse(const std::vector<uint8_t> &m, float *p, char *name, int nameSz)
{
    // Alesis QS series: F0 00 00 0E [model=0x0E for QS7] [func] [data] F7
    if (m.size() < 12 || m[0]!=0xF0 || m[1]!=0x00 || m[2]!=0x00 || m[3]!=0x0E) return false;
    auto b = [&](size_t i) { return (i < m.size()) ? m[i] / 127.0f : 0.5f; };
    if (name && m.size() > 16) {
        int ni = 0;
        for (int i = 6; i < 14 && (size_t)i < m.size()-1; ++i) {
            uint8_t c = m[i];
            if (c >= 32 && c < 127) name[ni++] = (char)c;
        }
        name[ni] = '\0';
    }
    const size_t B = 14;
    // Element 1
    p[QS71::E1_LVL]=b(B+0); p[QS71::E1_PAN]=b(B+1); p[QS71::E1_PITCH]=b(B+2); p[QS71::E1_TUN]=b(B+3);
    p[QS71::E1_FC]=b(B+4);  p[QS71::E1_FR]=b(B+5);  p[QS71::E1_FENV]=b(B+6);
    p[QS71::E1_AA]=b(B+8);  p[QS71::E1_AD]=b(B+9);  p[QS71::E1_AS]=b(B+10); p[QS71::E1_AR]=b(B+11);
    p[QS71::E1_MA]=b(B+12); p[QS71::E1_MD]=b(B+13); p[QS71::E1_MS]=b(B+14); p[QS71::E1_MR]=b(B+15);
    p[QS71::E1_LRT]=b(B+16); p[QS71::E1_LDP]=b(B+17);
    // Elements 2-4 follow at B+20, B+40, B+60
    for (int e = 1; e < 4; ++e) {
        int base = e * 20;
        int pi   = (e == 1) ? QS71::E2_LVL : (e == 2 ? QS71::E3_LVL : QS71::E4_LVL);
        p[pi+0]=b(B+base+0); p[pi+1]=b(B+base+1); p[pi+2]=b(B+base+4); p[pi+3]=b(B+base+5);
        p[pi+4]=b(B+base+8); p[pi+5]=b(B+base+9); p[pi+6]=b(B+base+10);p[pi+7]=b(B+base+11);
    }
    p[QS71::FX1_LVL]=b(B+80); p[QS71::FX2_LVL]=b(B+81);
    return true;
}

static bool psr540_parse(const std::vector<uint8_t> &m, float *p, char *name, int nameSz)
{
    // Yamaha XG/PSR style: F0 43 [dev] 7E 00 00 00 [data] F7 (bulk dump) or CC-based
    if (m.size() < 8 || m[0]!=0xF0 || m[1]!=0x43) return false;
    auto b = [&](size_t i) { return (i < m.size()) ? m[i] / 127.0f : 0.5f; };
    if (name) { snprintf(name, nameSz, "XG VOICE"); }
    const size_t B = 7;
    p[PSR540::MVOL]=b(B+0);   p[PSR540::MPAN]=b(B+1);
    p[PSR540::REV_T]=b(B+2);  p[PSR540::REV_L]=b(B+3);
    p[PSR540::CHO_T]=b(B+4);  p[PSR540::CHO_L]=b(B+5);
    p[PSR540::TRANSPOSE]=(m.size()>B+6) ? (m[B+6]+12)/24.0f : 0.5f;
    return true;
}

// Dispatch SysEx parsing to the right model
static void synth_handle_sysex(SynthSource *s, const std::vector<uint8_t> &raw)
{
    if (raw.size() < 4 || raw[0] != 0xF0) return;
    bool ok = false;
    switch (s->model) {
        case SynthModel::DeepMind12:   ok = dm12_parse(raw, s->params, s->patchName, sizeof(s->patchName)); break;
        case SynthModel::KorgDSS1:     ok = dss1_parse(raw, s->params, s->patchName, sizeof(s->patchName)); break;
        case SynthModel::AlesisQS71:   ok = qs71_parse(raw, s->params, s->patchName, sizeof(s->patchName)); break;
        case SynthModel::YamahaPSR540: ok = psr540_parse(raw, s->params, s->patchName, sizeof(s->patchName)); break;
    }
    if (ok) s->paramsValid = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings helpers
// ─────────────────────────────────────────────────────────────────────────────
static void synth_load_settings(SynthSource *s, obs_data_t *settings)
{
    s->model      = (SynthModel)(int)obs_data_get_int(settings, "model");
    s->midiPort   = (int)obs_data_get_int(settings, "midi_port");
    s->midiChannel= (int)obs_data_get_int(settings, "midi_ch");
    s->cx         = (uint32_t)std::max(200LL, obs_data_get_int(settings, "canvas_w"));
    s->cy         = (uint32_t)std::max(100LL, obs_data_get_int(settings, "canvas_h"));
}

static void synth_defaults(obs_data_t *settings)
{
    obs_data_set_default_int(settings, "model",    0);
    obs_data_set_default_int(settings, "midi_port",-1);
    obs_data_set_default_int(settings, "midi_ch",   0);
    obs_data_set_default_int(settings, "canvas_w", 720);
    obs_data_set_default_int(settings, "canvas_h", 400);
}

// ─────────────────────────────────────────────────────────────────────────────
// OBS lifecycle
// ─────────────────────────────────────────────────────────────────────────────
static const char *synth_get_name(void *) { return obs_module_text("SynthSource.Name"); }

static void *synth_create(obs_data_t *settings, obs_source_t *src)
{
    (void)src;
    auto *s = new SynthSource{};
    synth_load_settings(s, settings);
    synth_set_defaults(s);

    if (s->midiPort >= 0)
        MidiEngine::instance().openPort(s->midiPort);

    s->midiHandle = MidiEngine::instance().subscribe([s](const MidiEvent &ev) {
        if (s->midiPort >= 0 && ev.portIndex != s->midiPort) return;

        // Full SysEx patch dump
        if (ev.type == MidiEventType::Other && !ev.raw.empty())
            synth_handle_sysex(s, ev.raw);

        // Real-time CC updates for the most important parameters
        if (ev.type == MidiEventType::ControlChange &&
            (int)ev.channel == s->midiChannel)
        {
            switch (s->model) {
            case SynthModel::DeepMind12:
                switch (ev.param1) {
                    case 74: s->params[DM12::FILT_CUT] = ev.param2/127.0f; break;
                    case 71: s->params[DM12::FILT_RES] = ev.param2/127.0f; break;
                    case  1: s->params[DM12::LFO1_DP]  = ev.param2/127.0f; break;
                    case  7: s->params[DM12::VCO1_MIX] = ev.param2/127.0f; break;
                }
                break;
            case SynthModel::KorgDSS1:
                // DSS-1 responds to standard CCs for real-time control
                switch (ev.param1) {
                    case  1: s->params[DSS1::LFO_PIT] = ev.param2/127.0f; break; // Mod wheel → LFO pitch depth
                    case  7: s->params[DSS1::VDA_LVL] = ev.param2/127.0f; break; // Volume → VDA level
                    case 65: s->params[DSS1::SRC_PORTA]= ev.param2/127.0f; break; // Portamento switch
                    case 71: s->params[DSS1::VDF_RES] = ev.param2/127.0f; break; // CC71 → VDF resonance
                    case 74: s->params[DSS1::VDF_CUT] = ev.param2/127.0f; break; // CC74 → VDF cutoff
                }
                break;
            default: break;
            }
        }
    });
    return s;
}

static void synth_destroy(void *data)
{
    auto *s = static_cast<SynthSource *>(data);
    MidiEngine::instance().unsubscribe(s->midiHandle);
    delete s;
}

static void synth_update(void *data, obs_data_t *settings)
{
    auto *s = static_cast<SynthSource *>(data);
    SynthModel prevModel = s->model;
    synth_load_settings(s, settings);
    if (s->midiPort >= 0) MidiEngine::instance().openPort(s->midiPort);
    // Reset params if model changed
    if (prevModel != s->model) {
        memset(s->params, 0, sizeof(s->params));
        synth_set_defaults(s);
        s->paramsValid = false;
        snprintf(s->patchName, sizeof(s->patchName), "---");
    }
}

static void synth_tick(void *data, float seconds)
{
    (void)seconds;
    auto *s = static_cast<SynthSource *>(data);
    MidiEngine::instance().drainQueue();
}

static uint32_t synth_get_width (void *data) { return static_cast<SynthSource*>(data)->cx; }
static uint32_t synth_get_height(void *data) { return static_cast<SynthSource*>(data)->cy; }

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t lerp_argb_syn(uint32_t a, uint32_t b, float t)
{
    auto l = [t](uint32_t ca, uint32_t cb) -> uint32_t {
        return (uint32_t)((float)ca + ((float)cb - (float)ca) * t);
    };
    return (l((a>>24)&0xFF,(b>>24)&0xFF)<<24)
         | (l((a>>16)&0xFF,(b>>16)&0xFF)<<16)
         | (l((a>> 8)&0xFF,(b>> 8)&0xFF)<< 8)
         |  l( a     &0xFF, b     &0xFF);
}

static void synth_render(void *data, gs_effect_t *effect)
{
    (void)effect;
    auto *s = static_cast<SynthSource *>(data);

    const float W = (float)s->cx;
    const float H = (float)s->cy;

    gs_effect_t    *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t    *cp    = gs_effect_get_param_by_name(solid, "color");
    gs_technique_t *tech  = gs_effect_get_technique(solid, "Solid");
    size_t          passes= gs_technique_begin(tech);

    // ── Core draw helpers ────────────────────────────────────────────────────
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
            int fi = synFontIdx(text[ci]);
            float cx2 = x + (float)ci * 4.0f * sc;
            for (int row = 0; row < 5; ++row) {
                uint8_t bits = kSynFont[fi][row];
                for (int bit = 0; bit < 3; ++bit) {
                    if (bits & (1u << (2 - bit)))
                        drawRect(cx2 + (float)bit * sc, y + (float)row * sc, sc, sc, col);
                }
            }
        }
    };

    auto lerp = [](uint32_t a, uint32_t b, float t) -> uint32_t {
        return lerp_argb_syn(a, b, t);
    };

    // Filled circle (horizontal scan strips)
    auto fillCircle = [&](float cx2, float cy2, float r, uint32_t col) {
        for (float dy = -r; dy <= r; dy += 1.0f) {
            float dx = sqrtf(std::max(0.0f, r*r - dy*dy));
            drawRect(cx2 - dx, cy2 + dy, dx * 2.0f + 1.0f, 1.0f, col);
        }
    };

    // ── Synth-specific helpers ───────────────────────────────────────────────

    // 27-dot LED-arc knob
    auto drawSynthKnob = [&](float kx, float ky, float r,
                              float val, const char *label, uint32_t col) {
        val = std::max(0.0f, std::min(1.0f, val));
        fillCircle(kx, ky, r,        0xFF252525);
        fillCircle(kx, ky, r * 0.6f, 0xFF363636);
        for (int i = 0; i < 27; ++i) {
            float deg = 225.0f + (float)i * (270.0f / 26.0f);
            float rad = deg * kPi / 180.0f;
            float ax  = kx + (r + 5.5f) * sinf(rad);
            float ay  = ky - (r + 5.5f) * cosf(rad);
            bool  lit = ((float)i / 26.0f) <= (val + 0.02f);
            uint32_t dc = lit ? col : lerp(0xFF111111, col, 0.10f);
            drawRect(ax - 1.5f, ay - 1.5f, 3.0f, 3.0f, dc);
        }
        float ideg = 225.0f + val * 270.0f;
        float irad = ideg * kPi / 180.0f;
        float ix   = kx + r * 0.48f * sinf(irad);
        float iy   = ky - r * 0.48f * cosf(irad);
        drawRect(ix - 2.0f, iy - 2.0f, 4.0f, 4.0f, 0xFFEEEEEE);
        if (label && label[0]) {
            size_t ll = strlen(label);
            float  lw = ((float)ll * 4.0f - 1.0f) * 1.0f;
            drawText(kx - lw * 0.5f, ky + r + 5.0f, 1.0f, 0xFF666666, label);
        }
    };

    // ADSR envelope visualizer (column-by-column fill)
    auto drawEnvelope = [&](float x, float y, float w, float h,
                             float a, float d, float sv, float r,
                             uint32_t col) {
        drawRect(x, y, w, h, 0xFF0A0A0A);
        const float sHold = 0.28f;
        float total = a + d + sHold + r;
        if (total < 0.01f) total = 1.0f;
        float xD   = x + (a / total) * w;
        float xS   = xD + (d / total) * w;
        float xR   = xS + (sHold / total) * w;
        float xEnd = x + w - 1.0f;
        float yBot = y + h - 3.0f;
        float yTop = y + 3.0f;
        float ySus = yBot - sv * (h - 6.0f);
        uint32_t fillC = lerp(col & 0x00FFFFFF, col, 0.32f) | 0x55000000;
        for (float px = x; px < x + w; px += 1.0f) {
            float topY;
            if (px <= xD) {
                float t = (xD - x > 0.5f) ? (px - x) / (xD - x) : 1.0f;
                topY = yBot - t * (yBot - yTop);
            } else if (px <= xS) {
                float t = (xS - xD > 0.5f) ? (px - xD) / (xS - xD) : 1.0f;
                topY = yTop + t * (ySus - yTop);
            } else if (px <= xR) {
                topY = ySus;
            } else {
                float t = (xEnd - xR > 0.5f) ? (px - xR) / (xEnd - xR) : 1.0f;
                topY = ySus + t * (yBot - ySus);
            }
            float cH = yBot - topY;
            if (cH >= 1.0f) {
                drawRect(px, topY, 1.0f, cH, fillC);
                drawRect(px, topY, 1.0f, 2.0f, col);
            }
        }
        // Mini ADSR labels
        drawText(x + 2.0f,  y + 3.0f, 1.0f, 0xFF444444, "A");
        drawText(xD + 2.0f, y + 3.0f, 1.0f, 0xFF444444, "D");
        drawText(xS + 2.0f, y + 3.0f, 1.0f, 0xFF444444, "S");
        drawText(xR + 2.0f, y + 3.0f, 1.0f, 0xFF444444, "R");
    };

    // Section background with coloured title strip
    auto drawSectionBg = [&](float x, float y, float w, float h,
                              const char *title, uint32_t titleCol) {
        drawRect(x, y, w, h, 0xFF1C1C1C);
        drawRect(x, y, w, 18.0f, lerp(0xFF1C1C1C, titleCol, 0.22f));
        drawRect(x, y, w, 1.5f,  lerp(0xFF1C1C1C, titleCol, 0.55f));
        if (title && title[0]) {
            size_t tl = strlen(title);
            float  tw = ((float)tl * 4.0f - 1.0f) * 1.0f;
            drawText(x + (w - tw) * 0.5f, y + 6.5f, 1.0f, titleCol, title);
        }
    };

    // Horizontal level bar with label on the left
    auto drawHBar = [&](float x, float y, float w, float h,
                         float val, const char *label, uint32_t col) {
        val = std::max(0.0f, std::min(1.0f, val));
        drawRect(x, y, w, h, 0xFF0A0A0A);
        float bw = val * (w - 4.0f);
        if (bw >= 1.0f)
            drawRect(x + 2.0f, y + 2.0f, bw, h - 4.0f, col);
        if (label && label[0]) {
            drawText(x - (float)strlen(label) * 4.0f - 3.0f,
                     y + (h - 5.0f) * 0.5f, 1.0f, 0xFF777777, label);
        }
    };

    // LED indicator dot
    auto drawLED = [&](float lx, float ly, float r, bool lit, uint32_t col) {
        fillCircle(lx, ly, r, lit ? col : lerp(col, 0xFF000000, 0.82f));
        if (lit) fillCircle(lx, ly, r * 0.45f, 0xFFFFFFFF);
    };

    // ── Per-model render lambdas ─────────────────────────────────────────────
    float *p = s->params;

    auto renderDM12 = [&]() {
        // ── Header (y=0, h=36) ──────────────────────────────────────────────
        drawRect(0, 0, W, 36.0f, lerp(s->colorBg, 0xFF002233, 0.80f));
        // Title text: "DEEPMIND 12" left, patch name center, status right
        drawText(10.0f, 14.0f, 2.0f, 0xFFFF8800, "DEEPMIND 12");
        {   // Patch name centered
            size_t nl = strlen(s->patchName);
            float  nw = ((float)nl * 4.0f - 1.0f) * 2.0f;
            drawText((W - nw) * 0.5f, 14.0f, 2.0f, 0xFFCCCCCC, s->patchName);
        }
        {   // Status top-right
            const char *st = s->paramsValid ? "PATCH OK" : "NO PATCH";
            uint32_t sc = s->paramsValid ? 0xFF00CC44 : 0xFF554444;
            size_t sl = strlen(st);
            drawText(W - (float)sl * 4.0f - 8.0f, 14.0f, 1.0f, sc, st);
        }

        // ── Section constants ────────────────────────────────────────────────
        const float R1Y = 38.0f, R1H = 108.0f;
        const float R2Y = 148.0f, R2H = 108.0f;
        const float R3Y = 258.0f, R3H = 80.0f;
        const float R4Y = 340.0f, R4H = H - 340.0f;

        // Knob positions: 4 knobs in 180px (section at x=SX)
        // Knob center at y = SY + 18 + (SH-18)/2
        auto kCY = [](float sy, float sh) { return sy + 18.0f + (sh - 18.0f) * 0.5f; };

        // ── OSC row ──────────────────────────────────────────────────────────
        // VCO1 (x=0, w=180)
        drawSectionBg(0, R1Y, 180, R1H, "VCO1", 0xFF66AAFF);
        { float ky = kCY(R1Y, R1H);
          drawSynthKnob(30,  ky, 18, p[DM12::VCO1_OCT], "OCT", 0xFF66AAFF);
          drawSynthKnob(75,  ky, 18, p[DM12::VCO1_TUN], "TUN", 0xFF66AAFF);
          drawSynthKnob(120, ky, 18, p[DM12::VCO1_PW],  "PW",  0xFF66AAFF);
          drawSynthKnob(155, ky, 16, p[DM12::VCO1_MIX], "MIX", 0xFF4488CC); }

        // VCO2 (x=182, w=178)
        drawSectionBg(182, R1Y, 178, R1H, "VCO2", 0xFF66AAFF);
        { float ky = kCY(R1Y, R1H);
          drawSynthKnob(212, ky, 18, p[DM12::VCO2_OCT], "OCT", 0xFF66AAFF);
          drawSynthKnob(257, ky, 18, p[DM12::VCO2_TUN], "TUN", 0xFF66AAFF);
          drawSynthKnob(302, ky, 18, p[DM12::VCO2_PW],  "PW",  0xFF66AAFF);
          drawSynthKnob(337, ky, 16, p[DM12::VCO2_MIX], "MIX", 0xFF4488CC); }

        // FILTER (x=362, w=178)
        drawSectionBg(362, R1Y, 178, R1H, "FILTER", 0xFF0088FF);
        { float ky = kCY(R1Y, R1H);
          drawSynthKnob(392, ky, 18, p[DM12::FILT_CUT], "CUT", 0xFF0088FF);
          drawSynthKnob(437, ky, 18, p[DM12::FILT_RES], "RES", 0xFF0088FF);
          drawSynthKnob(482, ky, 18, p[DM12::FILT_ENV], "ENV", 0xFF44AAFF);
          drawSynthKnob(525, ky, 16, p[DM12::FILT_KEY], "KEY", 0xFF2266AA); }

        // AMP (x=542, w=178)
        drawSectionBg(542, R1Y, 178, R1H, "AMP", 0xFFAADD55);
        { float ky = kCY(R1Y, R1H);
          drawSynthKnob(599, ky, 22, p[DM12::VCO1_MIX], "VOL", 0xFFAADD55);
          drawSynthKnob(670, ky, 22, 0.50f,              "PAN", 0xFF88AA44); }

        // ── ENV row ──────────────────────────────────────────────────────────
        // ENV1 Filter (x=0, w=360)
        drawSectionBg(0, R2Y, 360, R2H, "FILTER ENV", 0xFF0088FF);
        drawEnvelope(4, R2Y+18+2, 352, R2H-22,
                     p[DM12::ENV1_A], p[DM12::ENV1_D], p[DM12::ENV1_S], p[DM12::ENV1_R],
                     0xFF0088FF);

        // ENV2 Amp (x=362, w=358)
        drawSectionBg(362, R2Y, 358, R2H, "AMP ENV", 0xFF00CC44);
        drawEnvelope(366, R2Y+18+2, 350, R2H-22,
                     p[DM12::ENV2_A], p[DM12::ENV2_D], p[DM12::ENV2_S], p[DM12::ENV2_R],
                     0xFF00CC44);

        // ── LFO + FX row ─────────────────────────────────────────────────────
        // LFO1 (x=0, w=180)
        drawSectionBg(0, R3Y, 180, R3H, "LFO1", 0xFFAA44FF);
        { float ky = R3Y + 18.0f + (R3H - 18.0f) * 0.5f;
          drawSynthKnob(35,  ky, 16, p[DM12::LFO1_RT], "RT",  0xFFAA44FF);
          drawSynthKnob(90,  ky, 16, p[DM12::LFO1_DP], "DP",  0xFFAA44FF);
          drawSynthKnob(145, ky, 16, p[DM12::LFO1_WV], "WV",  0xFF7733CC); }

        // LFO2 (x=182, w=178)
        drawSectionBg(182, R3Y, 178, R3H, "LFO2", 0xFFAA44FF);
        { float ky = R3Y + 18.0f + (R3H - 18.0f) * 0.5f;
          drawSynthKnob(217, ky, 16, p[DM12::LFO2_RT], "RT",  0xFFAA44FF);
          drawSynthKnob(272, ky, 16, p[DM12::LFO2_DP], "DP",  0xFFAA44FF);
          drawSynthKnob(327, ky, 16, p[DM12::LFO2_WV], "WV",  0xFF7733CC); }

        // FX (x=362, w=358)
        drawSectionBg(362, R3Y, 358, R3H, "FX", 0xFFFF4466);
        { float by = R3Y + 24.0f;
          drawHBar(422, by,      280, 14, p[DM12::FX_CHO], "CHO", 0xFFFF4466);
          drawHBar(422, by+18,   280, 14, p[DM12::FX_REV], "REV", 0xFFFF8833);
          drawHBar(422, by+36,   280, 14, p[DM12::FX_DLY], "DLY", 0xFFFFDD22); }

        // ── ARP + INFO row ────────────────────────────────────────────────────
        drawSectionBg(0, R4Y, 360, R4H, "ARP", 0xFFFF8800);
        { float ly = R4Y + 18.0f + (R4H - 18.0f) * 0.5f;
          bool arpOn = p[DM12::ARP_ON] > 0.5f;
          drawLED(22, ly, 7, arpOn, 0xFFFF8800);
          drawText(35, R4Y + 6.5f + 18.0f, 1.0f, arpOn ? 0xFFFF8800 : 0xFF444444,
                   arpOn ? "ON" : "OFF");
          // ARP rate bar
          drawHBar(80, ly - 6, 240, 12, p[DM12::ARP_RT], "RT", 0xFFFF8800); }

        drawSectionBg(362, R4Y, 358, R4H, "INFO", 0xFF444444);
        { char inf[48];
          snprintf(inf, sizeof(inf), "CH %d  PORT %d", s->midiChannel + 1, s->midiPort);
          drawText(368, R4Y + 24.0f, 1.0f, 0xFF666666, inf); }
    };

    // ── Korg DSS-1 panel ─────────────────────────────────────────────────────
    // The DSS-1 is a sampling synthesizer — no DCOs. Sources are PCM waveforms.
    // Panel layout: SOURCE | VDF | VDA  / EG1 | EG2  / LFO  / INFO
    auto renderDSS1 = [&]() {
        drawRect(0, 0, W, 36.0f, lerp(s->colorBg, 0xFF001A1A, 0.85f));
        drawText(10.0f, 14.0f, 2.0f, 0xFF00CCAA, "KORG DSS-1");
        { size_t nl = strlen(s->patchName); float nw = ((float)nl*4.0f-1.0f)*2.0f;
          drawText((W-nw)*0.5f, 14.0f, 2.0f, 0xFFCCCCCC, s->patchName); }
        { const char *st = s->paramsValid ? "PATCH OK" : "NO PATCH";
          size_t sl = strlen(st);
          drawText(W-(float)sl*4.0f-8.0f, 14.0f, 1.0f,
                   s->paramsValid ? 0xFF00CC88 : 0xFF554444, st); }

        const float R1Y=38.0f, R1H=108.0f;
        const float R2Y=148.0f, R2H=108.0f;
        const float R3Y=258.0f, R3H=80.0f;
        const float R4Y=340.0f, R4H=H-340.0f;
        auto kCY = [](float sy, float sh){ return sy+18.0f+(sh-18.0f)*0.5f; };

        // ── Row 1: SOURCE | VDF | VDA ────────────────────────────────────────
        // SOURCE section (replacing DCO1/DCO2 — DSS-1 is sample-based)
        drawSectionBg(0, R1Y, 270, R1H, "SOURCE (SAMPLE)", 0xFF00CCAA);
        { float ky=kCY(R1Y,R1H);
          // Waveform number shown as a bar indicator
          drawHBar(10, R1Y+24.0f, 200, 14, p[DSS1::SRC_WV], "WV#", 0xFF00CCAA);
          drawSynthKnob(60, ky, 18, p[DSS1::SRC_OCT],  "OCT",  0xFF00CCAA);
          drawSynthKnob(130,ky, 18, p[DSS1::SRC_TUNE], "TUNE", 0xFF00CCAA);
          drawSynthKnob(200,ky, 14, p[DSS1::SRC_PORTA],"PORT", 0xFF009988); }

        // VDF — Voltage Dependent Filter
        drawSectionBg(272, R1Y, 270, R1H, "VDF (FILTER)", 0xFF0099FF);
        { float ky=kCY(R1Y,R1H);
          drawSynthKnob(312,ky,18,p[DSS1::VDF_CUT],"CUT",0xFF0099FF);
          drawSynthKnob(362,ky,18,p[DSS1::VDF_RES],"RES",0xFF0099FF);
          drawSynthKnob(412,ky,18,p[DSS1::VDF_ENV],"ENV",0xFF3399FF);
          drawSynthKnob(462,ky,16,p[DSS1::VDF_KEY],"KEY",0xFF224466);
          drawSynthKnob(512,ky,14,p[DSS1::VDF_LVL],"LVL",0xFF116688); }

        // VDA — Voltage Dependent Amplifier
        drawSectionBg(544, R1Y, 176, R1H, "VDA (AMP)", 0xFF88CC44);
        { float ky=kCY(R1Y,R1H);
          drawSynthKnob(610,ky,22,p[DSS1::VDA_LVL],"LVL",0xFF88CC44);
          drawSynthKnob(675,ky,18,p[DSS1::VDA_ENV],"ENV",0xFF55AA22); }

        // ── Row 2: EG1 (VDF) | EG2 (VDA) ────────────────────────────────────
        drawSectionBg(0,   R2Y, 360, R2H, "EG1  (VDF ENVELOPE)", 0xFF0099FF);
        drawEnvelope(4,R2Y+20,352,R2H-24,
                     p[DSS1::EG1_A],p[DSS1::EG1_D],p[DSS1::EG1_S],p[DSS1::EG1_R],0xFF0099FF);

        drawSectionBg(362, R2Y, 358, R2H, "EG2  (VDA ENVELOPE)", 0xFF88CC44);
        drawEnvelope(366,R2Y+20,350,R2H-24,
                     p[DSS1::EG2_A],p[DSS1::EG2_D],p[DSS1::EG2_S],p[DSS1::EG2_R],0xFF88CC44);

        // ── Row 3: LFO ───────────────────────────────────────────────────────
        // LFO has: speed, delay, shape, pitch/VDF/VDA depths
        drawSectionBg(0, R3Y, W, R3H, "LFO", 0xFFCC88FF);
        { float ky=R3Y+18.0f+(R3H-18.0f)*0.5f;
          drawSynthKnob( 50,ky,16,p[DSS1::LFO_SPD],"SPD",0xFFCC88FF);
          drawSynthKnob(130,ky,16,p[DSS1::LFO_DLY],"DLY",0xFF9955CC);
          drawSynthKnob(210,ky,16,p[DSS1::LFO_SHP],"SHP",0xFF9955CC);
          // Depth bars: Pitch, VDF, VDA
          float bx=290.0f, by=R3Y+24.0f;
          drawHBar(bx,    by,    130, 12, p[DSS1::LFO_PIT],"PIT",0xFFFFDD44);
          drawHBar(bx,    by+18, 130, 12, p[DSS1::LFO_VDF],"VDF",0xFF4499FF);
          drawHBar(bx,    by+36, 130, 12, p[DSS1::LFO_VDA],"VDA",0xFF88CC44); }

        // ── Row 4: INFO ──────────────────────────────────────────────────────
        drawSectionBg(0, R4Y, W, R4H, "INFO", 0xFF2A2A2A);
        { char inf[64];
          snprintf(inf,sizeof(inf),
                   "CH %d  PORT %d     SysEx: F0 42 3%Xh 02h",
                   s->midiChannel+1, s->midiPort, s->midiChannel);
          drawText(10, R4Y+20.0f, 1.0f, 0xFF555555, inf); }
    };

    // ── Alesis QS7.1 panel ───────────────────────────────────────────────────
    auto renderQS71 = [&]() {
        drawRect(0,0,W,36.0f,lerp(s->colorBg,0xFF1A0A00,0.85f));
        drawText(10.0f,14.0f,2.0f,0xFFFF6600,"ALESIS QS7.1");
        { size_t nl=strlen(s->patchName); float nw=((float)nl*4.0f-1.0f)*2.0f;
          drawText((W-nw)*0.5f,14.0f,2.0f,0xFFCCCCCC,s->patchName); }
        { const char *st=s->paramsValid?"PATCH OK":"NO PATCH";
          size_t sl=strlen(st);
          drawText(W-(float)sl*4.0f-8.0f,14.0f,1.0f,
                   s->paramsValid?0xFFFF8800:0xFF554444,st); }

        // QS7 has 4 elements — show each as a compact row
        const int EI[] = { QS71::E1_LVL, QS71::E2_LVL, QS71::E3_LVL, QS71::E4_LVL };
        const uint32_t EC[] = { 0xFFFF6600, 0xFF4499FF, 0xFF44CC66, 0xFFDD44AA };
        const char *EN[] = { "ELEM 1", "ELEM 2", "ELEM 3", "ELEM 4" };
        const float EH = (H - 36.0f) / 4.0f;

        for (int e = 0; e < 4; ++e) {
            float ey = 38.0f + (float)e * EH;
            float eh = EH - 2.0f;
            int  ei  = EI[e];
            drawSectionBg(0, ey, W, eh, EN[e], EC[e]);
            float ky = ey + 18.0f + (eh - 18.0f) * 0.5f;
            float kR = std::min(14.0f, (eh - 24.0f) * 0.5f);
            // LVL, PAN, FC, FR, AA, AD, AS, AR
            drawSynthKnob(40,  ky, kR, p[ei+0], "LVL", EC[e]);
            drawSynthKnob(85,  ky, kR, p[ei+1], "PAN", EC[e]);
            drawSynthKnob(150, ky, kR, p[ei+2], "CUT", 0xFF4488FF);
            drawSynthKnob(195, ky, kR, p[ei+3], "RES", 0xFF4488FF);
            // Mini ADSR bars
            float bx = 260.0f, bh = (eh - 26.0f) * 0.22f;
            float by0 = ky - bh * 2.2f;
            const char *adslbls[] = {"A","D","S","R"};
            for (int j = 0; j < 4; ++j) {
                float bv = p[ei+4+j];
                drawHBar(bx + (float)j*48.0f, by0, 42.0f, (bh < 8.0f ? 8.0f : bh),
                         bv, adslbls[j], EC[e]);
            }
            // LFO
            drawSynthKnob(490, ky, kR*0.9f, (e==0)?p[QS71::E1_LRT]:0.3f, "LFO", 0xFFAA44FF);
            // FX levels (only on last element row)
            if (e == 3) {
                drawHBar(560, by0,        130, 10, p[QS71::FX1_LVL], "FX1", 0xFFFFDD22);
                drawHBar(560, by0 + 14.0f,130, 10, p[QS71::FX2_LVL], "FX2", 0xFFFF8844);
            }
        }
    };

    // ── Yamaha PSR-540 panel ─────────────────────────────────────────────────
    auto renderPSR540 = [&]() {
        drawRect(0,0,W,36.0f,lerp(s->colorBg,0xFF000A18,0.90f));
        drawText(10.0f,14.0f,2.0f,0xFF2288FF,"YAMAHA PSR-540");
        { size_t nl=strlen(s->patchName); float nw=((float)nl*4.0f-1.0f)*2.0f;
          drawText((W-nw)*0.5f,14.0f,2.0f,0xFFCCCCCC,s->patchName); }
        { const char *st=s->paramsValid?"XG PATCH":"NO PATCH";
          size_t sl=strlen(st);
          drawText(W-(float)sl*4.0f-8.0f,14.0f,1.0f,
                   s->paramsValid?0xFF2288FF:0xFF554444,st); }

        // Master row
        drawSectionBg(0, 38, 360, 100, "MASTER", 0xFF2288FF);
        { float ky=88.0f;
          drawSynthKnob(60, ky,22,p[PSR540::MVOL],"VOL",0xFF2288FF);
          drawSynthKnob(150,ky,22,p[PSR540::MPAN],"PAN",0xFF2288FF);
          // Transpose display
          int tr = (int)roundf((p[PSR540::TRANSPOSE] - 0.5f) * 24.0f);
          char trbuf[8]; snprintf(trbuf, sizeof(trbuf), tr>=0?"+%d":"%d", tr);
          drawText(240,78.0f,2.0f,0xFF66AAFF,"TRNS");
          drawText(252,96.0f,2.0f,0xFFCCCCCC,trbuf); }

        drawSectionBg(362, 38, 358, 100, "TEMPO / STYLE", 0xFF44AAFF);
        { // Tempo: 60–240 BPM
          int bpm = 60 + (int)(p[PSR540::TEMPO] * 180.0f);
          char bpmbuf[12]; snprintf(bpmbuf,sizeof(bpmbuf),"%d BPM",bpm);
          drawText(370,70.0f,2.0f,0xFF44AAFF,"TEMPO");
          drawText(378,88.0f,2.0f,0xFFEEEEEE,bpmbuf); }

        // Effects row
        drawSectionBg(0, 140, 360, 90, "REVERB", 0xFFFF8844);
        { float ky=185.0f;
          drawSynthKnob(90, ky,20,p[PSR540::REV_T],"TYPE",0xFFFF8844);
          drawSynthKnob(210,ky,20,p[PSR540::REV_L],"LEVL",0xFFFF8844); }

        drawSectionBg(362,140, 358, 90, "CHORUS", 0xFFFF44CC);
        { float ky=185.0f;
          drawSynthKnob(452,ky,20,p[PSR540::CHO_T],"TYPE",0xFFFF44CC);
          drawSynthKnob(572,ky,20,p[PSR540::CHO_L],"LEVL",0xFFFF44CC); }

        // Harmony row
        drawSectionBg(0,232, 360, 84, "HARMONY", 0xFFFFDD22);
        { float ky=272.0f;
          drawSynthKnob(90,ky,20,p[PSR540::HAR_T],"TYPE",0xFFFFDD22);
          drawSynthKnob(210,ky,20,p[PSR540::HAR_L],"LEVL",0xFFFFDD22); }

        drawSectionBg(362,232, 358, 84, "MIDI / INFO", 0xFF444444);
        { char inf[48];
          snprintf(inf,sizeof(inf),"CH %d  PORT %d",s->midiChannel+1,s->midiPort);
          drawText(368,256.0f,1.0f,0xFF666666,inf);
          drawText(368,270.0f,1.0f,0xFF555555,s->paramsValid?"XG MODE":"CC MODE"); }

        // Voice bar
        drawSectionBg(0, 318, W, H-318.0f, "VOICE", 0xFF2288FF);
        { float bw = p[PSR540::VOICE] * (W - 20.0f);
          drawRect(10,336,W-20.0f,20.0f,0xFF0A0A0A);
          if(bw>1.0f) drawRect(10,336,bw,20.0f,0xFF2288FF);
          int vn = (int)(p[PSR540::VOICE]*127.0f);
          char vnbuf[16]; snprintf(vnbuf,sizeof(vnbuf),"VOICE %d/127",vn);
          size_t vl=strlen(vnbuf); float vw=((float)vl*4.0f-1.0f)*1.0f;
          drawText((W-vw)*0.5f,341.0f,1.0f,0xFF888888,vnbuf); }
    };

    // ── Dispatch ─────────────────────────────────────────────────────────────
    for (size_t pass = 0; pass < passes; ++pass) {
        gs_technique_begin_pass(tech, pass);
        drawRect(0.0f, 0.0f, W, H, s->colorBg);

        switch (s->model) {
        case SynthModel::DeepMind12:   renderDM12();   break;
        case SynthModel::KorgDSS1:     renderDSS1();   break;
        case SynthModel::AlesisQS71:   renderQS71();   break;
        case SynthModel::YamahaPSR540: renderPSR540();  break;
        }

        // "No patch data" dim overlay — only shown when SysEx never received
        // and we haven't been seeded from properties either.
        // (We don't show it since synth_set_defaults always seeds the params;
        //  the status string in the header already communicates the state.)

        gs_technique_end_pass(tech);
    }
    gs_technique_end(tech);
}

// ─────────────────────────────────────────────────────────────────────────────
// Properties
// ─────────────────────────────────────────────────────────────────────────────
static obs_properties_t *synth_properties(void *)
{
    obs_properties_t *props = obs_properties_create();

    // Model selection
    obs_property_t *mdl = obs_properties_add_list(props, "model",
        "Synth Model", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(mdl, "Behringer DeepMind 12", (int)SynthModel::DeepMind12);
    obs_property_list_add_int(mdl, "Korg DSS-1",            (int)SynthModel::KorgDSS1);
    obs_property_list_add_int(mdl, "Alesis QS7.1",          (int)SynthModel::AlesisQS71);
    obs_property_list_add_int(mdl, "Yamaha PSR-540",        (int)SynthModel::YamahaPSR540);

    // MIDI
    obs_property_t *port = obs_properties_add_list(props, "midi_port",
        "MIDI Input Device", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(port, "Disabled", -1);
    for (int i = 0; i < (int)MidiEngine::instance().portNames().size(); ++i)
        obs_property_list_add_int(port, MidiEngine::instance().portNames()[i].c_str(), i);

    obs_properties_add_button(props, "midi_refresh", "Refresh Devices",
        [](obs_properties_t *pp, obs_property_t *, void *) -> bool {
            obs_property_t *lst = obs_properties_get(pp, "midi_port");
            obs_property_list_clear(lst);
            obs_property_list_add_int(lst, "Disabled", -1);
            for (int i = 0; i < (int)MidiEngine::instance().portNames().size(); ++i)
                obs_property_list_add_int(lst, MidiEngine::instance().portNames()[i].c_str(), i);
            return true;
        });

    obs_properties_add_int(props, "midi_ch", "MIDI Channel (1-16, displayed +1)", 0, 15, 1);

    // Canvas
    obs_properties_add_int(props, "canvas_w", "Canvas width",  200, 3840, 1);
    obs_properties_add_int(props, "canvas_h", "Canvas height", 100, 2160, 1);

    return props;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────
void synth_source_register(void)
{
    static obs_source_info info{};
    info.id             = "midi_synth_source";
    info.type           = OBS_SOURCE_TYPE_INPUT;
    info.output_flags   = OBS_SOURCE_VIDEO;
    info.get_name       = synth_get_name;
    info.create         = synth_create;
    info.destroy        = synth_destroy;
    info.get_defaults   = synth_defaults;
    info.get_properties = synth_properties;
    info.update         = synth_update;
    info.video_tick     = synth_tick;
    info.video_render   = synth_render;
    info.get_width      = synth_get_width;
    info.get_height     = synth_get_height;
    obs_register_source(&info);
}
