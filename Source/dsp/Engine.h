// Tekkforge - Klangerzeugung. Jede Stimme spielt drei Ebenen. LFOs und Envs laufen wie in Vital pro Stimme.
#pragma once
#include "Eq.h"
#include <array>
#include <atomic>

namespace tk
{
struct SampleData
{
    std::vector<float> l, r;      // r leer = mono
    float rate = 44100;
    std::string name;
    int length() const { return (int) l.size(); }
};

constexpr int NUM_VOICES = 16;
constexpr int MAX_UNISON = 7;
using Curves = std::array<std::array<std::atomic<float>, LFO_POINTS>, NUM_LFOS>;

inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
/** Wellen-Morph 0..4 = Sinus > Dreieck > Saegezahn > Rechteck > Rauschen */
inline float morphOsc (float ph, float dt, float wave, float noise = 0.0f)
{
    const auto one = [&] (int w) -> float
    {
        switch (w)
        {
            case 0: return std::sin (2.0f * kPi * ph);
            case 1: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
            case 2: return 2.0f * ph - 1.0f - polyBlep (ph, dt);
            case 3:
            {
                float y = ph < 0.5f ? 1.0f : -1.0f;
                y += polyBlep (ph, dt);
                float p2 = ph + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
                return y - polyBlep (p2, dt);
            }
            default: return noise;
        }
    };
    const int a = std::min ((int) wave, 3);
    const float f = wave - (float) a;
    if (f <= 0.0001f) return one (a);
    if (f >= 0.9999f) return one (a + 1);
    return one (a) * (1.0f - f) + one (a + 1) * f;
}

/** Ein Satz Modulationsquellen. Jede Stimme hat einen eigenen, dazu einer fuer Effekte und Master. */
struct ModState
{
    float value[NUM_SOURCES] {};
    double lfoPhase[NUM_LFOS] {};
    bool lfoDone[NUM_LFOS] {};
    float envLevel[NUM_ENVS] {}, envTime[NUM_ENVS] {}, envStart[NUM_ENVS] {};
    bool envRel[NUM_ENVS] { true, true, true };

    void noteOn (const float* v)
    {
        for (int i = 0; i < NUM_LFOS; ++i)
            if ((int) v[lfoParam (i, LF_TRIG)] > 0) { lfoPhase[i] = 0; lfoDone[i] = false; }
        for (int i = 0; i < NUM_ENVS; ++i) { envTime[i] = 0; envRel[i] = false; envStart[i] = envLevel[i]; }
    }
    void noteOff() { for (auto& r : envRel) r = true; }
    void clear() { *this = ModState(); }

    void update (const float* v, const Curves* curves, int n, float sr, double bpm, double ppq)
    {
        const float dt = (float) n / sr;
        for (int i = 0; i < NUM_LFOS; ++i)
        {
            const int trig = (int) v[lfoParam (i, LF_TRIG)];
            double ph = lfoPhase[i];
            if (v[lfoParam (i, LF_SYNC)] > 0.5f)
            {
                const double beats = laneBeats ((int) v[lfoParam (i, LF_DIV)]);
                if (trig == 0 && ppq >= 0) ph = std::fmod (ppq / beats, 1.0);
                else ph += (double) n / (beats * 60.0 / std::max (bpm, 20.0) * sr);
            }
            else ph += (double) v[lfoParam (i, LF_HZ)] * (double) dt;
            if (ph >= 1.0)
            {
                if (trig == 2) { ph = 1.0; lfoDone[i] = true; }
                else ph = std::fmod (ph, 1.0);
            }
            lfoPhase[i] = ph;
            float y = 0;
            if (curves != nullptr)
            {
                const float x = (float) ph * (float) (LFO_POINTS - 1);
                const int a = std::min ((int) x, LFO_POINTS - 2);
                const float fr = x - (float) a;
                const auto& c = (*curves)[(size_t) i];
                y = c[(size_t) a].load (std::memory_order_relaxed) * (1 - fr) + c[(size_t) a + 1].load (std::memory_order_relaxed) * fr;
            }
            value[i] = y;
        }
        for (int i = 0; i < NUM_ENVS; ++i)
        {
            const float a = v[envParam (i, EV_ATT)] * 0.001f, de = v[envParam (i, EV_DEC)] * 0.001f, su = v[envParam (i, EV_SUS)];
            const float re = v[envParam (i, EV_REL)] * 0.001f, c = 0.6f + v[envParam (i, EV_CURVE)] * 5.4f;
            if (envRel[i]) envLevel[i] *= std::exp (-6.9f * dt / std::max (re, 0.001f));
            else
            {
                envTime[i] += dt; const float t = envTime[i];
                if (a > 0 && t < a) envLevel[i] = envStart[i] + (1.0f - envStart[i]) * t / a;
                else if (t < a + de) envLevel[i] = su + (1.0f - su) * std::pow (1.0f - (t - a) / de, c);
                else envLevel[i] = su;
            }
            value[NUM_LFOS + i] = envLevel[i];
        }
    }
};

class Voice
{
public:
    bool active = false, stolen = false, heldDown = false;
    int note = -1, playedNote = -1;
    float vel = 1;
    uint64_t age = 0;
    ModState mod;
    float pv[M_COUNT] {};          // Parameter dieser Stimme nach der Modulation

