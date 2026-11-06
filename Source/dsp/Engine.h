// Tekkforge - Klangerzeugung: Stimmen, interne Automation, FX-Kette.
#pragma once
#include "Effects.h"
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

inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
/** Wellen-Morph 0..3 = Sinus > Dreieck > Saegezahn > Rechteck */
inline float morphOsc (float ph, float dt, float wave)
{
    const auto one = [&] (int w) -> float
    {
        switch (w)
        {
            case 0: return std::sin (2.0f * kPi * ph);
            case 1: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
            case 2: return 2.0f * ph - 1.0f - polyBlep (ph, dt);
            default:
            {
                float y = ph < 0.5f ? 1.0f : -1.0f;
                y += polyBlep (ph, dt);
                float p2 = ph + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
                return y - polyBlep (p2, dt);
            }
        }
    };
    const int a = std::min ((int) wave, 2);
    const float f = wave - (float) a;
    if (f <= 0.0001f) return one (a);
    if (f >= 0.9999f) return one (a + 1);
    return one (a) * (1.0f - f) + one (a + 1) * f;
}

class Voice
{
public:
    bool active = false, released = false, stolen = false;
    int note = -1, playedNote = -1;
    float vel = 1;
    uint64_t age = 0;

    void start (int midiNote, int played, float velocity, int delaySamples, uint64_t order, unsigned seed, int unisonCount)
    {
        active = true; released = false; stolen = false;
        note = midiNote; playedNote = played; vel = velocity; age = order;
        t = 0; tailCount = 0; smpPos = -1.0; smpDone = false; relSmp = 1; delay = delaySamples; relLevel = 1; lastEnv = 0; fade = 1; rng = seed | 1u;
        sub = 0; clkPh = 0; clkHp.reset(); toneL.reset(); toneR.reset(); lc[0].reset(); lc[1].reset();
        f[0].reset(); f[1].reset();
        for (int i = 0; i < MAX_UNISON; ++i) ph[i] = unisonCount > 1 ? rand01() : 0.0f;
    }
    void release() { if (active && ! released) { released = true; relLevel = lastEnv; relSmp = 1; } }
    void steal() { stolen = true; }

