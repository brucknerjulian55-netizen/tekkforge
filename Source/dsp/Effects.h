// Tekkforge - FX-Slot. Jeder Slot besitzt eigene Zustaende, deshalb kann jeder Effekt mehrfach laufen.
#pragma once
#include "Params.h"
#include <cstring>
#include <vector>

namespace tk
{
constexpr float kPi = 3.14159265358979f;
constexpr int MAX_CHUNK = 128;
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float softClip (float x)  // schnelle tanh-Naeherung
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
inline float shape (float x, int type, float asym = 0.0f)
{
    switch (type)
    {
        case 0: return softClip (x);
        case 1: return std::clamp (x, -1.0f, 1.0f);
        case 2: return std::sin (std::clamp (x, -32.0f, 32.0f) * 1.5707963f);
        default:
        {
            // Tekk: asymmetrisch, oben weich und unten hart, ergibt das knurrende Obertonbild
            const float a = 0.25f + asym * 0.5f;
            const float y = softClip (x + a) - softClip (a);
            return std::clamp (y * 1.35f, -1.0f, 0.92f);
        }
    }
}

struct OnePole
{
    float z = 0, a = 1;
    void set (float hz, float sr) { a = 1.0f - std::exp (-2.0f * kPi * std::min (hz, sr * 0.45f) / sr); }
    float lp (float x) { z += a * (x - z); return z; }
    float hp (float x) { return x - lp (x); }
    void reset() { z = 0; }
};

struct Svf  // TPT-Filter, stabil auch bei schneller Modulation
{
    float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void set (float hz, float res, float sr)
    {
        g = std::tan (kPi * std::clamp (hz, 10.0f, sr * 0.45f) / sr);
        k = 2.0f - 1.96f * std::clamp (res, 0.0f, 1.0f);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void tick (float x, float& lo, float& hi, float& band)
    {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        lo = v2; band = v1; hi = x - k * v1 - v2;
    }
    float process (float x, int type)
    {
        float l, h, b; tick (x, l, h, b);
        switch (type) { case 0: return l; case 1: return h; case 2: return b; default: return l + h; }
    }
    void reset() { ic1 = ic2 = 0; }
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    enum { LOWSHELF, PEAK, HIGHSHELF };
    void set (int kind, float hz, float db, float q, float sr)
    {
        const float A = std::pow (10.0f, db / 40.0f), w = 2.0f * kPi * std::min (hz, sr * 0.45f) / sr;
        const float c = std::cos (w), s = std::sin (w), al = s / (2.0f * q), sq = 2.0f * std::sqrt (A) * al;
        float B0, B1, B2, A0, A1, A2;
        if (kind == PEAK) { B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; }
        else if (kind == LOWSHELF)
        { B0 = A * ((A + 1) - (A - 1) * c + sq); B1 = 2 * A * ((A - 1) - (A + 1) * c); B2 = A * ((A + 1) - (A - 1) * c - sq);
          A0 = (A + 1) + (A - 1) * c + sq; A1 = -2 * ((A - 1) + (A + 1) * c); A2 = (A + 1) + (A - 1) * c - sq; }
        else
        { B0 = A * ((A + 1) + (A - 1) * c + sq); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - sq);
          A0 = (A + 1) - (A - 1) * c + sq; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - sq; }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
};

struct Phaser
{
    float z[2][12] {}, fb[2] {}, phase = 0;
    void reset() { std::memset (z, 0, sizeof z); fb[0] = fb[1] = 0; }
    void process (float* L, float* R, int n, float rate, float depth, float center, float feedback, int stages, float width, float sr)
    {
        const float inc = rate / sr;
        for (int i = 0; i < n; ++i)
        {
            phase += inc; if (phase >= 1) phase -= 1;
            for (int c = 0; c < 2; ++c)
            {
                const float lfo = std::sin (2 * kPi * (phase + (c ? width * 0.25f : 0.0f)));
                const float hz = center * std::pow (2.0f, lfo * depth * 2.0f);
                const float t = std::tan (kPi * std::clamp (hz, 20.0f, sr * 0.45f) / sr), a = (t - 1) / (t + 1);
                float x = (c ? R[i] : L[i]) + fb[c] * feedback;
                for (int s = 0; s < stages; ++s) { const float y = a * x + z[c][s]; z[c][s] = x - a * y; x = y; }
                fb[c] = softClip (x);
                (c ? R[i] : L[i]) = x;
            }
        }
    }
};

struct Comb
{
    std::vector<float> b; int p = 0, len = 1; float store = 0;
    void init (int maxLen) { b.assign ((size_t) maxLen + 1, 0.0f); }
    void clear() { std::fill (b.begin(), b.end(), 0.0f); store = 0; p = 0; }
    float run (float x, float fbk, float damp)
    {
        const float y = b[(size_t) p];
        store = y * (1 - damp) + store * damp;
        b[(size_t) p] = x + store * fbk;
        if (++p >= len) p = 0;
        return y;
    }
};
struct AllPass
{
    std::vector<float> b; int p = 0, len = 1;
    void init (int maxLen) { b.assign ((size_t) maxLen + 1, 0.0f); }
    void clear() { std::fill (b.begin(), b.end(), 0.0f); p = 0; }
    float run (float x)
    {
        const float d = b[(size_t) p], y = d - x;
        b[(size_t) p] = x + d * 0.5f;
        if (++p >= len) p = 0;
        return y;
    }
};

class FxSlot
{
public:
    void allocate (float maxSr)
    {
        dlLen = (int) (maxSr * 1.6f) + 8;
        dl[0].assign ((size_t) dlLen, 0.0f); dl[1].assign ((size_t) dlLen, 0.0f);
        const float scale = maxSr / 44100.0f;
        for (int c = 0; c < 2; ++c)
        {
            for (auto& x : comb[c]) x.init ((int) (1700.0f * scale) + 32);
            for (auto& x : ap[c])   x.init ((int) (620.0f * scale) + 32);
        }
    }
    void setSampleRate (float s)
    {
        sr = s;
        static const int cl[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int al[4] = { 556, 441, 341, 225 };
        const float scale = sr / 44100.0f;
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < 8; ++i) comb[c][i].len = std::max (1, (int) ((cl[i] + c * 23) * scale));
            for (int i = 0; i < 4; ++i) ap[c][i].len = std::max (1, (int) ((al[i] + c * 23) * scale));
        }
        reset();
    }
    void reset()
    {
        for (auto& d : dl) std::fill (d.begin(), d.end(), 0.0f);
        for (int c = 0; c < 2; ++c)
        {
            for (auto& x : comb[c]) x.clear();
            for (auto& x : ap[c]) x.clear();
            svf[c].reset(); hp[c].reset(); post[c].reset();
            for (auto& b : eq[c]) b.reset();
            hold[c] = 0;
        }
        phaser.reset(); wr = 0; lfo = 0; holdCount = 0; envDb = 0; lastEqKey = 1e30f;
    }
    void setLowcut (float hz) { for (auto& x : hp) x.set (hz, sr); }
    void setTone (float hz)   { for (auto& x : post) x.set (hz, sr); }