    void start (int midiNote, int played, float velocity, int delaySamples, uint64_t order, unsigned seed, const float* values, int mode)
    {
        active = true; stolen = false; heldDown = true;
        note = midiNote; playedNote = played; vel = velocity; age = order;
        delay = delaySamples; fade = 1; rng = seed | 1u;
        mod.noteOn (values);
        const float* p = values + modeParam (mode, 0);
        for (int l = 0; l < NUM_LAYERS; ++l)
        {
            auto& y = lay[l]; const float* q = p + Y (l, 0);
            y = Layer();
            y.alive = true;
            const bool spread = q[LY_UNI] > 1.5f;
            for (int i = 0; i < MAX_UNISON; ++i) y.ph[i] = spread ? rand01() : 0.0f;
        }
    }
    void release() { if (! active) return; heldDown = false; mod.noteOff(); }
    void steal() { stolen = true; }

    /** out[Ebene][Kanal]. Parameter stehen in pv. smp zeigt auf die drei Samples dieses Modus. */
    void render (float* const (*out)[2], int n, float sr, const SampleData* const* smp, bool anySolo)
    {
        int startAt = 0;
        if (delay > 0) { startAt = std::min (delay, n); delay -= startAt; if (startAt == n) return; }
        const float baseHz = 440.0f * std::pow (2.0f, ((float) note - 69.0f + pv[M_TUNE] + pv[M_FINE] * 0.01f) / 12.0f);
        const float velGain = dbToGain (pv[M_LEVEL]) * (1.0f - pv[M_VEL] * (1.0f - vel));
        const float stealC = std::exp (-6.9f / (0.003f * sr));
        float fadeEnd = fade;
        bool any = false;
        for (int l = 0; l < NUM_LAYERS; ++l)
        {
            fadeEnd = fade;
            auto& y = lay[l];
            if (! y.alive) continue;
            const float* q = pv + Y (l, 0);
            const bool silent = q[LY_MUTE] > 0.5f || (anySolo && q[LY_SOLO] < 0.5f) || q[LY_LVL] + q[LY_SUB] <= 0.0f;
            renderLayer (y, q, smp != nullptr ? smp[l] : nullptr, out[l][0], out[l][1], startAt, n, sr, keyHz (q, baseHz), velGain, silent, stealC, fadeEnd);
            any = any || y.alive;
        }
        fade = fadeEnd;
        if (stolen && fade < 0.001f) any = false;
        active = any;
    }

private:
    /** Tonlage der Ebene: Bass-Lage faltet jede Note in 40 bis 80 Hz, Fest ignoriert die Taste. */
    static float keyHz (const float* q, float hz)
    {
        const int k = (int) q[LY_KEY];
        if (k == 1) { while (hz >= 80.0f) hz *= 0.5f; while (hz < 40.0f) hz *= 2.0f; }
        else if (k == 2) hz = 440.0f * std::pow (2.0f, (q[LY_ROOT] - 69.0f) / 12.0f);
        return hz;
    }
    struct Layer
    {
        bool alive = false, released = false, smpDone = false;
        int t = 0, tail = 0;
        float ph[MAX_UNISON] {}, sub = 0, relLevel = 1, relSmp = 1, lastEnv = 0;
        double smpPos = -1.0;
        OnePole toneL, toneR, lc[4], monoL, monoR;
        Svf f[4], fm[6];
        float comb[2][2048] {}, combLen = 100.0f, lastOut = 0, h2 = 0, h3 = 0, h5 = 0, glide = 0;
        int combPos = 0;
        Biquad pre[2];
    };