    void render (float* L, float* R, int n, const float* p, float sr, const SampleData* smp)
    {
        const float wave = std::clamp (p[M_WAVE], 0.0f, 3.0f);
        const int uni = std::clamp ((int) std::round (p[M_UNI]), 1, MAX_UNISON);
        const float det = p[M_DETUNE], width = p[M_WIDTH];
        const float root = 440.0f * std::pow (2.0f, ((float) note - 69.0f + p[M_TUNE] + p[M_FINE] * 0.01f) / 12.0f);
        const float pT = p[M_PTIME] * 0.001f * sr, pAmt = p[M_PAMT], pC = 1.0f + p[M_PCURVE] * 9.0f;
        const float tA = std::max (p[M_ATT], 0.2f) * 0.001f * sr, tH = p[M_HOLD] * 0.001f * sr, tD = p[M_DEC] * 0.001f * sr;
        const bool gate = p[M_GATE] > 0.5f;
        const float sus = gate ? p[M_SUS] : 0.0f, aC = 0.6f + p[M_ACURVE] * 5.4f;
        const float relC = std::exp (-6.9f / std::max (1.0f, p[M_REL] * 0.001f * sr));
        const float stealC = std::exp (-6.9f / (0.003f * sr));
        const int ftype = (int) p[M_FTYPE];
        const float clkL = p[M_CLKLVL], clkN = p[M_CLKNOISE], clkT = p[M_CLKTONE], clkTau = p[M_CLKLEN] * 0.001f * sr;
        const float subL = p[M_SUBLVL], subDiv = std::pow (2.0f, -std::round (p[M_SUBOCT]));
        const float drv = p[M_DRV], dGain = std::pow (10.0f, drv * 40.0f / 20.0f), dComp = 1.0f / (1.0f + 0.9f * drv * drv);
        const int dT = (int) p[M_DRVTYPE];
        const float gain = dbToGain (p[M_LEVEL]) * (1.0f - p[M_VEL] * (1.0f - vel));
        const float uniNorm = 1.0f / std::sqrt ((float) uni);
        const float oscL = p[M_OSCLVL], smpL = smp != nullptr ? p[M_SMPLVL] : 0.0f;
        const int smpMode = (int) p[M_SMPPITCH], smpLen = smp != nullptr ? smp->length() : 0;
        const bool smpLoop = p[M_SMPLOOP] > 0.5f, smpFree = smp != nullptr && p[M_SMPFREE] > 0.5f && ! smpLoop;
        const double smpStart = (double) std::clamp (p[M_SMPSTART], 0.0f, 0.999f) * (double) smpLen;
        double smpBase = 0;
        if (smp != nullptr)
        {
            const float st = p[M_SMPTUNE] + (smpMode > 0 ? (float) note - p[M_SMPROOT] + p[M_TUNE] + p[M_FINE] * 0.01f : 0.0f);
            smpBase = (double) smp->rate / (double) sr * std::pow (2.0, (double) st / 12.0);
            if (smpPos < 0) smpPos = smpStart;
        }

        clkHp.set (clkT * 0.5f, sr);
        toneL.set (p[M_DRVTONE], sr); toneR.set (p[M_DRVTONE], sr);
        lc[0].set (p[M_LOWCUT], sr); lc[1].set (p[M_LOWCUT], sr);

        float detMul[MAX_UNISON], panL[MAX_UNISON], panR[MAX_UNISON];
        for (int u = 0; u < uni; ++u)
        {
            const float x = uni > 1 ? 2.0f * (float) u / (float) (uni - 1) - 1.0f : 0.0f;
            detMul[u] = std::pow (2.0f, x * det / 1200.0f);
            const float pan = x * width;
            panL[u] = std::sqrt (0.5f * (1.0f - pan)) * 1.4142f; panR[u] = std::sqrt (0.5f * (1.0f + pan)) * 1.4142f;
        }

        for (int i = 0; i < n; ++i)
        {
            if (delay > 0) { --delay; continue; }

            // Filter-Hullkurve wird alle 16 Samples neu gerechnet
            if ((t & 15) == 0)
            {
                const float fe = std::exp (-t / std::max (1.0f, p[M_FDEC] * 0.001f * sr));
                const float hz = p[M_CUT] * std::pow (2.0f, p[M_FENV] * fe);
                f[0].set (hz, p[M_RES], sr); f[1].set (hz, p[M_RES], sr);
            }

            // Lautstaerke
            float env; const float tf = (float) t;
            if (released) { relLevel *= relC; env = relLevel; }
            else if (tf < tA) env = tf / tA;
            else if (tf < tA + tH) env = 1.0f;
            else if (tf < tA + tH + tD)
            {
                const float x = (tf - tA - tH) / tD;
                env = sus + (1.0f - sus) * std::pow (1.0f - x, aC);
            }
            else env = sus;
            lastEnv = env;

            // Tonhoehe
            float semis = 0;
            if (pAmt != 0.0f && tf < pT) semis = pAmt * std::pow (1.0f - tf / pT, pC);
            const float freq = root * (semis != 0.0f ? std::pow (2.0f, semis / 12.0f) : 1.0f);

            // Oszillatoren
            float l = 0, r = 0;
            for (int u = 0; u < uni; ++u)
            {
                const float dt = std::min (freq * detMul[u] / sr, 0.49f);
                const float s = morphOsc (ph[u], dt, wave);
                ph[u] += dt; if (ph[u] >= 1.0f) ph[u] -= 1.0f;
                l += s * panL[u]; r += s * panR[u];
            }
            l *= uniNorm * oscL * env; r *= uniNorm * oscL * env;

            // Sample laeuft durch Filter und Drive wie der Oszillator
            if (smpL > 0.0f && ! smpDone)
            {
                const int a = (int) smpPos, b = a + 1 < smpLen ? a + 1 : (smpLoop ? 0 : a);
                const float fr = (float) (smpPos - (double) a);
                const float sl = smp->l[(size_t) a] + (smp->l[(size_t) b] - smp->l[(size_t) a]) * fr;
                const float sr2 = smp->r.empty() ? sl : smp->r[(size_t) a] + (smp->r[(size_t) b] - smp->r[(size_t) a]) * fr;
                const float g = smpL * (smpFree ? relSmp : env);
                l += sl * g; r += sr2 * g;
                smpPos += smpBase * (smpMode == 2 ? (double) (freq / root) : 1.0);
                if (smpPos >= (double) smpLen)
                {
                    if (smpLoop) smpPos = smpStart + std::fmod (smpPos - (double) smpLen, std::max (1.0, (double) smpLen - smpStart));
                    else smpDone = true;
                }
            }
            if (released) relSmp *= relC;

            float lo, hi, ba;
            if (ftype < 3)
            {
                f[0].tick (l, lo, hi, ba); l = ftype == 0 ? lo : ftype == 1 ? hi : ba;
                f[1].tick (r, lo, hi, ba); r = ftype == 0 ? lo : ftype == 1 ? hi : ba;
            }
            // Click-Layer
            if (clkL > 0.0f && tf < clkTau * 7.0f)
            {
                const float ce = std::exp (-tf / clkTau);
                const float burst = std::sin (2.0f * kPi * clkPh);
                clkPh += std::min (clkT * (1.0f + 2.0f * ce) / sr, 0.45f); if (clkPh >= 1.0f) clkPh -= 1.0f;
                const float nz = clkHp.hp (rand01() * 2.0f - 1.0f);
                const float c = clkL * ce * ((1.0f - clkN) * burst + clkN * nz * 1.6f);
                l += c; r += c;
            }

            // Drive
            if (drv > 0.0001f)
            {
                l = toneL.lp (shape (l * dGain, dT) * dComp);
                r = toneR.lp (shape (r * dGain, dT) * dComp);
            }

            // Sauberer Sub nach dem Drive, damit das Fundament stabil bleibt
            if (subL > 0.0f)
            {
                const float s = std::sin (2.0f * kPi * sub) * subL * env;
                sub += std::min (freq * subDiv / sr, 0.49f); if (sub >= 1.0f) sub -= 1.0f;
                l += s; r += s;
            }

            if (p[M_LOWCUT] > 20.5f) { l = lc[0].hp (lc[0].hp2 (l)); r = lc[1].hp (lc[1].hp2 (r)); }
            if (stolen) { fade *= stealC; if (fade < 0.001f) { active = false; return; } }
            L[i] += l * gain * fade; R[i] += r * gain * fade;
            ++t;

            bool ended = released ? relLevel < 0.0003f : (sus <= 0.0f && tf >= tA + tH + tD);
            if (smpFree && ! smpDone && relSmp > 0.0003f && (smpL > 0.0f)) ended = false;
            if (ended) { tailCount += 1; if (tailCount > (int) (0.05f * sr)) { active = false; tailCount = 0; return; } }
        }
    }

private:
    struct LowCut2  // 12 dB/Okt Lowcut
    {
        OnePole a, b;
        void set (float hz, float sr) { a.set (hz, sr); b.set (hz, sr); }
        float hp2 (float x) { return a.hp (x); }
        float hp (float x) { return b.hp (x); }
        void reset() { a.reset(); b.reset(); }
    };
    float rand01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng & 0xffffff) / 16777216.0f; }

    float ph[MAX_UNISON] {}, sub = 0, clkPh = 0, relLevel = 1, lastEnv = 0, fade = 1, relSmp = 1;
    double smpPos = -1.0;
    bool smpDone = false;
    int t = 0, delay = 0, tailCount = 0;
    unsigned rng = 1;
    OnePole clkHp, toneL, toneR;
    LowCut2 lc[2];
    Svf f[2];
};

