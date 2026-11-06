// Tekkforge - Pro-EQ: 8 Baender je Ebene und Master, mit Flanken, Mid/Side und Dynamik.
#pragma once
#include "Effects.h"
#include <cstring>

namespace tk
{
constexpr int NUM_EQS = NUM_LAYERS + 1, EQ_BANDS = 8;
enum EqKind { EQ_BELL = 0, EQ_LOWSHELF, EQ_HIGHSHELF, EQ_LOWCUT, EQ_HIGHCUT, EQ_NOTCH, EQ_BANDPASS, EQ_TILT };
enum EqStereo { ES_STEREO = 0, ES_MID, ES_SIDE, ES_LEFT, ES_RIGHT };

struct Coef { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

inline Coef eqCoef (int kind, float hz, float db, float q, float sr)
{
    const float A = std::pow (10.0f, db / 40.0f), w = 2.0f * kPi * std::clamp (hz, 10.0f, sr * 0.45f) / sr;
    const float c = std::cos (w), s = std::sin (w), al = s / (2.0f * std::max (q, 0.05f)), sq = 2.0f * std::sqrt (A) * al;
    float B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
    switch (kind)
    {
        case EQ_BELL:      B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; break;
        case EQ_LOWSHELF:  B0 = A * ((A + 1) - (A - 1) * c + sq); B1 = 2 * A * ((A - 1) - (A + 1) * c); B2 = A * ((A + 1) - (A - 1) * c - sq);
                           A0 = (A + 1) + (A - 1) * c + sq; A1 = -2 * ((A - 1) + (A + 1) * c); A2 = (A + 1) + (A - 1) * c - sq; break;
        case EQ_HIGHSHELF: B0 = A * ((A + 1) + (A - 1) * c + sq); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - sq);
                           A0 = (A + 1) - (A - 1) * c + sq; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - sq; break;
        case EQ_LOWCUT:    B0 = (1 + c) / 2; B1 = -1 - c; B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
        case EQ_HIGHCUT:   B0 = (1 - c) / 2; B1 = 1 - c; B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
        case EQ_NOTCH:     B0 = 1; B1 = -2 * c; B2 = 1; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
        default:           B0 = al; B1 = 0; B2 = -al; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
    }
    return { B0 / A0, B1 / A0, B2 / A0, A1 / A0, A2 / A0 };
}
inline float coefDb (const Coef& q, float hz, float sr)
{
    const float w = 2.0f * kPi * hz / sr, c1 = std::cos (w), c2 = std::cos (2 * w);
    const float n = q.b0 * q.b0 + q.b1 * q.b1 + q.b2 * q.b2 + 2 * (q.b0 * q.b1 + q.b1 * q.b2) * c1 + 2 * q.b0 * q.b2 * c2;
    const float e = 1 + q.a1 * q.a1 + q.a2 * q.a2 + 2 * (q.a1 + q.a1 * q.a2) * c1 + 2 * q.a2 * c2;
    return 10.0f * std::log10 (std::max (n, 1.0e-12f) / std::max (e, 1.0e-12f));
}

/** Filterstufen eines Bandes: Cuts staffeln fuer 12, 24 oder 48 dB je Oktave, Tilt besteht aus zwei Shelves. */
inline int bandStages (const float* b, float gainDb, float sr, Coef* out)
{
    const int kind = (int) b[E_TYPE]; const float hz = b[E_FREQ], q = b[E_Q];
    if (kind == EQ_TILT)
    {
        out[0] = eqCoef (EQ_LOWSHELF, hz, -gainDb * 0.5f, 0.6f, sr); out[1] = eqCoef (EQ_HIGHSHELF, hz, gainDb * 0.5f, 0.6f, sr);
        return 2;
    }
    if (kind == EQ_LOWCUT || kind == EQ_HIGHCUT)
    {
        const int slope = (int) b[E_SLOPE], n = slope == 0 ? 1 : slope == 1 ? 2 : 4;
        static const float q2[2] = { 0.541f, 1.307f }, q4[4] = { 0.51f, 0.601f, 0.9f, 2.563f };
        for (int i = 0; i < n; ++i)
        {
            const float base = n == 1 ? 1.0f : n == 2 ? q2[i] : q4[i];
            out[i] = eqCoef (kind, hz, 0, base * std::clamp (q, 0.5f, 2.0f), sr);
        }
        return n;
    }
    out[0] = eqCoef (kind, hz, gainDb, q, sr);
    return 1;
}
inline float bandDb (const float* b, float hz, float sr, float dynDb = 0)
{
    Coef c[4]; const int n = bandStages (b, b[E_GAIN] + dynDb, sr, c);
    float db = 0; for (int i = 0; i < n; ++i) db += coefDb (c[i], hz, sr);
    return db;
}

class ProEq
{
public:
    void setSampleRate (float s) { sr = s; reset(); }
    void reset() { std::memset (st, 0, sizeof st); std::memset (detZ, 0, sizeof detZ); for (auto& e : env) e = 0; }