    void renderLayer (Layer& y, const float* q, const SampleData* s, float* L, float* R, int from, int n, float sr, float root, float velGain, bool silent, float stealC, float& fadeOut)
    {
        const bool holdMode = q[LY_HOLD] > 0.5f;
        if (! heldDown && holdMode && ! y.released) { y.released = true; y.relLevel = y.lastEnv; y.relSmp = 1; }
        const float tA = std::max (q[LY_ATT], 0.2f) * 0.001f * sr, tD = std::max (q[LY_LEN] * 0.001f * sr, 1.0f);
        const float sus = holdMode ? q[LY_SUS] : 0.0f, aC = 0.6f + q[LY_CURVE] * 5.4f;
        const float relC = std::exp (-6.9f / std::max (1.0f, q[LY_REL] * 0.001f * sr));
        const float pT = q[LY_PTIME] * 0.001f * sr, pAmt = q[LY_PAMT], pC = 1.0f + q[LY_PCURVE] * 9.0f;
        const int ftype = (int) q[LY_FTYPE], dT = (int) q[LY_DRVTYPE], d2T = (int) q[LY_DRV2TYPE];
        const int table = std::clamp ((int) q[LY_TABLE], 0, NUM_TABLES - 1), warp = (int) q[LY_WARP];
        const float warpA = q[LY_WARPAMT], pos = q[LY_WAVE], glide = q[LY_GLIDE], h2L = q[LY_H2], h3L = q[LY_H3], h5L = q[LY_H5];
        const auto& bank = WavetableBank::get();
        const bool tableOk = table != WT_NOISE_INDEX && bank.ready (table), isNoise = table == WT_NOISE_INDEX;
        const float phaseSign = q[LY_PHASE] > 0.5f ? -1.0f : 1.0f;
        const bool preOn = q[LY_PREAMT] > 0.01f;
        if (preOn) { y.pre[0].set (Biquad::PEAK, q[LY_PREF], q[LY_PREAMT], 1.2f, sr); y.pre[1].set (Biquad::PEAK, q[LY_PREF], q[LY_PREAMT], 1.2f, sr); }
        const float drv2 = q[LY_DRV2], d2Gain = std::pow (10.0f, drv2 * 36.0f / 20.0f), d2Comp = 1.0f / (1.0f + 1.2f * drv2);
        const bool monoOn = q[LY_MONO] > 20.5f;
        if (monoOn) { y.monoL.set (q[LY_MONO], sr); y.monoR.set (q[LY_MONO], sr); }
        const float combRes = 0.5f + q[LY_RES] * 0.48f;
        if (ftype == 8)
            for (int k = 0; k < 3; ++k)
                for (int c = 0; c < 2; ++c) y.fm[k * 2 + c].set (formantHz (q[LY_CUT], k), 0.7f + q[LY_RES] * 0.28f, sr);
        const float drv = q[LY_DRV], dGain = std::pow (10.0f, drv * 40.0f / 20.0f), dComp = 1.0f / (1.0f + 1.2f * drv);
        const float subL = q[LY_SUB], subDiv = std::pow (2.0f, -std::round (q[LY_SUBOCT]));
        const float mul = std::pow (2.0f, (q[LY_TUNE] + q[LY_FINE] * 0.01f) / 12.0f);
        const float panV = std::clamp (q[LY_PAN], -1.0f, 1.0f);
        const float gl = std::sqrt (0.5f * (1.0f - panV)) * 1.4142f, gr = std::sqrt (0.5f * (1.0f + panV)) * 1.4142f;
        const bool lowcut = q[LY_LOWCUT] > 20.5f;
        if (drv > 0.0001f) { y.toneL.set (q[LY_DRVTONE], sr); y.toneR.set (q[LY_DRVTONE], sr); }
        if (lowcut) for (auto& c : y.lc) c.set (q[LY_LOWCUT], sr);

        const bool useSample = q[LY_SRC] > 0.5f && s != nullptr && s->length() >= 2;
        const int smpMode = (int) q[LY_PITCH], smpLen = useSample ? s->length() : 0;
        const bool loop = q[LY_LOOP] > 0.5f, freeLen = useSample && q[LY_FREE] > 0.5f && ! loop;
        const double smpStart = (double) std::clamp (q[LY_START], 0.0f, 0.999f) * (double) smpLen;
        double smpBase = 0;
        if (useSample)
        {
            const float st = q[LY_TUNE] + q[LY_FINE] * 0.01f + (smpMode > 0 ? (float) note - q[LY_ROOT] + pv[M_TUNE] + pv[M_FINE] * 0.01f : 0.0f);
            smpBase = (double) s->rate / (double) sr * std::pow (2.0, (double) st / 12.0);
            if (y.smpPos < 0) y.smpPos = smpStart;
        }
        const int uni = useSample ? 1 : std::clamp ((int) std::round (q[LY_UNI]), 1, MAX_UNISON);
        const float uniNorm = 1.0f / std::sqrt ((float) uni);
        float det[MAX_UNISON], pl[MAX_UNISON], pr[MAX_UNISON];
        for (int u = 0; u < uni; ++u)
        {
            const float x = uni > 1 ? 2.0f * (float) u / (float) (uni - 1) - 1.0f : 0.0f;
            det[u] = std::pow (2.0f, x * q[LY_DETUNE] / 1200.0f);
            const float pan = x * q[LY_WIDTH];
            pl[u] = std::sqrt (0.5f * (1.0f - pan)) * 1.4142f; pr[u] = std::sqrt (0.5f * (1.0f + pan)) * 1.4142f;
        }

        float fadeNow = fade;
        for (int i = from; i < n; ++i)
        {
            const float tf = (float) y.t;
            if ((y.t & 15) == 0 && ftype < 8)
            {
                const float fe = std::exp (-tf / std::max (1.0f, q[LY_FDEC] * 0.001f * sr));
                const float hz = q[LY_CUT] * std::pow (2.0f, q[LY_FAMT] * fe);
                for (auto& fl : y.f) fl.set (hz, q[LY_RES], sr);
                y.combLen = std::clamp (sr / std::max (hz, 30.0f), 2.0f, 2040.0f);
            }
            float env;
            if (y.released) { y.relLevel *= relC; env = y.relLevel; }
            else if (tf < tA) env = tf / tA;
            else if (tf < tA + tD) { const float x = (tf - tA) / tD; env = sus + (1.0f - sus) * std::pow (1.0f - x, aC); }
            else env = sus;
            y.lastEnv = env;

            float semis = 0;
            if (pAmt != 0.0f && tf < pT) semis = pAmt * std::pow (1.0f - tf / pT, pC);
            float bend = semis != 0.0f ? std::pow (2.0f, semis / 12.0f) : 1.0f;
            if (glide != 0.0f) bend *= std::pow (2.0f, glide * std::min (tf / tD, 1.0f) / 12.0f);   // Tail Pitch ueber die Laenge
            const float freq = root * bend;

            float l = 0, r = 0;
            if (useSample)
            {
                if (! y.smpDone)
                {
                    const int a = (int) y.smpPos, b = a + 1 < smpLen ? a + 1 : (loop ? 0 : a);
                    const float fr = (float) (y.smpPos - (double) a);
                    const float sl = s->l[(size_t) a] + (s->l[(size_t) b] - s->l[(size_t) a]) * fr;
                    const float sr2 = s->r.empty() ? sl : s->r[(size_t) a] + (s->r[(size_t) b] - s->r[(size_t) a]) * fr;
                    const float g = freeLen ? y.relSmp : env;
                    l = sl * g; r = sr2 * g;
                    y.smpPos += smpBase * (smpMode == 2 ? (double) bend : 1.0);
                    if (y.smpPos >= (double) smpLen)
                    {
                        if (loop) y.smpPos = smpStart + std::fmod (y.smpPos - (double) smpLen, std::max (1.0, (double) smpLen - smpStart));
                        else y.smpDone = true;
                    }
                }
            }
            else
            {
                const float hz = freq * mul;
                for (int u = 0; u < uni; ++u)
                {
                    const float dt = std::min (hz * det[u] / sr, 0.49f);
                    const float rp = warp == 0 ? y.ph[u] : warpPhase (y.ph[u], warp, warpA, y.lastOut);
                    const float o = isNoise ? rand01() * 2.0f - 1.0f : tableOk ? bank.read (table, pos, rp, dt) : std::sin (2.0f * kPi * rp);
                    y.lastOut = o;
                    y.ph[u] += dt; if (y.ph[u] >= 1.0f) y.ph[u] -= 1.0f;
                    l += o * pl[u]; r += o * pr[u];
                }
                l *= uniNorm * env; r *= uniNorm * env;
            }
            if (y.released) y.relSmp *= relC;
            l *= q[LY_LVL]; r *= q[LY_LVL];

            // Filter
            float lo, hi, ba;
            const auto one = [&] (int c, float x)
            {
                switch (ftype)
                {
                    case 0: y.f[c].tick (x, lo, hi, ba); return lo;
                    case 1: y.f[c].tick (x, lo, hi, ba); y.f[c + 2].tick (lo, lo, hi, ba); return lo;
                    case 2: y.f[c].tick (x, lo, hi, ba); return hi;
                    case 3: y.f[c].tick (x, lo, hi, ba); y.f[c + 2].tick (hi, lo, hi, ba); return hi;
                    case 4: y.f[c].tick (x, lo, hi, ba); return ba;
                    case 5: y.f[c].tick (x, lo, hi, ba); return lo + hi;
                    case 6: case 7:
                    {
                        auto& line = y.comb[c];
                        float pp = (float) y.combPos - y.combLen; if (pp < 0) pp += 2048.0f;
                        const int a = (int) pp & 2047; const float fr2 = pp - std::floor (pp);
                        const float d = line[a] + (line[(a + 1) & 2047] - line[a]) * fr2;
                        line[y.combPos] = softClip (x + (ftype == 6 ? combRes : -combRes) * d);
                        return 0.5f * (x + (ftype == 6 ? d : -d));
                    }
                    case 8:
                    {
                        float sum = 0;
                        for (int k = 0; k < 3; ++k) { y.fm[k * 2 + c].tick (x, lo, hi, ba); sum += ba * (k == 0 ? 1.0f : 0.7f); }
                        return sum * 1.2f;
                    }
                    default: return x;
                }
            };
            l = one (0, l); r = one (1, r);
            y.combPos = (y.combPos + 1) & 2047;

            // Zweistufige Verzerrung: Boost davor gibt den Stufen einen Bereich, den sie aufreissen
            if (preOn) { l = y.pre[0].process (l); r = y.pre[1].process (r); }
            if (drv > 0.0001f)
            {
                l = y.toneL.lp (shape (l * dGain, dT) * dComp);
                r = y.toneR.lp (shape (r * dGain, dT) * dComp);
            }
            if (drv2 > 0.0001f) { l = shape (l * d2Gain, d2T) * d2Comp; r = shape (r * d2Gain, d2T) * d2Comp; }
            if (subL > 0.0f)
            {
                const float o = std::sin (2.0f * kPi * y.sub) * subL * env;
                y.sub += std::min (freq * mul * subDiv / sr, 0.49f); if (y.sub >= 1.0f) y.sub -= 1.0f;
                l += o; r += o;
            }
            // Zusaetzliche Obertoene ueber dem Grundton, sauber nach der Verzerrung
            if (h2L + h3L + h5L > 0.0f)
            {
                const float d0 = std::min (freq * mul / sr, 0.08f);
                y.h2 += d0 * 2; y.h3 += d0 * 3; y.h5 += d0 * 5;
                y.h2 -= std::floor (y.h2); y.h3 -= std::floor (y.h3); y.h5 -= std::floor (y.h5);
                const float o = env * (h2L * std::sin (2 * kPi * y.h2) + h3L * std::sin (2 * kPi * y.h3) + h5L * std::sin (2 * kPi * y.h5));
                l += o; r += o;
            }
            if (monoOn)
            {
                // Unter der Grenze bleibt nur die Mitte
                const float s = 0.5f * (l - r);
                const float lowS = y.monoL.lp (s);
                l -= lowS; r += lowS;
            }
            l *= phaseSign; r *= phaseSign;
            if (lowcut) { l = y.lc[1].hp (y.lc[0].hp (l)); r = y.lc[3].hp (y.lc[2].hp (r)); }
            if (stolen) fadeNow *= stealC;
            if (! silent)
            {
                const float g = velGain * fadeNow;
                L[i] += l * g * gl; R[i] += r * g * gr;
            }
            ++y.t;

            bool ended = y.released ? y.relLevel < 0.0003f : (! holdMode || sus <= 0.0f) && tf >= tA + tD;
            if (ended && freeLen && ! y.smpDone && y.relSmp > 0.0003f) ended = false;
            if (ended && ++y.tail > (int) (0.05f * sr)) { y.alive = false; break; }
        }
        fadeOut = fadeNow;
    }
    float rand01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng & 0xffffff) / 16777216.0f; }

    Layer lay[NUM_LAYERS];
    float fade = 1;
    int delay = 0;
    unsigned rng = 1;
};