class Engine
{
public:
    std::array<std::array<std::atomic<float>, LANE_POINTS>, NUM_LANES>* curves = nullptr;
    std::array<std::atomic<float>, NUM_PARAMS>* modView = nullptr;     // aktueller Wert 0..1 inkl. Automation (fuer die Knobs)
    const std::atomic<const SampleData*>* samples = nullptr;           // ein Eintrag je Modus
    std::atomic<float> lanePos[NUM_LANES];
    std::atomic<float> sourceView[NUM_SOURCES];                        // aktueller Wert jeder Quelle

    Engine()
    {
        for (auto& l : lanePos) l.store (0.0f);
        for (auto& l : sourceView) l.store (0.0f);
    }

    void allocate (float maxSampleRate) { for (auto& s : slots) s.allocate (maxSampleRate); }

    void setSampleRate (float s)
    {
        sr = s;
        for (auto& x : slots) x.setSampleRate (s);
        for (auto& v : voices) v.active = false;
    }
    void reset()
    {
        for (auto& v : voices) v.active = false;
        for (auto& x : slots) x.reset();
                for (auto& p : pos) p = 0;
        for (int i = 0; i < NUM_ENVS; ++i) { envLevel[i] = 0; envTime[i] = 0; envRel[i] = true; }
        for (auto& p : lfoPhase) p = 0;
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
        const int uni = (int) std::round (p[M_UNI]);
        for (int k = 0; k < count; ++k)
        {
            Voice* slot = nullptr;
            for (auto& v : voices) if (! v.active) { slot = &v; break; }
            if (slot == nullptr)
            {
                slot = &voices[0];
                for (auto& v : voices) if (v.age < slot->age) slot = &v;
            }
            slot->start (note + iv[k], note, vel, (int) ((float) k * p[M_STRUM] * 0.001f * sr), ++order, seed += 0x9e3779b9u, uni);
        }
        for (int l = 0; l < NUM_LANES; ++l) pos[l] = 0;
        ++held;
        for (int i = 0; i < NUM_ENVS; ++i) { envTime[i] = 0; envRel[i] = false; envStart[i] = envLevel[i]; }
        for (int i = 0; i < NUM_LFOS; ++i)
            if (values[lfoParam (i, LF_RETRIG)] > 0.5f) { lfoPhase[i] = 0; lfoHeld[i] = 2.0f * rand01() - 1.0f; lfoLast[i] = 0; }
    }
    void noteOff (int note, const float* values)
    {
        if (held > 0 && --held == 0) for (auto& r : envRel) r = true;
        const int mode = (int) values[G_MODE];
        if (values[modeParam (mode, M_GATE)] < 0.5f) return;
        for (auto& v : voices) if (v.active && v.playedNote == note) v.release();
    }
    void allNotesOff()
    {
        for (auto& v : voices) if (v.active) v.steal();
        held = 0; for (auto& r : envRel) r = true;
    }