    /** v zeigt auf die Parameter von Band 0. Laeuft nur, wenn mindestens ein Band an ist. */
    void process (float* L, float* R, int n, const float* v)
    {
        bool any = false;
        for (int b = 0; b < EQ_BANDS; ++b) any = any || v[b * E_COUNT + E_ON] > 0.5f;
        if (! any) return;
        const float atk = std::exp (-1.0f / (0.003f * sr)), rel = std::exp (-1.0f / (0.08f * sr));
        for (int b = 0; b < EQ_BANDS; ++b)
        {
            const float* q = v + b * E_COUNT;
            if (q[E_ON] < 0.5f) continue;
            const int stereo = (int) q[E_STEREO];
            const bool dyn = std::fabs (q[E_DYN]) > 0.01f;
            float dynDb = 0;
            if (dyn)
            {
                // Pegel im Band messen: Bei Ueberschreiten der Schwelle faehrt das Band um bis zu Dyn dB
                const Coef bp = eqCoef (EQ_BANDPASS, q[E_FREQ], 0, q[E_Q], sr);
                for (int i = 0; i < n; ++i)
                {
                    const float x = 0.5f * (L[i] + R[i]);
                    const float y = bp.b0 * x + detZ[b][0];
                    detZ[b][0] = bp.b1 * x - bp.a1 * y + detZ[b][1]; detZ[b][1] = bp.b2 * x - bp.a2 * y;
                    const float a = std::fabs (y);
                    env[b] = a > env[b] ? atk * env[b] + (1 - atk) * a : rel * env[b] + (1 - rel) * a;
                }
                const float level = 20.0f * std::log10 (env[b] + 1.0e-6f);
                dynDb = q[E_DYN] * std::clamp ((level - q[E_THR]) / 12.0f, 0.0f, 1.0f);
            }
            Coef c[4]; const int stages = bandStages (q, q[E_GAIN] + dynDb, sr, c);
            for (int i = 0; i < n; ++i)
            {
                float a = L[i], d = R[i];
                if (stereo == ES_MID || stereo == ES_SIDE) { const float m = 0.5f * (a + d), s = 0.5f * (a - d); a = stereo == ES_MID ? m : s; d = stereo == ES_MID ? s : m; }
                else if (stereo == ES_RIGHT) std::swap (a, d);
                const bool both = stereo == ES_STEREO;
                for (int k = 0; k < stages; ++k)
                {
                    a = run (c[k], a, st[b][k][0]);
                    if (both) d = run (c[k], d, st[b][k][1]);
                }
                if (stereo == ES_MID) { L[i] = a + d; R[i] = a - d; }
                else if (stereo == ES_SIDE) { L[i] = d + a; R[i] = d - a; }
                else if (stereo == ES_RIGHT) R[i] = a;
                else { L[i] = a; if (both) R[i] = d; }
                if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) { L[i] = R[i] = 0; reset(); }
            }
        }
    }
private:
    static float run (const Coef& c, float x, float* z)
    {
        const float y = c.b0 * x + z[0];
        z[0] = c.b1 * x - c.a1 * y + z[1]; z[1] = c.b2 * x - c.a2 * y;
        return y;
    }
    float sr = 44100, st[EQ_BANDS][4][2][2] {}, detZ[EQ_BANDS][2] {}, env[EQ_BANDS] {};
};
} // namespace tk