class Engine
{
public:
    Curves* curves = nullptr;
    std::array<std::atomic<float>, NUM_PARAMS>* modView = nullptr;     // aktueller Wert 0..1 inkl. Modulation (fuer die Knobs)
    const std::atomic<const SampleData*>* samples = nullptr;           // sampleSlot (Modus, Ebene)
    std::atomic<float> sideLevel { 0.0f };                             // Pegel am Sidechain-Eingang
    std::atomic<float> lfoPos[NUM_LFOS];
    std::atomic<float> sourceView[NUM_SOURCES];                        // aktueller Wert jeder Quelle

    Engine()
    {
        for (auto& l : lfoPos) l.store (0.0f);
        for (auto& l : sourceView) l.store (0.0f);
    }

    void allocate (float maxSampleRate) { for (auto& s : slots) s.allocate (maxSampleRate); WavetableBank::get().prepare(); }

    void setSampleRate (float s)
    {
        sr = s;
        for (auto& x : slots) x.setSampleRate (s);
        for (auto& x : eqs) x.setSampleRate (s);
        for (auto& v : voices) v.active = false;
    }
    void reset()
    {
        for (auto& v : voices) { v.active = false; v.mod.clear(); }
        for (auto& x : slots) x.reset();
        for (auto& x : eqs) x.reset();
        global.clear();
        held = 0;
    }