    /** Schreibt n Samples. values = reale Parameterwerte. bpm/ppq vom Host (ppq < 0 = kein Transport). */
    void process (float* L, float* R, int n, const float* values, double bpm, double ppq)
    {
        std::memset (L, 0, sizeof (float) * (size_t) n);
        std::memset (R, 0, sizeof (float) * (size_t) n);
        std::copy (values, values + NUM_PARAMS, cur);
        const int mode = (int) values[G_MODE];
        if (mode != lastMode) { lastMode = mode; for (auto& v : voices) if (v.active) v.steal(); }

        const int tick = std::min (MAX_CHUNK, std::max (8, (int) (sr * 0.0006f)));
        int done = 0;
        while (done < n)
        {
            const int c = std::min (tick, n - done);
            applyLanes (values, c, bpm, ppq < 0 ? -1.0 : ppq + (double) done / sr * bpm / 60.0);

            const float* mp = cur + modeParam (mode, 0);
            const SampleData* smp = samples != nullptr ? samples[mode].load (std::memory_order_acquire) : nullptr;
            if (smp != nullptr && smp->length() < 2) smp = nullptr;
            for (auto& v : voices) if (v.active) v.render (L + done, R + done, c, mp, sr, smp);
            for (int s = 0; s < NUM_SLOTS; ++s) slots[(size_t) s].process (L + done, R + done, c, cur + slotParam (s, 0));

            const float target = dbToGain (cur[G_MASTER]), step = (target - gainNow) / (float) c;
            const bool lim = values[G_LIMIT] > 0.5f;
            for (int i = 0; i < c; ++i)
            {
                gainNow += step;
                float l = L[done + i] * gainNow, r = R[done + i] * gainNow;
                if (lim) { l = limiter (l); r = limiter (r); }
                L[done + i] = l; R[done + i] = r;
            }
            done += c;
        }
    }
    int activeVoices() const { int c = 0; for (auto& v : voices) c += v.active ? 1 : 0; return c; }

private:
    static float limiter (float x)   // bis -3 dBFS linear, danach weich gegen 0 dBFS
    {
        const float k = 0.7f, a = std::fabs (x);
        if (a <= k) return x;
        const float y = k + (1.0f - k) * softClip ((a - k) / (1.0f - k));
        return x < 0 ? -y : y;
    }
    float rand01() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (float) (seed & 0xffffff) / 16777216.0f; }

    void updateSources (int n, double bpm, double ppq)
    {
        const float dt = (float) n / sr;
        for (int i = 0; i < NUM_LFOS; ++i)
        {
            double ph;
            if (cur[lfoParam (i, LF_SYNC)] > 0.5f)
            {
                const double beats = laneBeats ((int) cur[lfoParam (i, LF_DIV)]);
                if (ppq >= 0 && cur[lfoParam (i, LF_RETRIG)] < 0.5f) ph = std::fmod (ppq / beats, 1.0);
                else ph = std::fmod (lfoPhase[i] + (double) n / (beats * 60.0 / std::max (bpm, 20.0) * sr), 1.0);
            }
            else ph = std::fmod (lfoPhase[i] + (double) cur[lfoParam (i, LF_HZ)] * (double) dt, 1.0);
            if (ph < lfoLast[i]) lfoHeld[i] = 2.0f * rand01() - 1.0f;
            lfoLast[i] = ph; lfoPhase[i] = ph;
            const float x = (float) ph; float y;
            switch ((int) cur[lfoParam (i, LF_SHAPE)])
            {
                case 0:  y = std::sin (2.0f * kPi * x); break;
                case 1:  y = x < 0.25f ? 4 * x : x < 0.75f ? 2 - 4 * x : 4 * x - 4; break;
                case 2:  y = 1.0f - 2.0f * x; break;
                case 3:  y = x < 0.5f ? 1.0f : -1.0f; break;
                default: y = lfoHeld[i]; break;
            }
            source[i] = y;
        }
        for (int i = 0; i < NUM_ENVS; ++i)
        {
            const float a = cur[envParam (i, EV_ATT)] * 0.001f, de = cur[envParam (i, EV_DEC)] * 0.001f, su = cur[envParam (i, EV_SUS)];
            const float re = cur[envParam (i, EV_REL)] * 0.001f, c = 0.6f + cur[envParam (i, EV_CURVE)] * 5.4f;
            if (envRel[i]) envLevel[i] *= std::exp (-6.9f * dt / std::max (re, 0.001f));
            else
            {
                envTime[i] += dt; const float t = envTime[i];
                if (a > 0 && t < a) envLevel[i] = envStart[i] + (1.0f - envStart[i]) * t / a;
                else if (t < a + de) envLevel[i] = su + (1.0f - su) * std::pow (1.0f - (t - a) / de, c);
                else envLevel[i] = su;
            }
            source[NUM_LFOS + i] = envLevel[i];
        }
        for (int i = 0; i < NUM_SOURCES; ++i) sourceView[i].store (source[i], std::memory_order_relaxed);
    }

    void touch (int target) { for (int i = 0; i < numTouched; ++i) if (touched[i] == target) return; touched[numTouched++] = target; }

    void applyLanes (const float* values, int n, double bpm, double ppq)
    {
        const auto& table = params();
        updateSources (n, bpm, ppq);      // liest die Werte des letzten Schritts, also auch modulierte Tempi
        for (int i = 0; i < numTouched; ++i) cur[touched[i]] = values[touched[i]];
        numTouched = 0;

        for (int l = 0; l < NUM_LANES; ++l)
        {
            const double beats = laneBeats ((int) values[laneParam (l, L_RATE)]);
            const int trig = (int) values[laneParam (l, L_TRIG)];
            const double len = beats * 60.0 / std::max (bpm, 20.0) * sr;
            if (trig == 2 && ppq >= 0) pos[l] = std::fmod (ppq / beats, 1.0);
            else
            {
                pos[l] += (double) n / len;
                if (pos[l] >= 1.0) pos[l] = trig == 0 ? 1.0 : std::fmod (pos[l], 1.0);
            }
            lanePos[l].store ((float) pos[l], std::memory_order_relaxed);

            const int target = (int) values[laneParam (l, L_TARGET)];
            if (values[laneParam (l, L_ON)] < 0.5f || target < 0 || target >= NUM_PARAMS || curves == nullptr) continue;
            const auto& d = table[(size_t) target];
            if (! d.modulatable()) continue;

            const float x = (float) pos[l] * (float) (LANE_POINTS - 1);
            const int a = std::min ((int) x, LANE_POINTS - 2);
            const float fr = x - (float) a;
            const auto& c = (*curves)[(size_t) l];
            const float y = c[(size_t) a].load (std::memory_order_relaxed) * (1 - fr) + c[(size_t) a + 1].load (std::memory_order_relaxed) * fr;
            cur[target] = d.fromNorm (d.toNorm (cur[target]) + values[laneParam (l, L_AMOUNT)] * y);
            touch (target);
        }
        for (int m = 0; m < NUM_MODS; ++m)
        {
            const int target = (int) values[modSlot (m, MD_TARGET)];
            if (target < 0 || target >= NUM_PARAMS) continue;
            const auto& d = table[(size_t) target];
            if (! d.modulatable()) continue;
            const float y = source[(int) values[modSlot (m, MD_SOURCE)]];
            cur[target] = d.fromNorm (d.toNorm (cur[target]) + values[modSlot (m, MD_AMOUNT)] * y);
            touch (target);
        }
        if (modView != nullptr)
            for (int i = 0; i < numTouched; ++i)
                (*modView)[(size_t) touched[i]].store (table[(size_t) touched[i]].toNorm (cur[touched[i]]), std::memory_order_relaxed);
    }

    float sr = 44100, gainNow = 1;
    float cur[NUM_PARAMS] {};
    double pos[NUM_LANES] {};
    int lastMode = -1, numTouched = 0, held = 0;
    int touched[NUM_LANES + NUM_MODS] {};
    float source[NUM_SOURCES] {}, envLevel[NUM_ENVS] {}, envTime[NUM_ENVS] {}, envStart[NUM_ENVS] {}, lfoHeld[NUM_LFOS] {};
    bool envRel[NUM_ENVS] { true, true, true };
    double lfoPhase[NUM_LFOS] {}, lfoLast[NUM_LFOS] {};
    uint64_t order = 0;
    unsigned seed = 0x12345;
    std::array<Voice, NUM_VOICES> voices;
    std::array<FxSlot, NUM_SLOTS> slots;
};
} // namespace tk
