// Tekkforge - zentrale Parameterliste (ohne JUCE, damit auch Tests sie nutzen koennen)
#pragma once
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

namespace tk
{
enum Mode { MELO = 0, STAB, TOPKICK, KICKBASS, FULLKICK, NUM_MODES };
inline const char* modeName (int m)
{
    static const char* n[] = { "Melo One-Shot", "Stab", "Topkick", "Kickbass", "Full Kick" };
    return n[m];
}
inline const char* modeShort (int m)
{
    static const char* n[] = { "Melo", "Stab", "Top", "KBass", "Kick" };
    return n[m];
}
inline bool modeIsMono (int m) { return m >= TOPKICK; }

// Globale Parameter
enum { G_MODE = 0, G_MASTER, G_QUALITY, G_LIMIT, G_COUNT };

// Parameter, die jeder Modus getrennt besitzt
enum
{
    M_WAVE = 0, M_UNI, M_DETUNE, M_WIDTH, M_TUNE, M_FINE,
    M_PAMT, M_PTIME, M_PCURVE,
    M_ATT, M_HOLD, M_DEC, M_SUS, M_REL, M_ACURVE, M_GATE,
    M_FTYPE, M_CUT, M_RES, M_FENV, M_FDEC,
    M_CLKLVL, M_CLKTONE, M_CLKLEN, M_CLKNOISE,
    M_SUBLVL, M_SUBOCT,
    M_DRV, M_DRVTYPE, M_DRVTONE,
    M_CHORD, M_STRUM,
    M_LOWCUT, M_VEL, M_LEVEL,
    M_OSCLVL, M_SMPLVL, M_SMPSTART, M_SMPTUNE, M_SMPROOT, M_SMPPITCH, M_SMPLOOP, M_SMPFREE,
    M_COUNT
};

// FX
constexpr int NUM_SLOTS = 8;
enum { S_ON = 0, S_TYPE, S_MIX, S_P1, S_P2, S_P3, S_P4, S_P5, S_P6, S_COUNT };
constexpr int FX_PARAMS = 6;

// interne Automation
constexpr int NUM_LANES = 4;
constexpr int LANE_POINTS = 64;
enum { L_ON = 0, L_TARGET, L_AMOUNT, L_RATE, L_TRIG, L_COUNT };

// frei ziehbare Modulationsquellen
constexpr int NUM_LFOS = 3, NUM_ENVS = 3, NUM_SOURCES = NUM_LFOS + NUM_ENVS, NUM_MODS = 16;
enum { LF_SHAPE = 0, LF_SYNC, LF_HZ, LF_DIV, LF_RETRIG, LF_COUNT };
enum { EV_ATT = 0, EV_DEC, EV_SUS, EV_REL, EV_CURVE, EV_COUNT };
enum { MD_SOURCE = 0, MD_TARGET, MD_AMOUNT, MD_COUNT };

constexpr int MODE_BASE = G_COUNT;
constexpr int SLOT_BASE = MODE_BASE + NUM_MODES * M_COUNT;
constexpr int LANE_BASE = SLOT_BASE + NUM_SLOTS * S_COUNT;
constexpr int LFO_BASE = LANE_BASE + NUM_LANES * L_COUNT;
constexpr int ENV_BASE = LFO_BASE + NUM_LFOS * LF_COUNT;
constexpr int MOD_BASE = ENV_BASE + NUM_ENVS * EV_COUNT;
constexpr int NUM_PARAMS = MOD_BASE + NUM_MODS * MD_COUNT;

constexpr int modeParam (int mode, int p) { return MODE_BASE + mode * M_COUNT + p; }
constexpr int slotParam (int slot, int p) { return SLOT_BASE + slot * S_COUNT + p; }
constexpr int laneParam (int lane, int p) { return LANE_BASE + lane * L_COUNT + p; }
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
    FX_CRUSH, FX_DELAY, FX_REVERB, FX_EQ, FX_COMP, FX_CLIP, FX_TYPES
};
inline const std::vector<std::string>& fxNames()
{
    static const std::vector<std::string> n { "Leer", "Distortion", "Filter", "Chorus", "Flanger", "Phaser",
                                              "Destructor", "Bitcrush", "Delay", "Reverb", "EQ", "Kompressor", "Clipper" };
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
    switch (type)
    {
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

inline const std::vector<std::string>& driveTypes() { static const std::vector<std::string> v { "Soft", "Hard", "Fold", "Tekk" }; return v; }
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
inline const std::vector<std::string>& laneTrigs()
{
    static const std::vector<std::string> v { "Je Note", "Loop Note", "Loop Takt" };
    return v;
}

// ---------------- Startklang je Modus --------------------------------------
inline float modeDefault (int mode, int p)
{
    //                         Melo    Stab   Top     KBass   Kick
    #define D(a,b,c,d,e) { static const float t[5] = { a, b, c, d, e }; return t[mode]; }
    switch (p)
    {
        case M_WAVE:    D(1.7f,  2.0f,  1.6f,   0.25f,  0.0f)
        case M_UNI:     D(3,     5,     1,      1,      1)
        case M_DETUNE:  D(10,    18,    0,      0,      0)
        case M_WIDTH:   D(0.6f,  0.85f, 0,      0,      0)
        case M_TUNE:    D(0,     0,     0,      0,      0)
        case M_FINE:    D(0,     0,     0,      0,      0)
        case M_PAMT:    D(0,     0,     60,     24,     48)
        case M_PTIME:   D(20,    20,    18,     35,     60)
        case M_PCURVE:  D(0.5f,  0.5f,  0.7f,   0.7f,   0.7f)
        case M_ATT:     D(1,     1,     0,      0,      0)
        case M_HOLD:    D(0,     20,    5,      60,     40)
        case M_DEC:     D(450,   260,   70,     180,    320)
        case M_SUS:     D(0,     0,     0,      0,      0)
        case M_REL:     D(180,   120,   10,     20,     20)
        case M_ACURVE:  D(0.5f,  0.55f, 0.6f,   0.35f,  0.5f)
        case M_GATE:    D(0,     0,     0,      1,      0)
        case M_FTYPE:   D(0,     0,     0,      0,      0)
        case M_CUT:     D(3500,  2600,  12000,  1800,   9000)
        case M_RES:     D(0.15f, 0.25f, 0.1f,   0.1f,   0.1f)
        case M_FENV:    D(1.5f,  2.5f,  0,      0,      0)
        case M_FDEC:    D(220,   160,   60,     120,    120)
        case M_CLKLVL:  D(0,     0,     0.8f,   0.1f,   0.6f)
        case M_CLKTONE: D(3000,  3000,  4500,   2500,   3500)
        case M_CLKLEN:  D(4,     4,     8,      4,      6)
        case M_CLKNOISE:D(0.3f,  0.3f,  0.3f,   0.2f,   0.35f)
        case M_SUBLVL:  D(0,     0,     0,      0.7f,   0.35f)
        case M_SUBOCT:  D(0,     0,     0,      0,      0)
        case M_DRV:     D(0.1f,  0.2f,  0.7f,   0.5f,   0.65f)
        case M_DRVTYPE: D(0,     0,     3,      3,      3)
        case M_DRVTONE: D(14000, 12000, 7000,   5000,   7000)
        case M_CHORD:   D(0,     3,     0,      0,      0)
        case M_STRUM:   D(0,     0,     0,      0,      0)
        case M_LOWCUT:  D(120,   180,   240,    30,     30)
        case M_VEL:     D(0.3f,  0.3f,  0,      0,      0)
        case M_LEVEL:   D(-9,    -9,    -6,     -6,     -6)
        case M_OSCLVL:  D(1,     1,     1,      1,      1)
        case M_SMPLVL:  D(1,     1,     1,      1,      1)
        case M_SMPROOT: D(60,    60,    29,     29,     29)
        case M_SMPPITCH:D(1,     1,     0,      1,      0)
        case M_SMPFREE: D(1,     1,     1,      0,      1)
        default: return 0;
    }
    #undef D
}

/** Einstellungen nach dem Fertig-Rendern: Alles ist neutral, nur das Sample klingt unveraendert. */
struct FrozenValue { int index; float value; };
inline std::vector<FrozenValue> frozenValues (int mode, int note)
{
    std::vector<FrozenValue> v { { G_MODE, (float) mode }, { G_MASTER, 0 }, { G_QUALITY, 0 }, { G_LIMIT, 0 } };
    const std::pair<int, float> m[] = {
        { M_OSCLVL, 0 }, { M_CLKLVL, 0 }, { M_SUBLVL, 0 }, { M_DRV, 0 }, { M_PAMT, 0 }, { M_FENV, 0 }, { M_FTYPE, 3 },
        { M_LOWCUT, 20 }, { M_LEVEL, 0 }, { M_VEL, 0 }, { M_UNI, 1 }, { M_CHORD, 0 }, { M_STRUM, 0 }, { M_TUNE, 0 }, { M_FINE, 0 },
        { M_SMPLVL, 1 }, { M_SMPROOT, (float) note }, { M_SMPSTART, 0 }, { M_SMPTUNE, 0 }, { M_SMPLOOP, 0 }, { M_SMPFREE, 1 } };
    for (auto& e : m) v.push_back ({ modeParam (mode, e.first), e.second });
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

        ch (G_MODE, "mode", "Modus", { "Melo One-Shot", "Stab", "Topkick", "Kickbass", "Full Kick" }, FULLKICK);
        fl (G_MASTER, "master", "Master", -36, 6, 0, 1, "dB");
        ch (G_QUALITY, "quality", "Qualität", { "1x", "2x", "4x" }, 1);
        ch (G_LIMIT, "limit", "Schutz-Limiter", { "Aus", "An" }, 1);

        for (int m = 0; m < NUM_MODES; ++m)
        {
            const std::string pi = "m" + std::to_string (m) + "_", pn = std::string (modeShort (m)) + " ";
            auto F = [&] (int p, const char* id, const char* name, float mn, float mx, float skew, const char* unit)
            { fl (modeParam (m, p), pi + id, pn + name, mn, mx, modeDefault (m, p), skew, unit); };
            auto C = [&] (int p, const char* id, const char* name, std::vector<std::string> items)
            { ch (modeParam (m, p), pi + id, pn + name, std::move (items), (int) modeDefault (m, p)); };

            F (M_WAVE, "wave", "Welle", 0, 3, 1, "");
            F (M_UNI, "uni", "Unison", 1, 7, 1, "");
            F (M_DETUNE, "detune", "Verstimmung", 0, 60, 0.6f, "ct");
            F (M_WIDTH, "width", "Breite", 0, 1, 1, "");
            F (M_TUNE, "tune", "Tune", -24, 24, 1, "st");
            F (M_FINE, "fine", "Fein", -100, 100, 1, "ct");
            F (M_PAMT, "pamt", "Pitch Menge", -24, 84, 1, "st");
            F (M_PTIME, "ptime", "Pitch Zeit", 1, 800, 0.35f, "ms");
            F (M_PCURVE, "pcurve", "Pitch Kurve", 0, 1, 1, "");
            F (M_ATT, "att", "Attack", 0, 500, 0.3f, "ms");
            F (M_HOLD, "hold", "Hold", 0, 2000, 0.3f, "ms");
            F (M_DEC, "dec", "Decay", 5, 4000, 0.3f, "ms");
            F (M_SUS, "sus", "Sustain", 0, 1, 1, "");
            F (M_REL, "rel", "Release", 1, 2000, 0.3f, "ms");
            F (M_ACURVE, "acurve", "Hkurve", 0, 1, 1, "");
            C (M_GATE, "gate", "Spielart", { "One-Shot", "Gate" });
            C (M_FTYPE, "ftype", "Filterart", { "LP", "HP", "BP", "Aus" });
            F (M_CUT, "cut", "Cutoff", 20, 20000, 0.3f, "Hz");
            F (M_RES, "res", "Resonanz", 0, 1, 1, "");
            F (M_FENV, "fenv", "Filter Env", -6, 6, 1, "oct");
            F (M_FDEC, "fdec", "Filter Decay", 1, 2000, 0.35f, "ms");
            F (M_CLKLVL, "clklvl", "Click Pegel", 0, 1, 1, "");
            F (M_CLKTONE, "clktone", "Click Ton", 200, 14000, 0.35f, "Hz");
            F (M_CLKLEN, "clklen", "Click Länge", 1, 60, 0.5f, "ms");
            F (M_CLKNOISE, "clknoise", "Click Rauschen", 0, 1, 1, "");
            F (M_SUBLVL, "sublvl", "Sub Pegel", 0, 1, 1, "");
            C (M_SUBOCT, "suboct", "Sub Oktave", { "0", "-1", "-2" });
            F (M_DRV, "drv", "Drive", 0, 1, 1, "");
            C (M_DRVTYPE, "drvtype", "Drive Art", driveTypes());
            F (M_DRVTONE, "drvtone", "Drive Ton", 500, 20000, 0.35f, "Hz");
            C (M_CHORD, "chord", "Akkord", chordNames());
            F (M_STRUM, "strum", "Strum", 0, 40, 1, "ms");
            F (M_LOWCUT, "lowcut", "Lowcut", 20, 800, 0.4f, "Hz");
            F (M_VEL, "vel", "Velocity", 0, 1, 1, "");
            F (M_LEVEL, "level", "Pegel", -36, 6, 1, "dB");
            F (M_OSCLVL, "osclvl", "Osz Pegel", 0, 1, 1, "");
            F (M_SMPLVL, "smplvl", "Sample Pegel", 0, 1, 1, "");
            F (M_SMPSTART, "smpstart", "Sample Start", 0, 1, 1, "");
            F (M_SMPTUNE, "smptune", "Sample Tune", -24, 24, 1, "st");
            F (M_SMPROOT, "smproot", "Sample Grundton", 0, 127, 1, "");
            C (M_SMPPITCH, "smppitch", "Sample Tonhöhe", { "Fest", "Note", "Note + Kurve" });
            C (M_SMPLOOP, "smploop", "Sample Loop", { "Aus", "An" });
            C (M_SMPFREE, "smpfree", "Sample Länge", { "Hüllkurve", "Ganz" });
        }
        for (int s = 0; s < NUM_SLOTS; ++s)
        {
            const std::string pi = "s" + std::to_string (s) + "_", pn = "FX" + std::to_string (s + 1) + " ";
            ch (slotParam (s, S_ON), pi + "on", pn + "An", { "Aus", "An" }, 1);
            ch (slotParam (s, S_TYPE), pi + "type", pn + "Effekt", fxNames(), 0);
            fl (slotParam (s, S_MIX), pi + "mix", pn + "Mix", 0, 1, 1, 1, "");
            for (int k = 0; k < FX_PARAMS; ++k)
                fl (slotParam (s, S_P1 + k), pi + "p" + std::to_string (k + 1), pn + "Wert " + std::to_string (k + 1), 0, 1, 0.5f, 1, "");
        }
        for (int l = 0; l < NUM_LANES; ++l)
        {
            const std::string pi = "l" + std::to_string (l) + "_", pn = "Auto" + std::to_string (l + 1) + " ";
            ch (laneParam (l, L_ON), pi + "on", pn + "An", { "Aus", "An" }, 0);
            ParamDef d; d.id = pi + "target"; d.name = pn + "Ziel"; d.min = -1; d.max = (float) NUM_PARAMS - 1.0f; d.def = -1; d.kind = INT;
            t[(size_t) laneParam (l, L_TARGET)] = std::move (d);
            fl (laneParam (l, L_AMOUNT), pi + "amount", pn + "Menge", -1, 1, 0.5f, 1, "");
            ch (laneParam (l, L_RATE), pi + "rate", pn + "Länge", laneRates(), 4);
            ch (laneParam (l, L_TRIG), pi + "trig", pn + "Start", laneTrigs(), 0);
        }
        for (int i = 0; i < NUM_LFOS; ++i)
        {
            const std::string pi = "lfo" + std::to_string (i) + "_", pn = "LFO " + std::to_string (i + 1) + " ";
            ch (lfoParam (i, LF_SHAPE), pi + "shape", pn + "Form", { "Sinus", "Dreieck", "Säge", "Rechteck", "Zufall" }, 0);
            ch (lfoParam (i, LF_SYNC), pi + "sync", pn + "Takt", { "Frei", "Takt" }, 1);
            fl (lfoParam (i, LF_HZ), pi + "hz", pn + "Tempo", 0.02f, 40, 2.0f, 0.35f, "Hz");
            ch (lfoParam (i, LF_DIV), pi + "div", pn + "Teiler", laneRates(), 5);
            ch (lfoParam (i, LF_RETRIG), pi + "retrig", pn + "Neustart", { "Frei", "Je Note" }, 1);
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
        return t;
    }();
    return table;
}
} // namespace tk