    void noteOn (int note, float vel, const float* values)
    {
        const int mode = (int) values[G_MODE];
        const float* p = values + modeParam (mode, 0);
        if (mode != lastMode) { lastMode = mode; for (auto& v : voices) if (v.active) v.steal(); }
        if (modeIsMono (mode)) for (auto& v : voices) if (v.active) v.steal();

        int iv[5];
        const int count = modeIsMono (mode) ? (iv[0] = 0, 1) : chordIntervals ((int) p[M_CHORD], iv);
        for (int k = 0; k < count; ++k)
        {
            Voice* slot = nullptr;
            for (auto& v : voices) if (! v.active) { slot = &v; break; }
            if (slot == nullptr)
            {
                slot = &voices[0];
                for (auto& v : voices) if (v.age < slot->age) slot = &v;
            }
            slot->start (note + iv[k], note, vel, (int) ((float) k * p[M_STRUM] * 0.001f * sr), ++order, seed += 0x9e3779b9u, values, mode);
        }
        ++held; ++fxCtx.notes;
        global.noteOn (values);
    }
    void noteOff (int note, const float*)
    {
        if (held > 0 && --held == 0) global.noteOff();
        for (auto& v : voices) if (v.active && v.playedNote == note) v.release();
    }
    void allNotesOff()
    {
        for (auto& v : voices) if (v.active) v.steal();
        held = 0; global.noteOff();
    }

