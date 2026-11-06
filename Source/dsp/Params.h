// Tekkforge - zentrale Parameterliste (ohne JUCE, damit auch Tests sie nutzen koennen)
#pragma once
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>
#include "Wavetables.h"

namespace tk
{
enum Mode { MELO = 0, STAB, KICK, NUM_MODES };
inline const char* modeName (int m) { static const char* n[] = { "Melo", "Stab", "Kick" }; return n[m]; }
inline const char* modeShort (int m) { return modeName (m); }
inline bool modeIsMono (int m) { return m == KICK; }
inline const char* layerName (int mode, int l)
{
    static const char* kick[] = { "Topkick", "Bass", "Layer" }, *syn[] = { "Ebene A", "Ebene B", "Ebene C" };
    return mode == KICK ? kick[l] : syn[l];
}

// Globale Parameter
enum { G_MODE = 0, G_MASTER, G_QUALITY, G_LIMIT, G_COUNT };

// Jede Ebene ist ein kleiner eigener Synth
constexpr int NUM_LAYERS = 3;
enum
{
    LY_LVL = 0, LY_MUTE, LY_SOLO, LY_SRC, LY_WAVE, LY_TUNE, LY_FINE, LY_UNI, LY_DETUNE, LY_WIDTH, LY_PAN,
    LY_SUB, LY_SUBOCT,
    LY_START, LY_ROOT, LY_PITCH, LY_LOOP, LY_FREE,
    LY_HOLD, LY_LEN, LY_ATT, LY_CURVE, LY_SUS, LY_REL,
    LY_PAMT, LY_PTIME, LY_PCURVE,
    LY_FTYPE, LY_CUT, LY_RES, LY_FAMT, LY_FDEC,
    LY_DRV, LY_DRVTYPE, LY_DRVTONE, LY_LOWCUT,
    LY_TABLE, LY_WARP, LY_WARPAMT, LY_KEY, LY_GLIDE, LY_H2, LY_H3, LY_H5, LY_MONO, LY_PHASE, LY_PREF, LY_PREAMT, LY_DRV2, LY_DRV2TYPE,
    LY_COUNT
};

// Parameter je Modus
enum { M_TUNE = 0, M_FINE, M_CHORD, M_STRUM, M_VEL, M_LEVEL, M_LAYER0, M_COUNT = M_LAYER0 + NUM_LAYERS * LY_COUNT };
constexpr int Y (int layer, int p) { return M_LAYER0 + layer * LY_COUNT + p; }
constexpr int sampleSlot (int mode, int layer) { return mode * NUM_LAYERS + layer; }

// FX: 8 Master-Slots plus 3 Slots je Ebene
constexpr int NUM_MASTER_SLOTS = 8, SLOTS_PER_LAYER = 3, NUM_SLOTS = NUM_MASTER_SLOTS + NUM_LAYERS * SLOTS_PER_LAYER;
constexpr int layerSlot (int layer, int i) { return NUM_MASTER_SLOTS + layer * SLOTS_PER_LAYER + i; }
enum { S_ON = 0, S_TYPE, S_MIX, S_P1, S_P2, S_P3, S_P4, S_P5, S_P6, S_COUNT };
constexpr int FX_PARAMS = 6;

// Modulation wie in Vital: Quellen laufen pro Stimme, LFO-Formen werden gezeichnet
constexpr int NUM_LFOS = 4, NUM_ENVS = 3, NUM_SOURCES = NUM_LFOS + NUM_ENVS, NUM_MODS = 32, LFO_POINTS = 64;
enum { LF_SYNC = 0, LF_HZ, LF_DIV, LF_TRIG, LF_COUNT };
enum { EV_ATT = 0, EV_DEC, EV_SUS, EV_REL, EV_CURVE, EV_COUNT };
enum { MD_SOURCE = 0, MD_TARGET, MD_AMOUNT, MD_COUNT };

constexpr int MODE_BASE = G_COUNT;
constexpr int SLOT_BASE = MODE_BASE + NUM_MODES * M_COUNT;
constexpr int LFO_BASE = SLOT_BASE + NUM_SLOTS * S_COUNT;
constexpr int ENV_BASE = LFO_BASE + NUM_LFOS * LF_COUNT;
constexpr int MOD_BASE = ENV_BASE + NUM_ENVS * EV_COUNT;
constexpr int EQ_BASE = MOD_BASE + NUM_MODS * MD_COUNT;
enum { E_ON = 0, E_TYPE, E_FREQ, E_GAIN, E_Q, E_SLOPE, E_STEREO, E_DYN, E_THR, E_COUNT };
constexpr int NUM_PARAMS = EQ_BASE + 4 * 8 * E_COUNT;
constexpr int eqParam (int eq, int band, int p) { return EQ_BASE + (eq * 8 + band) * E_COUNT + p; }

constexpr int modeParam (int mode, int p) { return MODE_BASE + mode * M_COUNT + p; }
constexpr int slotParam (int slot, int p) { return SLOT_BASE + slot * S_COUNT + p; }
constexpr int lfoParam (int i, int p) { return LFO_BASE + i * LF_COUNT + p; }
constexpr int envParam (int i, int p) { return ENV_BASE + i * EV_COUNT + p; }
constexpr int modSlot (int i, int p) { return MOD_BASE + i * MD_COUNT + p; }
inline std::string sourceName (int s) { return s < NUM_LFOS ? "LFO " + std::to_string (s + 1) : "Env " + std::to_string (s - NUM_LFOS + 1); }

enum Kind { FLOAT = 0, CHOICE, INT };

struct ParamDef
{
    std::string id, name, unit;
    float min = 0, max = 1, def = 0, skew = 1;
    Kind kind = FLOAT;
    std::vector<std::string> choices;