    /** v = Slotparameter ab S_ON. Wechselt der Typ, wird der Zustand geleert. */
    void process (float* L, float* R, int n, const float* v)
    {
        const int type = (int) v[S_TYPE];
        if (type != lastType) { reset(); lastType = type; }
        if (v[S_ON] < 0.5f || type == FX_OFF) return;

        const FxP* info = fxInfo (type);
        float p[FX_PARAMS];
        for (int i = 0; i < FX_PARAMS; ++i) p[i] = fxReal (info[i], v[S_P1 + i]);

        float dryL[MAX_CHUNK], dryR[MAX_CHUNK];
        std::memcpy (dryL, L, sizeof (float) * (size_t) n);
        std::memcpy (dryR, R, sizeof (float) * (size_t) n);

        switch (type)
        {
            case FX_DIST:       distortion (L, R, n, p); break;
            case FX_FILTER:     filter (L, R, n, p); break;
            case FX_CHORUS:     modDelay (L, R, n, p[0], p[1] * 6.0f, p[2], p[3], p[4], p[5]); break;
            case FX_FLANGER:    modDelay (L, R, n, p[0], p[1] * p[2], p[2], p[3], p[4], p[5]); break;
            case FX_PHASER:     phaser.process (L, R, n, p[0], p[1], p[2], p[3], ((int) std::round (p[4]) / 2) * 2, p[5], sr); break;
            case FX_DESTRUCTOR: destructor (L, R, n, p); break;
            case FX_CRUSH:      crush (L, R, n, p); break;
            case FX_DELAY:      delay (L, R, n, p); break;
            case FX_REVERB:     reverb (L, R, n, p); break;
            case FX_EQ:         equalizer (L, R, n, p); break;
            case FX_COMP:       compressor (L, R, n, p); break;
            case FX_CLIP:       clipper (L, R, n, p); break;
            default: break;
        }
        const float mix = v[S_MIX];
        for (int i = 0; i < n; ++i)
        {
            L[i] = dryL[i] + (L[i] - dryL[i]) * mix;
            R[i] = dryR[i] + (R[i] - dryR[i]) * mix;
            if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) { L[i] = R[i] = 0; reset(); }
        }
    }

private:
    void distortion (float* L, float* R, int n, const float* p)
    {
        const float g = dbToGain (p[0]), out = dbToGain (p[5]); const int t = (int) p[1];
        setLowcut (p[4]); setTone (p[2]);
        for (int i = 0; i < n; ++i)
        {
            L[i] = post[0].lp (hp[0].hp (shape (L[i] * g + p[3] * 0.3f, t, p[3]) - shape (p[3] * 0.3f, t, p[3]))) * out;
            R[i] = post[1].lp (hp[1].hp (shape (R[i] * g + p[3] * 0.3f, t, p[3]) - shape (p[3] * 0.3f, t, p[3]))) * out;
        }
    }
    void filter (float* L, float* R, int n, const float* p)
    {
        const float g = dbToGain (p[3]); const int t = (int) p[0];
        lfo += p[4] * (float) n / sr; if (lfo >= 1) lfo -= 1;
        const float hz = p[1] * std::pow (2.0f, std::sin (2 * kPi * lfo) * p[5] * 3.0f);
        svf[0].set (hz, p[2], sr); svf[1].set (hz, p[2], sr);
        for (int i = 0; i < n; ++i)
        {
            L[i] = svf[0].process (p[3] > 0 ? softClip (L[i] * g) : L[i], t);
            R[i] = svf[1].process (p[3] > 0 ? softClip (R[i] * g) : R[i], t);
        }
    }
    float readDl (int c, float samples) const
    {
        float pos = (float) wr - samples;
        while (pos < 0) pos += (float) dlLen;
        const int a = (int) pos, b = (a + 1) % dlLen; const float f = pos - (float) a;
        return dl[c][(size_t) a] + (dl[c][(size_t) b] - dl[c][(size_t) a]) * f;
    }
    void modDelay (float* L, float* R, int n, float rate, float depthMs, float baseMs, float fb, float width, float lowcut)
    {
        setLowcut (lowcut);
        const float inc = rate / sr, ms = sr * 0.001f;
        for (int i = 0; i < n; ++i)
        {
            lfo += inc; if (lfo >= 1) lfo -= 1;
            const float a = std::sin (2 * kPi * lfo), b = std::sin (2 * kPi * (lfo + width * 0.5f));
            const float tl = std::max (1.0f, (baseMs + depthMs * 0.5f * (1 + a)) * ms);
            const float tr = std::max (1.0f, (baseMs + depthMs * 0.5f * (1 + b)) * ms);
            const float yl = readDl (0, tl), yr = readDl (1, tr);
            dl[0][(size_t) wr] = hp[0].hp (L[i]) + softClip (yl * fb);
            dl[1][(size_t) wr] = hp[1].hp (R[i]) + softClip (yr * fb);
            if (++wr >= dlLen) wr = 0;
            L[i] += yl; R[i] += yr;
            L[i] *= 0.71f; R[i] *= 0.71f;
        }
    }
    void destructor (float* L, float* R, int n, const float* p)
    {
        // Kette: Drive -> Wavefold -> Sample-Crush -> eingebauter Phaser
        const float g = dbToGain (p[0]), fold = p[1];
        const int every = 1 + (int) (p[2] * p[2] * 24.0f * sr / 44100.0f);
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                float x = (c ? R[i] : L[i]) * g;
                float y = softClip (x);
                if (fold > 0) y = y + fold * (std::sin (x * 2.2f) - y);
                (c ? R[i] : L[i]) = y;
            }
            if (holdCount++ % every == 0) { hold[0] = L[i]; hold[1] = R[i]; }
            L[i] = hold[0]; R[i] = hold[1];
        }
        if (p[3] > 0)
        {
            float wl[MAX_CHUNK], wr2[MAX_CHUNK];
            std::memcpy (wl, L, sizeof (float) * (size_t) n); std::memcpy (wr2, R, sizeof (float) * (size_t) n);
            phaser.process (wl, wr2, n, p[4], 0.85f, 900.0f, p[5], 8, 0.5f, sr);
            for (int i = 0; i < n; ++i) { L[i] += (wl[i] - L[i]) * p[3]; R[i] += (wr2[i] - R[i]) * p[3]; }
        }
    }
    void crush (float* L, float* R, int n, const float* p)
    {
        const float steps = std::pow (2.0f, p[0] - 1.0f), out = dbToGain (p[5]);
        const int every = std::max (1, (int) std::round (p[1] * sr / 44100.0f));
        setLowcut (p[4]); setTone (p[2]);
        for (int i = 0; i < n; ++i)
        {
            if (holdCount++ % every == 0)
            {
                const float d = p[3] * (noise() - 0.5f) / steps;
                hold[0] = std::round ((L[i] + d) * steps) / steps;
                hold[1] = std::round ((R[i] + d) * steps) / steps;
            }
            L[i] = post[0].lp (hp[0].hp (hold[0])) * out;
            R[i] = post[1].lp (hp[1].hp (hold[1])) * out;
        }
    }
    void delay (float* L, float* R, int n, const float* p)
    {
        setLowcut (p[3]); setTone (p[2]);
        const float t = std::min (p[0] * 0.001f * sr, (float) dlLen - 4.0f), pp = p[4], width = p[5];
        for (int i = 0; i < n; ++i)
        {
            const float yl = readDl (0, t), yr = readDl (1, t);
            const float inL = post[0].lp (hp[0].hp (L[i])), inR = post[1].lp (hp[1].hp (R[i]));
            const float m = (inL + inR) * 0.5f;
            dl[0][(size_t) wr] = (inL * (1 - pp) + m * pp) + softClip (((1 - pp) * yl + pp * yr) * p[1]);
            dl[1][(size_t) wr] = (inR * (1 - pp)) + softClip (((1 - pp) * yr + pp * yl) * p[1]);
            if (++wr >= dlLen) wr = 0;
            const float mid = (yl + yr) * 0.5f, side = (yl - yr) * 0.5f * width;
            L[i] = mid + side; R[i] = mid - side;
        }
    }
    void reverb (float* L, float* R, int n, const float* p)
    {
        const float fbk2 = 0.7f + p[0] * 0.28f, damp = p[1] * 0.6f, out = dbToGain (p[5]), w = p[2];
        setLowcut (p[3]); setTone (p[4]);
        for (int i = 0; i < n; ++i)
        {
            const float in = (hp[0].hp (L[i]) + hp[1].hp (R[i])) * 0.5f * 0.03f * 2.0f;
            float a = 0, b = 0;
            for (auto& c : comb[0]) a += c.run (in, fbk2, damp);
            for (auto& c : comb[1]) b += c.run (in, fbk2, damp);
            for (auto& x : ap[0]) a = x.run (a);
            for (auto& x : ap[1]) b = x.run (b);
            a = post[0].lp (a); b = post[1].lp (b);
            const float mid = (a + b) * 0.5f, side = (a - b) * 0.5f * w;
            L[i] = (mid + side) * out; R[i] = (mid - side) * out;
        }
    }
    void equalizer (float* L, float* R, int n, const float* p)
    {
        const float key = p[0] + p[1] * 3.1f + p[2] * 7.3f + p[3] * 0.37f + p[4] * 11.9f + p[5] * 0.13f;
        if (key != lastEqKey)
        {
            lastEqKey = key;
            for (int c = 0; c < 2; ++c)
            {
                eq[c][0].set (Biquad::LOWSHELF, p[1], p[0], 0.7f, sr);
                eq[c][1].set (Biquad::PEAK, p[3], p[2], 1.0f, sr);
                eq[c][2].set (Biquad::HIGHSHELF, p[5], p[4], 0.7f, sr);
            }
        }
        for (int i = 0; i < n; ++i)
            for (int b = 0; b < 3; ++b) { L[i] = eq[0][b].process (L[i]); R[i] = eq[1][b].process (R[i]); }
    }
    void compressor (float* L, float* R, int n, const float* p)
    {
        const float at = std::exp (-1.0f / (p[2] * 0.001f * sr)), rl = std::exp (-1.0f / (p[3] * 0.001f * sr));
        const float thr = p[0], ratio = p[1], knee = p[4], mk = dbToGain (p[5]);
        for (int i = 0; i < n; ++i)
        {
            const float lv = 20.0f * std::log10 (std::max (std::fabs (L[i]), std::fabs (R[i])) + 1e-6f);
            float over = lv - thr, red = 0;
            if (knee > 0 && std::fabs (over) < knee * 0.5f) { const float o = over + knee * 0.5f; red = (1 / ratio - 1) * o * o / (2 * knee); }
            else if (over > 0) red = (1 / ratio - 1) * over;
            envDb = red < envDb ? at * envDb + (1 - at) * red : rl * envDb + (1 - rl) * red;
            const float g = dbToGain (envDb) * mk;
            L[i] *= g; R[i] *= g;
        }
    }
    void clipper (float* L, float* R, int n, const float* p)
    {
        const float g = dbToGain (p[0]), ceil = dbToGain (p[1]), s = p[2], out = dbToGain (p[5]);
        setLowcut (p[3]); setTone (p[4]);
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float x = (c ? R[i] : L[i]) * g / ceil;
                const float y = std::clamp (x, -1.0f, 1.0f) * (1 - s) + softClip (x) * s;
                (c ? R[i] : L[i]) = post[c].lp (hp[c].hp (y * ceil)) * out;
            }
        }
    }
    float noise() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng & 0xffffff) / 16777216.0f; }

    float sr = 44100;
    int lastType = 0, dlLen = 1, wr = 0;
    unsigned holdCount = 0, rng = 0x9e3779b9u;
    float lfo = 0, hold[2] {}, envDb = 0, lastEqKey = 1e30f;
    std::vector<float> dl[2];
    Comb comb[2][8]; AllPass ap[2][4];
    Svf svf[2]; OnePole hp[2], post[2];
    Biquad eq[2][3];
    Phaser phaser;
};
} // namespace tk