    /** Schreibt n Samples. values = reale Parameterwerte. bpm/ppq vom Host (ppq < 0 = kein Transport). */
    void process (float* L, float* R, int n, const float* values, double bpm, double ppq)
    {
        std::memset (L, 0, sizeof (float) * (size_t) n);
        std::memset (R, 0, sizeof (float) * (size_t) n);
        const int mode = (int) values[G_MODE];
        if (mode != lastMode) { lastMode = mode; for (auto& v : voices) if (v.active) v.steal(); }

        const int tick = std::min (MAX_CHUNK, std::max (8, (int) (sr * 0.0006f)));
        int done = 0;
        while (done < n)
        {
            const int c = std::min (tick, n - done);
            const double ppqNow = ppq < 0 ? -1.0 : ppq + (double) done / sr * bpm / 60.0;
            renderChunk (L + done, R + done, c, values, mode, bpm, ppqNow);
            done += c;
        }
    }
    int activeVoices() const { int c = 0; for (auto& v : voices) c += v.active ? 1 : 0; return c; }

private:
    static float limiter (float x)   // bis etwa -1 dBFS linear, danach weich gegen 0 dBFS
    {
        const float k = 0.9f, a = std::fabs (x);
        if (a <= k) return x;
        const float y = k + (1.0f - k) * softClip ((a - k) / (1.0f - k));
        return x < 0 ? -y : y;
    }