    float toNorm (float v) const
    {
        const float n = (std::clamp (v, min, max) - min) / (max - min);
        return skew == 1.0f ? n : std::pow (n, skew);
    }
    float fromNorm (float n) const
    {
        n = std::clamp (n, 0.0f, 1.0f);
        if (skew != 1.0f) n = std::pow (n, 1.0f / skew);
        return min + (max - min) * n;
    }
    bool modulatable() const { return kind == FLOAT; }
};

// ---------------- FX-Beschreibung -----------------------------------------
enum FxType
{
    FX_OFF = 0, FX_DIST, FX_FILTER, FX_CHORUS, FX_FLANGER, FX_PHASER, FX_DESTRUCTOR,
    FX_CRUSH, FX_DELAY, FX_REVERB, FX_EQ, FX_COMP, FX_CLIP, FX_SIDECHAIN,
    FX_OTT, FX_TRANSIENT, FX_SHAPER, FX_STEREO, FX_SHIFT, FX_RING, FX_RUMBLE, FX_TAPE, FX_GATE, FX_TYPES
};
inline const std::vector<std::string>& fxNames()
{
    static const std::vector<std::string> n { "Leer", "Distortion", "Filter", "Chorus", "Flanger", "Phaser",
                                              "Destructor", "Bitcrush", "Delay", "Reverb", "EQ", "Kompressor", "Clipper", "Sidechain",
                                              "OTT", "Transient", "Waveshaper", "Stereo", "Freq Shift", "Ring Mod", "Rumble", "Tape", "Gate" };
    return n;
}

struct FxP
{
    const char* label; float min, max, skew; const char* unit; float def;   // def = realer Startwert
    const char* items = nullptr;                                            // Stufen, durch | getrennt
};
inline const FxP* fxInfo (int type)
{
    static const FxP none[FX_PARAMS] = { {"",0,1,1,"",0},{"",0,1,1,"",0},{"",0,1,1,"",0},{"",0,1,1,"",0},{"",0,1,1,"",0},{"",0,1,1,"",0} };
    static const FxP dist[FX_PARAMS] = { {"Drive",0,48,1,"dB",18}, {"Art",0,3,1,"",3,"Soft|Hard|Fold|Tekk"}, {"Ton",500,20000,0.35f,"Hz",8000},
                                         {"Asym",0,1,1,"",0.2f}, {"Lowcut",20,800,0.4f,"Hz",20}, {"Output",-24,12,1,"dB",-6} };
    static const FxP filt[FX_PARAMS] = { {"Art",0,3,1,"",0,"LP|HP|BP|Notch"}, {"Cutoff",20,20000,0.3f,"Hz",2500}, {"Reso",0,1,1,"",0.2f},
                                         {"Drive",0,24,1,"dB",0}, {"LFO Rate",0.05f,20,0.4f,"Hz",1}, {"LFO Tiefe",0,1,1,"",0} };
    static const FxP chor[FX_PARAMS] = { {"Rate",0.05f,8,0.4f,"Hz",0.7f}, {"Tiefe",0,1,1,"",0.45f}, {"Delay",3,40,0.6f,"ms",14},
                                         {"Feedback",0,0.9f,1,"",0.1f}, {"Breite",0,1,1,"",0.9f}, {"Lowcut",20,800,0.4f,"Hz",180} };
    static const FxP flan[FX_PARAMS] = { {"Rate",0.02f,8,0.4f,"Hz",0.25f}, {"Tiefe",0,1,1,"",0.7f}, {"Delay",0.3f,12,0.6f,"ms",2.5f},
                                         {"Feedback",-0.95f,0.95f,1,"",0.6f}, {"Breite",0,1,1,"",0.6f}, {"Lowcut",20,800,0.4f,"Hz",20} };
    static const FxP phas[FX_PARAMS] = { {"Rate",0.02f,8,0.4f,"Hz",0.35f}, {"Tiefe",0,1,1,"",0.7f}, {"Center",150,8000,0.35f,"Hz",1000},
                                         {"Feedback",-0.95f,0.95f,1,"",0.5f}, {"Stufen",2,12,1,"",6}, {"Breite",0,1,1,"",0.5f} };
    static const FxP dest[FX_PARAMS] = { {"Drive",0,48,1,"dB",24}, {"Fold",0,1,1,"",0.4f}, {"Crush",0,1,1,"",0.15f},
                                         {"Phaser",0,1,1,"",0.6f}, {"Ph Rate",0.02f,8,0.4f,"Hz",0.6f}, {"Ph Feedb",-0.95f,0.95f,1,"",0.6f} };
    static const FxP crus[FX_PARAMS] = { {"Bits",2,16,1,"bit",8}, {"Downsample",1,40,0.5f,"x",4}, {"Ton",500,20000,0.35f,"Hz",14000},
                                         {"Dither",0,1,1,"",0}, {"Lowcut",20,800,0.4f,"Hz",20}, {"Output",-24,12,1,"dB",0} };
    static const FxP dela[FX_PARAMS] = { {"Zeit",1,1500,0.4f,"ms",265}, {"Feedback",0,0.95f,1,"",0.4f}, {"Ton",500,20000,0.35f,"Hz",6000},
                                         {"Lowcut",20,2000,0.35f,"Hz",250}, {"Ping-Pong",0,1,1,"",0.7f}, {"Breite",0,1,1,"",1} };
    static const FxP reve[FX_PARAMS] = { {"Größe",0,1,1,"",0.6f}, {"Dämpfung",0,1,1,"",0.45f}, {"Breite",0,1,1,"",1},
                                         {"Lowcut",20,2000,0.35f,"Hz",300}, {"Ton",500,20000,0.35f,"Hz",9000}, {"Output",-24,12,1,"dB",0} };
    static const FxP equa[FX_PARAMS] = { {"Low",-24,24,1,"dB",0}, {"Low Freq",30,500,0.4f,"Hz",100}, {"Mid",-24,24,1,"dB",0},
                                         {"Mid Freq",100,10000,0.3f,"Hz",1200}, {"High",-24,24,1,"dB",0}, {"High Freq",1000,18000,0.4f,"Hz",6000} };
    static const FxP comp[FX_PARAMS] = { {"Schwelle",-48,0,1,"dB",-18}, {"Ratio",1,20,0.5f,":1",4}, {"Attack",0.1f,100,0.4f,"ms",5},
                                         {"Release",10,800,0.4f,"ms",120}, {"Knee",0,12,1,"dB",3}, {"Makeup",-12,24,1,"dB",4} };
    static const FxP clip[FX_PARAMS] = { {"Drive",0,36,1,"dB",6}, {"Decke",-24,0,1,"dB",-1}, {"Weich",0,1,1,"",0.2f},
                                         {"Lowcut",20,800,0.4f,"Hz",20}, {"Ton",500,20000,0.35f,"Hz",20000}, {"Output",-24,12,1,"dB",0} };
    static const FxP side[FX_PARAMS] = { {"Quelle",0,3,1,"",0,"Takt|Note|Audio|Link"}, {"Tiefe",0,1,1,"",0.85f}, {"Attack",0.1f,30,0.5f,"ms",1},
                                         {"Länge",10,600,0.5f,"ms",180}, {"Bis Freq",60,20000,0.35f,"Hz",20000}, {"Pegel",0,1,1,"",0.1f} };
    static const FxP ott[FX_PARAMS]  = { {"Menge",0,1,1,"",0.4f}, {"Tief",0,1,1,"",0.7f}, {"Mitte",0,1,1,"",0.7f}, {"Hoch",0,1,1,"",0.7f}, {"Tempo",0,1,1,"",0.5f}, {"Output",-24,12,1,"dB",0} };
    static const FxP tran[FX_PARAMS] = { {"Anschlag",-1,1,1,"",0.4f}, {"Ausklang",-1,1,1,"",0}, {"Tempo",1,40,0.5f,"ms",8}, {"Lowcut",20,800,0.4f,"Hz",20}, {"Soft Clip",0,1,1,"",0}, {"Output",-24,12,1,"dB",0} };
    static const FxP wave[FX_PARAMS] = { {"Kurve",0,5,1,"",0,"Sinus|Stufe|Exp|Spitz|Chebyshev|Hart"}, {"Drive",0,36,1,"dB",12}, {"Bias",-1,1,1,"",0}, {"Ton",500,20000,0.35f,"Hz",14000}, {"Lowcut",20,800,0.4f,"Hz",20}, {"Output",-24,12,1,"dB",-6} };
    static const FxP ster[FX_PARAMS] = { {"Breite",0,2,1,"",1}, {"Mono bis",20,500,0.4f,"Hz",120}, {"Haas",0,30,1,"ms",0}, {"Balance",-1,1,1,"",0}, {"Seite Hoch",-12,12,1,"dB",0}, {"Output",-24,12,1,"dB",0} };
    static const FxP shif[FX_PARAMS] = { {"Shift",-2000,2000,1,"Hz",30}, {"Spreizung",0,1,1,"",0}, {"Feedback",0,0.9f,1,"",0}, {"Lowcut",20,800,0.4f,"Hz",20}, {"Ton",500,20000,0.35f,"Hz",20000}, {"Output",-24,12,1,"dB",0} };
    static const FxP ring[FX_PARAMS] = { {"Frequenz",1,5000,0.3f,"Hz",220}, {"Form",0,1,1,"",0}, {"LFO Tiefe",0,1,1,"",0}, {"LFO Rate",0.05f,20,0.4f,"Hz",1}, {"Lowcut",20,800,0.4f,"Hz",20}, {"Output",-24,12,1,"dB",0} };
    static const FxP rumb[FX_PARAMS] = { {"Pegel",0,1,1,"",0.5f}, {"Größe",0,1,1,"",0.7f}, {"Tiefpass",80,600,0.4f,"Hz",220}, {"Drive",0,30,1,"dB",12}, {"Ducking",0,1,1,"",0.8f}, {"Output",-24,12,1,"dB",0} };
    static const FxP tape[FX_PARAMS] = { {"Sättigung",0,1,1,"",0.4f}, {"Wow",0,1,1,"",0.1f}, {"Flutter",0,1,1,"",0.1f}, {"Höhen",1000,20000,0.35f,"Hz",12000}, {"Bass",-6,6,1,"dB",1}, {"Output",-24,12,1,"dB",0} };
    static const FxP gate[FX_PARAMS] = { {"Muster",0,5,1,"",0,"1/8|1/16|Tresillo|Gallop|Off|Stotter"}, {"Tiefe",0,1,1,"",1}, {"Weich",0,1,1,"",0.15f}, {"Länge",0.1f,1,1,"",0.6f}, {"Takt",0,3,1,"",1,"1/2|1/1|2x|4x"}, {"Output",-24,12,1,"dB",0} };
    switch (type)
    {
        case FX_OTT: return ott; case FX_TRANSIENT: return tran; case FX_SHAPER: return wave; case FX_STEREO: return ster; case FX_SHIFT: return shif;
        case FX_RING: return ring; case FX_RUMBLE: return rumb; case FX_TAPE: return tape; case FX_GATE: return gate;
        case FX_SIDECHAIN: return side;
        case FX_DIST: return dist;  case FX_FILTER: return filt;  case FX_CHORUS: return chor;
        case FX_FLANGER: return flan; case FX_PHASER: return phas; case FX_DESTRUCTOR: return dest;
        case FX_CRUSH: return crus; case FX_DELAY: return dela;   case FX_REVERB: return reve;
        case FX_EQ: return equa;    case FX_COMP: return comp;    case FX_CLIP: return clip;
        default: return none;
    }
}
inline float fxReal (const FxP& p, float n)
{
    n = std::clamp (n, 0.0f, 1.0f);
    if (p.skew != 1.0f) n = std::pow (n, 1.0f / p.skew);
    float v = p.min + (p.max - p.min) * n;
    return p.items != nullptr ? std::round (v) : v;
}
inline float fxNorm (const FxP& p, float v)
{
    const float n = (std::clamp (v, p.min, p.max) - p.min) / (p.max - p.min);
    return p.skew == 1.0f ? n : std::pow (n, p.skew);
}
inline float fxDefaultMix (int type)
{
    switch (type) { case FX_CHORUS: return 0.5f; case FX_FLANGER: return 0.5f; case FX_PHASER: return 0.5f;
                    case FX_DELAY: return 0.25f; case FX_REVERB: return 0.2f; default: return 1.0f; }
}

inline const std::vector<std::string>& driveTypes() { static const std::vector<std::string> v { "Soft", "Hard", "Fold", "Tekk", "Tube", "Asym", "Rectify", "Sinus", "Bit", "Screech" }; return v; }
inline const std::vector<std::string>& chordNames()
{
    static const std::vector<std::string> v { "Aus", "Oktave", "Quinte", "Moll", "Dur", "Sus2", "Sus4", "Moll7", "Dur7", "Moll9" };
    return v;
}
inline int chordIntervals (int chord, int* out)
{
    static const int t[10][5] = { {0}, {0,12}, {0,7,12}, {0,3,7}, {0,4,7}, {0,2,7}, {0,5,7}, {0,3,7,10}, {0,4,7,11}, {0,3,7,10,14} };
    static const int c[10] = { 1, 2, 3, 3, 3, 3, 3, 4, 4, 5 };
    chord = std::clamp (chord, 0, 9);
    for (int i = 0; i < c[chord]; ++i) out[i] = t[chord][i];
    return c[chord];
}
inline const std::vector<std::string>& laneRates()
{
    static const std::vector<std::string> v { "4 Takte", "2 Takte", "1 Takt", "1/2", "1/4", "1/8", "1/16", "1/32" };
    return v;
}
inline double laneBeats (int r) { static const double b[] = { 16, 8, 4, 2, 1, 0.5, 0.25, 0.125 }; return b[std::clamp (r, 0, 7)]; }
inline const std::vector<std::string>& lfoTrigs()
{
    static const std::vector<std::string> v { "Frei", "Je Note", "Einmal" };
    return v;
}

// ---------------- Startklang -----------------------------------------------
inline float modeDefault (int mode, int p)
{
    switch (p)
    {
        case M_CHORD: return mode == STAB ? 3.0f : 0.0f;
        case M_VEL:   return mode == KICK ? 0.0f : 0.3f;
        case M_LEVEL: return -6;
        default: return 0;
    }
}
inline float layerDefault (int mode, int l, int p)
{
    struct E { int p; float v; };
    static const E melo[] = { {LY_TABLE,2},{LY_UNI,3},{LY_DETUNE,10},{LY_WIDTH,0.6f},{LY_LEN,450},{LY_REL,180},{LY_ATT,1},{LY_CUT,3500},{LY_FAMT,1.5f},{LY_FDEC,220},{LY_DRV,0.1f},{LY_LOWCUT,120} };
    static const E stab[] = { {LY_TABLE,2},{LY_UNI,5},{LY_DETUNE,18},{LY_WIDTH,0.85f},{LY_LEN,280},{LY_REL,120},{LY_ATT,1},{LY_CUT,2600},{LY_RES,0.25f},{LY_FAMT,2.5f},{LY_FDEC,160},{LY_DRV,0.2f},{LY_LOWCUT,180} };
    static const E top[]  = { {LY_LVL,0.8f},{LY_TABLE,1},{LY_KEY,2},{LY_ROOT,41},{LY_PAMT,60},{LY_PTIME,18},{LY_PCURVE,0.7f},{LY_LEN,70},{LY_CURVE,0.6f},{LY_FTYPE,2},{LY_CUT,300},{LY_DRV,0.4f},{LY_DRVTYPE,3},{LY_DRVTONE,7000},{LY_LOWCUT,240} };
    static const E bass[] = { {LY_KEY,1},{LY_PAMT,36},{LY_PTIME,45},{LY_PCURVE,0.7f},{LY_LEN,300},{LY_CURVE,0.3f},{LY_HOLD,1},{LY_SUS,0},{LY_DRV,0.35f},{LY_DRVTYPE,3},{LY_DRVTONE,5000},{LY_LOWCUT,30} };
    static const E lay[]  = { {LY_MUTE,1},{LY_LVL,0.7f},{LY_TABLE,2},{LY_LEN,120},{LY_LOWCUT,120} };
    const auto find = [&] (const E* e, int n, float def) { for (int i = 0; i < n; ++i) if (e[i].p == p) return e[i].v; return def; };
    float def = 0;
    switch (p)
    {
        case LY_LVL: def = 1; break;
        case LY_UNI: def = 1; break;
        case LY_ROOT: def = mode == KICK ? 29.0f : 60.0f; break;
        case LY_LEN: def = 300; break;
        case LY_CURVE: def = 0.5f; break;
        case LY_REL: def = 20; break;
        case LY_PTIME: def = 30; break;
        case LY_PCURVE: def = 0.5f; break;
        case LY_CUT: def = 20000; break;
        case LY_FDEC: def = 120; break;
        case LY_DRVTONE: def = 14000; break;
        case LY_LOWCUT: def = 20; break;
        case LY_FREE: def = 1; break;
        case LY_PREF: def = 700; break;
        case LY_MONO: def = 20; break;
        case LY_PITCH: def = mode == KICK ? 0.0f : 1.0f; break;
        default: break;
    }
    if (mode == KICK)
    {
        if (l == 0) return find (top, sizeof (top) / sizeof (E), def);
        if (l == 1) return find (bass, sizeof (bass) / sizeof (E), def);
        return find (lay, sizeof (lay) / sizeof (E), def);
    }
    if (l > 0) { if (p == LY_MUTE) return 1; return find (lay, sizeof (lay) / sizeof (E), def); }
    return mode == MELO ? find (melo, sizeof (melo) / sizeof (E), def) : find (stab, sizeof (stab) / sizeof (E), def);
}

/** Fertige Charaktere je Ebene: setzen nur die Klangregler, ein guter Startpunkt in einem Klick. */
struct Patch { const char* name; std::vector<std::pair<int, float>> values; };
inline const std::vector<Patch>& bassPatches()
{
    static const std::vector<Patch> p {
        { "Sauberer Sub",     { {LY_TABLE,0},{LY_WAVE,0},{LY_WARP,0},{LY_FTYPE,9},{LY_DRV,0.05f},{LY_DRV2,0},{LY_SUB,0},{LY_H2,0},{LY_H3,0},{LY_H5,0},{LY_PREAMT,0},{LY_PAMT,30},{LY_PTIME,40} } },
        { "Tekk Kick-Bass",   { {LY_TABLE,12},{LY_WAVE,0.4f},{LY_FTYPE,1},{LY_CUT,3000},{LY_RES,0.1f},{LY_DRV,0.45f},{LY_DRVTYPE,3},{LY_DRV2,0.25f},{LY_DRV2TYPE,1},{LY_PREF,700},{LY_PREAMT,9},{LY_SUB,0.5f},{LY_PAMT,36},{LY_PTIME,45} } },
        { "Rumble",           { {LY_TABLE,8},{LY_WAVE,0.5f},{LY_FTYPE,1},{LY_CUT,900},{LY_RES,0.15f},{LY_DRV,0.5f},{LY_DRVTYPE,0},{LY_SUB,0.6f},{LY_H2,0.2f},{LY_PAMT,24},{LY_PTIME,30} } },
        { "Reese",            { {LY_TABLE,9},{LY_WAVE,0.6f},{LY_UNI,3},{LY_DETUNE,14},{LY_WIDTH,0.5f},{LY_FTYPE,1},{LY_CUT,1800},{LY_RES,0.2f},{LY_DRV,0.3f},{LY_SUB,0.5f},{LY_MONO,120},{LY_PAMT,0} } },
        { "Growl",            { {LY_TABLE,10},{LY_WAVE,0.6f},{LY_WARP,6},{LY_WARPAMT,0.3f},{LY_FTYPE,8},{LY_CUT,900},{LY_RES,0.5f},{LY_DRV,0.5f},{LY_DRVTYPE,4},{LY_SUB,0.45f},{LY_PAMT,12},{LY_PTIME,60} } },
        { "Hoover",           { {LY_TABLE,11},{LY_WAVE,0.7f},{LY_UNI,5},{LY_DETUNE,20},{LY_WIDTH,0.6f},{LY_FTYPE,1},{LY_CUT,4000},{LY_DRV,0.25f},{LY_SUB,0.4f},{LY_GLIDE,-3},{LY_MONO,150},{LY_PAMT,12},{LY_PTIME,120} } },
        { "Acid",             { {LY_TABLE,13},{LY_WAVE,0.5f},{LY_FTYPE,1},{LY_CUT,900},{LY_RES,0.75f},{LY_FAMT,3.5f},{LY_FDEC,160},{LY_DRV,0.4f},{LY_DRVTYPE,3},{LY_SUB,0.3f},{LY_PAMT,0} } },
        { "FM Bite",          { {LY_TABLE,16},{LY_WAVE,0.5f},{LY_FTYPE,1},{LY_CUT,3500},{LY_DRV,0.3f},{LY_SUB,0.5f},{LY_H3,0.15f},{LY_PAMT,24},{LY_PTIME,25} } },
        { "Harmonische Sub",  { {LY_TABLE,0},{LY_FTYPE,9},{LY_DRV,0.2f},{LY_DRVTYPE,4},{LY_H2,0.4f},{LY_H3,0.3f},{LY_H5,0.15f},{LY_SUB,0.3f},{LY_PAMT,30},{LY_PTIME,40} } },
        { "Screech Tail",     { {LY_TABLE,34},{LY_WAVE,0.5f},{LY_FTYPE,4},{LY_CUT,900},{LY_RES,0.6f},{LY_DRV,0.6f},{LY_DRVTYPE,9},{LY_DRV2,0.4f},{LY_DRV2TYPE,3},{LY_PREF,900},{LY_PREAMT,12},{LY_SUB,0.5f},{LY_GLIDE,-5},{LY_PAMT,36},{LY_PTIME,70} } } };
    return p;
}
inline const std::vector<Patch>& topPatches()
{
    static const std::vector<Patch> p {
        { "Harter Klick",  { {LY_TABLE,3},{LY_PAMT,72},{LY_PTIME,12},{LY_LEN,40},{LY_FTYPE,3},{LY_CUT,500},{LY_DRV,0.5f},{LY_DRVTYPE,1},{LY_LOWCUT,300} } },
        { "Punch",         { {LY_TABLE,0},{LY_PAMT,48},{LY_PTIME,25},{LY_LEN,90},{LY_FTYPE,3},{LY_CUT,180},{LY_DRV,0.5f},{LY_DRVTYPE,3},{LY_LOWCUT,160} } },
        { "Noise Tok",     { {LY_TABLE,WT_NOISE_INDEX},{LY_PAMT,0},{LY_LEN,35},{LY_FTYPE,4},{LY_CUT,3000},{LY_RES,0.3f},{LY_DRV,0.3f},{LY_LOWCUT,400} } },
        { "Metall",        { {LY_TABLE,19},{LY_WAVE,0.6f},{LY_PAMT,36},{LY_PTIME,15},{LY_LEN,60},{LY_FTYPE,3},{LY_CUT,600},{LY_DRV,0.3f},{LY_LOWCUT,300} } },
        { "Weich",         { {LY_TABLE,1},{LY_PAMT,24},{LY_PTIME,20},{LY_LEN,80},{LY_FTYPE,3},{LY_CUT,200},{LY_DRV,0.15f},{LY_LOWCUT,200} } } };
    return p;
}

/** Alles, was ein Charakter setzt: erst neutral, dann seine Werte. So bleibt vom vorigen nichts haengen. */
inline std::vector<std::pair<int, float>> patchValues (const Patch& p)
{
    std::vector<std::pair<int, float>> v { {LY_SRC,0},{LY_WARP,0},{LY_WARPAMT,0},{LY_WAVE,0},{LY_UNI,1},{LY_DETUNE,0},{LY_WIDTH,0},{LY_RES,0},{LY_FAMT,0},{LY_DRV,0},{LY_DRV2,0},
        {LY_PREAMT,0},{LY_H2,0},{LY_H3,0},{LY_H5,0},{LY_GLIDE,0},{LY_SUB,0},{LY_MONO,20} };
    v.insert (v.end(), p.values.begin(), p.values.end());
    return v;
}
/** Vokal der Formant-Filter: Cutoff faehrt durch A, E, I, O, U. */
inline float formantHz (float cut, int k)
{
    static const float vowels[5][3] = { { 800, 1150, 2900 }, { 350, 2000, 2800 }, { 270, 2140, 2950 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
    const float x = std::clamp (std::log2 (std::max (cut, 20.0f) / 150.0f) / 4.0f, 0.0f, 0.999f) * 4.0f;
    const int a = (int) x; const float m = x - (float) a;
    return vowels[a][k] + (vowels[a + 1][k] - vowels[a][k]) * m;
}
inline const char* fxHint (int t)
{
    switch (t)
    {
        case FX_OTT:       return "Drei Bänder, oben und unten komprimiert. Macht leise Details laut und alles dicht.";
        case FX_TRANSIENT: return "Formt Anschlag und Ausklang ohne Schwelle. Gut für Klick und kurze Schwänze.";
        case FX_SHAPER:    return "Biegt die Wellenform. Bias bringt gerade Obertöne, Chebyshev klingt metallisch.";
        case FX_STEREO:    return "Breite mit Mono-Grenze. Unter Mono bis bleibt alles in der Mitte, wichtig für Bass.";
        case FX_SHIFT:     return "Verschiebt alle Frequenzen um feste Hz. Kleine Werte schweben, große klingen metallisch.";
        case FX_RING:      return "Multipliziert mit einem Ton. Für Glocken, Roboter und harte Mitten.";
        case FX_RUMBLE:    return "Tiefe verzerrte Fahne hinter der Kick, wird vom Signal weggeduckt.";
        case FX_TAPE:      return "Sättigung mit Gleichlaufschwankung und Bass-Anhebung.";
        case FX_GATE:      return "Schneidet im Takt des Projekts. Für rollende Bässe und Stotter.";
        default:           return "";
    }
}

/** Einstellungen nach dem Fertig-Rendern: Alles ist neutral, nur das Sample auf Ebene 1 klingt unveraendert. */
struct FrozenValue { int index; float value; };
inline std::vector<FrozenValue> frozenValues (int mode, int note)
{
    std::vector<FrozenValue> v { { G_MODE, (float) mode }, { G_MASTER, 0 }, { G_QUALITY, 0 }, { G_LIMIT, 0 } };
    for (auto& e : std::initializer_list<std::pair<int, float>> { { M_LEVEL, 0 }, { M_VEL, 0 }, { M_CHORD, 0 }, { M_STRUM, 0 }, { M_TUNE, 0 }, { M_FINE, 0 } })
        v.push_back ({ modeParam (mode, e.first), e.second });
    const std::pair<int, float> y[] = {
        { LY_LVL, 1 }, { LY_MUTE, 0 }, { LY_SOLO, 0 }, { LY_SRC, 1 }, { LY_ROOT, (float) note }, { LY_START, 0 }, { LY_TUNE, 0 }, { LY_FINE, 0 }, { LY_PAN, 0 },
        { LY_LOOP, 0 }, { LY_FREE, 1 }, { LY_SUB, 0 }, { LY_DRV, 0 }, { LY_DRV2, 0 }, { LY_PREAMT, 0 }, { LY_PAMT, 0 }, { LY_FAMT, 0 }, { LY_FTYPE, 9 }, { LY_LOWCUT, 20 },
        { LY_KEY, 0 }, { LY_GLIDE, 0 }, { LY_H2, 0 }, { LY_H3, 0 }, { LY_H5, 0 }, { LY_MONO, 20 }, { LY_PHASE, 0 } };
    for (auto& e : y) v.push_back ({ modeParam (mode, Y (0, e.first)), e.second });
    for (int l = 1; l < NUM_LAYERS; ++l) { v.push_back ({ modeParam (mode, Y (l, LY_MUTE)), 1 }); v.push_back ({ modeParam (mode, Y (l, LY_SOLO)), 0 }); }
    return v;
}

inline const std::vector<ParamDef>& params()
{
    static const std::vector<ParamDef> table = []
    {
        std::vector<ParamDef> t ((size_t) NUM_PARAMS);
        auto fl = [&] (int i, std::string id, std::string name, float mn, float mx, float def, float skew, std::string unit)
        {
            ParamDef d; d.id = std::move (id); d.name = std::move (name); d.min = mn; d.max = mx; d.def = def; d.skew = skew; d.unit = std::move (unit);
            t[(size_t) i] = std::move (d);
        };
        auto ch = [&] (int i, std::string id, std::string name, std::vector<std::string> items, int def)
        {
            ParamDef d; d.id = std::move (id); d.name = std::move (name); d.min = 0; d.max = (float) items.size() - 1.0f;
            d.def = (float) def; d.kind = CHOICE; d.choices = std::move (items);
            t[(size_t) i] = std::move (d);
        };
        const auto offOn = [] { return std::vector<std::string> { "Aus", "An" }; };

        ch (G_MODE, "mode", "Modus", { "Melo", "Stab", "Kick" }, KICK);
        fl (G_MASTER, "master", "Master", -36, 6, 0, 1, "dB");
        ch (G_QUALITY, "quality", "Qualität", { "1x", "2x", "4x" }, 1);
        ch (G_LIMIT, "limit", "Schutz-Limiter", offOn(), 1);

        for (int m = 0; m < NUM_MODES; ++m)
        {
            const std::string pi = "m" + std::to_string (m) + "_", pn = std::string (modeShort (m)) + " ";
            const auto F = [&] (int p, const char* id, const char* name, float mn, float mx, float skew, const char* unit)
            { fl (modeParam (m, p), pi + id, pn + name, mn, mx, modeDefault (m, p), skew, unit); };
            F (M_TUNE, "tune", "Tune", -24, 24, 1, "st");
            F (M_FINE, "fine", "Fein", -100, 100, 1, "ct");
            ch (modeParam (m, M_CHORD), pi + "chord", pn + "Akkord", chordNames(), (int) modeDefault (m, M_CHORD));
            F (M_STRUM, "strum", "Strum", 0, 40, 1, "ms");
            F (M_VEL, "vel", "Velocity", 0, 1, 1, "");
            F (M_LEVEL, "level", "Pegel", -36, 6, 1, "dB");
            for (int l = 0; l < NUM_LAYERS; ++l)
            {
                const std::string oi = pi + "y" + std::to_string (l) + "_", on = pn + layerName (m, l) + " ";
                const auto OF = [&] (int p, const char* id, const char* name, float mn, float mx, float skew, const char* unit)
                { fl (modeParam (m, Y (l, p)), oi + id, on + name, mn, mx, layerDefault (m, l, p), skew, unit); };
                const auto OC = [&] (int p, const char* id, const char* name, std::vector<std::string> items)
                { ch (modeParam (m, Y (l, p)), oi + id, on + name, std::move (items), (int) layerDefault (m, l, p)); };
                OF (LY_LVL, "lvl", "Pegel", 0, 1, 1, "");
                OC (LY_MUTE, "mute", "Stumm", offOn());
                OC (LY_SOLO, "solo", "Solo", offOn());
                OC (LY_SRC, "src", "Quelle", { "Welle", "Sample" });
                OF (LY_WAVE, "wave", "Position", 0, 1, 1, "");
                OF (LY_TUNE, "tune", "Tune", -24, 24, 1, "st");
                OF (LY_FINE, "fine", "Fein", -100, 100, 1, "ct");
                OF (LY_UNI, "uni", "Stimmen", 1, 7, 1, "");
                OF (LY_DETUNE, "detune", "Verstimmung", 0, 60, 0.6f, "ct");
                OF (LY_WIDTH, "width", "Breite", 0, 1, 1, "");
                OF (LY_PAN, "pan", "Pan", -1, 1, 1, "");
                OF (LY_SUB, "sub", "Sub", 0, 1, 1, "");
                OC (LY_SUBOCT, "suboct", "Sub Oktave", { "0", "-1", "-2" });
                OF (LY_START, "start", "Sample Start", 0, 1, 1, "");
                OF (LY_ROOT, "root", "Grundton", 0, 127, 1, "");
                OC (LY_PITCH, "pitch", "Tonhöhe", { "Fest", "Note", "Note + Kurve" });
                OC (LY_LOOP, "loop", "Loop", offOn());
                OC (LY_FREE, "free", "Sample Länge", { "Hüllkurve", "Ganz" });
                OC (LY_HOLD, "hold", "Spielart", { "One-Shot", "Notenlänge" });
                OF (LY_LEN, "len", "Länge", 5, 6000, 0.3f, "ms");
                OF (LY_ATT, "att", "Attack", 0, 500, 0.3f, "ms");
                OF (LY_CURVE, "curve", "Kurve", 0, 1, 1, "");
                OF (LY_SUS, "sus", "Sustain", 0, 1, 1, "");
                OF (LY_REL, "rel", "Release", 1, 2000, 0.3f, "ms");
                OF (LY_PAMT, "pamt", "Pitch Menge", -24, 84, 1, "st");
                OF (LY_PTIME, "ptime", "Pitch Zeit", 1, 800, 0.35f, "ms");
                OF (LY_PCURVE, "pcurve", "Pitch Kurve", 0, 1, 1, "");
                OC (LY_FTYPE, "ftype", "Filterart", { "LP 12", "LP 24", "HP 12", "HP 24", "BP", "Notch", "Comb +", "Comb -", "Formant", "Aus" });
                OF (LY_CUT, "cut", "Cutoff", 20, 20000, 0.3f, "Hz");
                OF (LY_RES, "res", "Resonanz", 0, 1, 1, "");
                OF (LY_FAMT, "famt", "Filter Env", -6, 6, 1, "oct");
                OF (LY_FDEC, "fdec", "Filter Zeit", 1, 2000, 0.35f, "ms");
                OF (LY_DRV, "drv", "Drive", 0, 1, 1, "");
                OC (LY_DRVTYPE, "drvtype", "Drive Art", driveTypes());
                OC (LY_TABLE, "table", "Welle", waveNames());
                OC (LY_WARP, "warp", "Warp", warpNames());
                OF (LY_WARPAMT, "warpamt", "Warp Menge", 0, 1, 1, "");
                OC (LY_KEY, "key", "Tonlage", { "Normal", "Bass-Lage", "Fest" });
                OF (LY_GLIDE, "glide", "Tail Pitch", -24, 24, 1, "st");
                OF (LY_H2, "h2", "Oberton 2", 0, 1, 1, "");
                OF (LY_H3, "h3", "Oberton 3", 0, 1, 1, "");
                OF (LY_H5, "h5", "Oberton 5", 0, 1, 1, "");
                OF (LY_MONO, "mono", "Mono bis", 20, 500, 0.4f, "Hz");
                OC (LY_PHASE, "phase", "Phase", { "Normal", "180" });
                OF (LY_PREF, "pref", "Boost Freq", 100, 4000, 0.4f, "Hz");
                OF (LY_PREAMT, "preamt", "Boost", 0, 24, 1, "dB");
                OF (LY_DRV2, "drv2", "Drive 2", 0, 1, 1, "");
                OC (LY_DRV2TYPE, "drv2type", "Drive 2 Art", driveTypes());
                OF (LY_DRVTONE, "drvtone", "Drive Ton", 500, 20000, 0.35f, "Hz");
                OF (LY_LOWCUT, "lowcut", "Lowcut", 20, 800, 0.4f, "Hz");
            }
        }
        for (int s = 0; s < NUM_SLOTS; ++s)
        {
            const std::string pi = "s" + std::to_string (s) + "_";
            const std::string pn = s < NUM_MASTER_SLOTS ? "FX" + std::to_string (s + 1) + " "
                                                        : "Ebene" + std::to_string ((s - NUM_MASTER_SLOTS) / SLOTS_PER_LAYER + 1) + " FX" + std::to_string ((s - NUM_MASTER_SLOTS) % SLOTS_PER_LAYER + 1) + " ";
            ch (slotParam (s, S_ON), pi + "on", pn + "An", offOn(), 1);
            ch (slotParam (s, S_TYPE), pi + "type", pn + "Effekt", fxNames(), 0);
            fl (slotParam (s, S_MIX), pi + "mix", pn + "Mix", 0, 1, 1, 1, "");
            for (int k = 0; k < FX_PARAMS; ++k)
                fl (slotParam (s, S_P1 + k), pi + "p" + std::to_string (k + 1), pn + "Wert " + std::to_string (k + 1), 0, 1, 0.5f, 1, "");
        }
        for (int i = 0; i < NUM_LFOS; ++i)
        {
            const std::string pi = "lfo" + std::to_string (i) + "_", pn = "LFO " + std::to_string (i + 1) + " ";
            ch (lfoParam (i, LF_SYNC), pi + "sync", pn + "Takt", { "Frei", "Takt" }, 1);
            fl (lfoParam (i, LF_HZ), pi + "hz", pn + "Tempo", 0.02f, 40, 2.0f, 0.35f, "Hz");
            ch (lfoParam (i, LF_DIV), pi + "div", pn + "Teiler", laneRates(), 5);
            ch (lfoParam (i, LF_TRIG), pi + "trig", pn + "Start", lfoTrigs(), 1);
        }
        for (int i = 0; i < NUM_ENVS; ++i)
        {
            const std::string pi = "env" + std::to_string (i) + "_", pn = "Env " + std::to_string (i + 1) + " ";
            fl (envParam (i, EV_ATT), pi + "att", pn + "Attack", 0, 2000, 0.3f, 1, "ms");
            fl (envParam (i, EV_DEC), pi + "dec", pn + "Decay", 1, 4000, 0.3f, 120, "ms");
            fl (envParam (i, EV_SUS), pi + "sus", pn + "Sustain", 0, 1, 0, 1, "");
            fl (envParam (i, EV_REL), pi + "rel", pn + "Release", 1, 4000, 0.3f, 60, "ms");
            fl (envParam (i, EV_CURVE), pi + "curve", pn + "Kurve", 0, 1, 0.5f, 1, "");
        }
        for (int i = 0; i < NUM_MODS; ++i)
        {
            const std::string pi = "mod" + std::to_string (i) + "_", pn = "Mod " + std::to_string (i + 1) + " ";
            std::vector<std::string> src; for (int k = 0; k < NUM_SOURCES; ++k) src.push_back (sourceName (k));
            ch (modSlot (i, MD_SOURCE), pi + "src", pn + "Quelle", src, 0);
            ParamDef d; d.id = pi + "target"; d.name = pn + "Ziel"; d.min = -1; d.max = (float) NUM_PARAMS - 1.0f; d.def = -1; d.kind = INT;
            t[(size_t) modSlot (i, MD_TARGET)] = std::move (d);
            fl (modSlot (i, MD_AMOUNT), pi + "amount", pn + "Menge", -1, 1, 0.3f, 1, "");
        }
        static const char* eqNames[4] = { "Master", "Ebene1", "Ebene2", "Ebene3" };
        for (int q = 0; q < 4; ++q)
            for (int b = 0; b < 8; ++b)
            {
                const std::string pi = "e" + std::to_string (q) + "b" + std::to_string (b) + "_", pn = std::string ("EQ ") + eqNames[q] + " B" + std::to_string (b + 1) + " ";
                static const float start[8] = { 40, 90, 200, 500, 1200, 3000, 7000, 14000 };
                ch (eqParam (q, b, E_ON), pi + "on", pn + "An", offOn(), 0);
                ch (eqParam (q, b, E_TYPE), pi + "type", pn + "Form", { "Glocke", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Bandpass", "Tilt" }, 0);
                fl (eqParam (q, b, E_FREQ), pi + "freq", pn + "Frequenz", 20, 20000, start[b], 0.3f, "Hz");
                fl (eqParam (q, b, E_GAIN), pi + "gain", pn + "Gain", -30, 30, 0, 1, "dB");
                fl (eqParam (q, b, E_Q), pi + "q", pn + "Q", 0.1f, 18, 1, 0.4f, "");
                ch (eqParam (q, b, E_SLOPE), pi + "slope", pn + "Flanke", { "12 dB", "24 dB", "48 dB" }, 1);
                ch (eqParam (q, b, E_STEREO), pi + "stereo", pn + "Stereo", { "Stereo", "Mid", "Side", "Links", "Rechts" }, 0);
                fl (eqParam (q, b, E_DYN), pi + "dyn", pn + "Dynamik", -24, 24, 0, 1, "dB");
                fl (eqParam (q, b, E_THR), pi + "thr", pn + "Schwelle", -60, 0, -24, 1, "dB");
            }
        return t;
    }();
    return table;
}
} // namespace tk