    void renderChunk (float* L, float* R, int c, const float* values, int mode, double bpm, double ppq)
    {
        const auto& table = params();
        const int modeBase = modeParam (mode, 0);
        global.update (values, curves, c, sr, bpm, ppq);
        for (int i = 0; i < NUM_SOURCES; ++i) sourceView[i].store (global.value[i], std::memory_order_relaxed);
        for (int i = 0; i < NUM_LFOS; ++i) lfoPos[i].store ((float) global.lfoPhase[i], std::memory_order_relaxed);

        // Verbindungen einmal auslesen, damit der Audio-Thread nicht jede Stimme einzeln durchsucht
        struct Link { int target, source; float amount; };
        Link links[NUM_MODS]; int count = 0;
        for (int m = 0; m < NUM_MODS; ++m)
        {
            const int t = (int) values[modSlot (m, MD_TARGET)];
            if (t < 0 || t >= NUM_PARAMS || ! table[(size_t) t].modulatable()) continue;
            links[count++] = { t, (int) values[modSlot (m, MD_SOURCE)], values[modSlot (m, MD_AMOUNT)] };
        }
        const auto apply = [&] (float& v, const ParamDef& d, const ModState& s, int src, float amt)
        { v = d.fromNorm (d.toNorm (v) + amt * s.value[src]); };

        // Stimmen: Quellen fortschreiben, Parameter dieser Stimme modulieren, Ebenen rendern
        float buf[NUM_LAYERS][2][MAX_CHUNK];
        std::memset (buf, 0, sizeof (float) * (size_t) (NUM_LAYERS * 2) * (size_t) MAX_CHUNK);
        float* const out[NUM_LAYERS][2] = { { buf[0][0], buf[0][1] }, { buf[1][0], buf[1][1] }, { buf[2][0], buf[2][1] } };
        const SampleData* smp[NUM_LAYERS] { nullptr, nullptr, nullptr };
        if (samples != nullptr)
            for (int l = 0; l < NUM_LAYERS; ++l) smp[l] = samples[sampleSlot (mode, l)].load (std::memory_order_acquire);

        Voice* lead = nullptr;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            v.mod.update (values, curves, c, sr, bpm, ppq);
            std::copy (values + modeBase, values + modeBase + M_COUNT, v.pv);
            for (int i = 0; i < count; ++i)
                if (const int k = links[i].target - modeBase; k >= 0 && k < M_COUNT)
                    apply (v.pv[k], table[(size_t) links[i].target], v.mod, links[i].source, links[i].amount);
            bool anySolo = false;
            for (int l = 0; l < NUM_LAYERS; ++l) anySolo = anySolo || v.pv[Y (l, LY_SOLO)] > 0.5f;
            v.render (out, c, sr, smp, anySolo);
            if (lead == nullptr || v.age > lead->age) lead = &v;
        }

        // Alles ausserhalb der Stimmen (Effekte, Master, Env-Regler) folgt der juengsten Note
        std::copy (values, values + NUM_PARAMS, cur);
        for (int i = 0; i < count; ++i)
        {
            const int t = links[i].target, k = t - modeBase;
            if (k >= 0 && k < M_COUNT) continue;
            apply (cur[t], table[(size_t) t], lead != nullptr ? lead->mod : global, links[i].source, links[i].amount);
        }
        if (modView != nullptr)
            for (int i = 0; i < count; ++i)
            {
                const int t = links[i].target, k = t - modeBase;
                const float now = k >= 0 && k < M_COUNT ? (lead != nullptr ? lead->pv[k] : values[t]) : cur[t];
                (*modView)[(size_t) t].store (table[(size_t) t].toNorm (now), std::memory_order_relaxed);
            }

        // Takt fuer den Sidechain: laeuft auch bei gestopptem Transport weiter
        freePpq += (double) c / sr * bpm / 60.0;
        fxCtx.ppq = ppq >= 0 ? ppq : freePpq;
        fxCtx.sideLevel = sideLevel.load (std::memory_order_relaxed);
        for (auto& x : slots) x.ctx = &fxCtx;

        for (int l = 0; l < NUM_LAYERS; ++l)
        {
            for (int s = 0; s < SLOTS_PER_LAYER; ++s)
            {
                const int i = layerSlot (l, s);
                slots[(size_t) i].process (buf[l][0], buf[l][1], c, cur + slotParam (i, 0));
            }
            eqs[(size_t) l + 1].process (buf[l][0], buf[l][1], c, cur + eqParam (l + 1, 0, 0));
            for (int i = 0; i < c; ++i) { L[i] += buf[l][0][i]; R[i] += buf[l][1][i]; }
        }
        for (int s = 0; s < NUM_MASTER_SLOTS; ++s) slots[(size_t) s].process (L, R, c, cur + slotParam (s, 0));
        eqs[0].process (L, R, c, cur + eqParam (0, 0, 0));

        const float target = dbToGain (cur[G_MASTER]), step = (target - gainNow) / (float) c;
        const bool lim = values[G_LIMIT] > 0.5f;
        for (int i = 0; i < c; ++i)
        {
            gainNow += step;
            float l = L[i] * gainNow, r = R[i] * gainNow;
            if (lim) { l = limiter (l); r = limiter (r); }
            L[i] = l; R[i] = r;
        }
    }

    float sr = 44100, gainNow = 1;
    float cur[NUM_PARAMS] {};
    int lastMode = -1, held = 0;
    ModState global;
    FxContext fxCtx;
    double freePpq = 0;
    uint64_t order = 0;
    unsigned seed = 0x12345;
    std::array<Voice, NUM_VOICES> voices;
    std::array<FxSlot, NUM_SLOTS> slots;
    std::array<ProEq, NUM_EQS> eqs;
};
} // namespace tk
