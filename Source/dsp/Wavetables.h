// Tekkforge - Wavetable-Bank. Alle Tabellen werden per Formel erzeugt und bandbegrenzt, damit hohe Toene nicht kratzen.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tk
{
inline void wtFft (std::vector<std::complex<float>>& a, bool inverse)
{
    constexpr float pi = 3.14159265358979f;
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<float> step = std::polar (1.0f, (inverse ? 2.0f : -2.0f) * pi / (float) len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k, w *= step)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
            }
        }
    }
}

struct WaveInfo { const char* name; const char* group; };
enum { WT_NOISE_INDEX = 35 };
constexpr int NUM_TABLES = 36, WT_SIZE = 1024, WT_FRAMES = 16, WT_MIPS = 8;

inline const WaveInfo* waveInfos()
{
    static const WaveInfo w[NUM_TABLES] = {
        { "Sinus", "Grundformen" }, { "Dreieck", "Grundformen" }, { "Säge", "Grundformen" }, { "Rechteck", "Grundformen" }, { "Puls", "Grundformen" },
        { "Sinus zu Säge", "Grundformen" }, { "Sinus zu Rechteck", "Grundformen" },
        { "Sub Plus", "Bass" }, { "Rumble", "Bass" }, { "Reese", "Bass" }, { "Growl", "Bass" }, { "Hoover", "Bass" }, { "Tekk Knurren", "Bass" }, { "Acid", "Bass" }, { "Wobble", "Bass" }, { "Sägezahn-Sub", "Bass" },
        { "FM 2 zu 1", "FM" }, { "FM 3 zu 1", "FM" }, { "FM 5 zu 2", "FM" }, { "FM Metall", "FM" },
        { "Orgel", "Harmonisch" }, { "Ungerade", "Harmonisch" }, { "Gerade", "Harmonisch" }, { "Glocke", "Harmonisch" }, { "Obertöne", "Harmonisch" },
        { "Bit", "Digital" }, { "Sync", "Digital" }, { "PWM Doppel", "Digital" }, { "Chip", "Digital" }, { "Treppe", "Digital" },
        { "Vokal A bis E", "Vokal" }, { "Vokal O bis U", "Vokal" }, { "Talking", "Vokal" }, { "Choir", "Vokal" }, { "Formant Bass", "Vokal" },
        { "Rauschen", "Rauschen" } };
    return w;
}
inline std::vector<std::string> waveNames()
{
    std::vector<std::string> v; for (int i = 0; i < NUM_TABLES; ++i) v.push_back (waveInfos()[i].name); return v;
}

/** Eine Welle als Formel. x = Phase 0..1, m = Position in der Tabelle 0..1 */
inline float waveFormula (int t, float x, float m)
{
    constexpr float pi = 3.14159265358979f, tp = 2.0f * pi;
    const auto s = [&] (float h) { return std::sin (tp * h * x); };
    const auto saw = [&] (float p) { p -= std::floor (p); return 2.0f * p - 1.0f; };
    const auto tri = [&] (float p) { p -= std::floor (p); return p < 0.5f ? 4 * p - 1 : 3 - 4 * p; };
    const auto sq = [&] (float p) { p -= std::floor (p); return p < 0.5f ? 1.0f : -1.0f; };
    const auto vowel = [&] (float f1, float f2, float f3)
    {
        float y = 0;
        for (int h = 1; h <= 60; ++h)
        {
            const float fh = (float) h * 8.0f;     // Grundton rund 8 Einheiten, Formanten als Huellkurve
            const auto peak = [&] (float c, float w) { const float d = (fh - c) / w; return std::exp (-d * d); };
            y += (peak (f1, 40) + 0.7f * peak (f2, 70) + 0.4f * peak (f3, 90)) * std::sin (tp * (float) h * x) / std::sqrt ((float) h);
        }
        return y;
    };
    switch (t)
    {
        case 0: return s (1);
        case 1: return tri (x);
        case 2: return saw (x);
        case 3: return sq (x);
        case 4: return std::fmod (x, 1.0f) < 0.5f - 0.47f * m ? 1.0f : -1.0f;
        case 5: return s (1) * (1 - m) + saw (x) * m;
        case 6: return s (1) * (1 - m) + sq (x) * m;
        case 7: return s (1) + (0.15f + 0.5f * m) * s (2) + 0.25f * m * s (3);
        case 8: return std::tanh ((1.0f + 7.0f * m) * (s (1) + 0.3f * s (3) + 0.15f * s (7)));
        case 9: return 0.5f * (saw (x) + saw (x * 1.0f + m * 0.5f)) + 0.2f * s (1);
        case 10: return std::tanh ((1 + 6 * m) * (s (1) + 0.6f * std::sin (tp * x * 2 + (3 + 8 * m) * s (1))));
        case 11: { const float p = x + 0.18f * m * std::sin (tp * x); return 0.6f * saw (p) + 0.4f * saw (p * 2 + 0.2f * m); }
        case 12: { const float a = std::tanh ((2 + 10 * m) * (s (1) + 0.35f)); return a + 0.4f * m * sq (x * 5.0f) * std::fabs (s (1)); }
        case 13: { float y = 0; for (int h = 1; h <= 40; ++h) { const float c = 1 + 22 * m, r = std::exp (-std::pow (((float) h - c) / 2.5f, 2.0f)); y += (1.0f / (float) h + 0.7f * r) * std::sin (tp * (float) h * x); } return y; }
        case 14: return s (1) * (1 - m) + m * (s (1) * std::cos (tp * 3 * x) + 0.5f * s (5));
        case 15: return 0.7f * s (1) + 0.45f * saw (x * 2.0f) * (0.2f + m);
        case 16: return std::sin (tp * x + (0.3f + 5 * m) * s (2));
        case 17: return std::sin (tp * x + (0.3f + 5 * m) * s (3));
        case 18: return std::sin (tp * 2 * x + (0.3f + 6 * m) * std::sin (tp * 5 * x));
        case 19: return std::sin (tp * x + (1 + 7 * m) * std::sin (tp * 7 * x + 2 * m * s (11)));
        case 20: return s (1) + 0.6f * s (2) * (1 - m * 0.5f) + 0.5f * s (3) + 0.35f * m * s (4) + 0.3f * s (6) + 0.25f * m * s (8);
        case 21: { float y = 0; const int n = 1 + (int) (m * 20); for (int h = 0; h < n; ++h) y += s (2.0f * (float) h + 1) / (2.0f * (float) h + 1); return y; }
        case 22: { float y = s (1); const int n = (int) (m * 12); for (int h = 1; h <= n; ++h) y += s (2.0f * (float) h) / (2.0f * (float) h); return y; }
        case 23: return s (1) + 0.5f * s (2.0f) * m + 0.6f * s (5) + 0.4f * s (9) * m + 0.3f * s (13);
        case 24: { float y = 0; const int n = 1 + (int) (m * 40); for (int h = 1; h <= n; ++h) y += s ((float) h) / std::sqrt ((float) h); return y; }
        case 25: { const float st = 2.0f + std::floor (14.0f * (1 - m)); return std::round (saw (x) * st) / st; }
        case 26: { const float r = 1 + 7 * m, p = x * r; return saw (p) * (1 - x); }
        case 27: return 0.5f * (sq (x) + sq (x + 0.1f + 0.35f * m));
        case 28: { const float st = 4 + std::floor (12 * m); return std::round (tri (x) * st) / st; }
        case 29: { const float st = 3 + std::floor (10 * m); return std::floor (x * st) / st * 2 - 1; }
        case 30: return vowel (70 + 40 * m, 110 + 240 * m, 250);
        case 31: return vowel (50 + 5 * m, 90 - 15 * m, 240);
        case 32: return vowel (40 + 60 * std::fabs (std::sin (pi * m * 2)), 90 + 200 * m, 260);
        case 33: return vowel (60, 95, 260) + 0.4f * vowel (55 + 15 * m, 130 + 30 * m, 280);
        case 34: return std::tanh (1.5f * (vowel (35 + 40 * m, 80 + 100 * m, 210) * 0.6f + 0.8f * s (1)));
        default: return 0;
    }
}

class WavetableBank
{
public:
    static WavetableBank& get() { static WavetableBank b; return b; }
    /** Startet den Aufbau im Hintergrund. Bis dahin spielen Oszillatoren einen Sinus. */
    void prepare() { std::lock_guard<std::mutex> l (lock); if (! started) { started = true; stop.store (false); worker = std::thread ([this] { build(); }); } }
    void wait() { prepare(); std::lock_guard<std::mutex> l (lock); if (worker.joinable()) worker.join(); }
    /** Jede Plugin-Instanz meldet sich an und ab. Der letzte beendet den Aufbau, bevor die DLL entladen wird. */
    void addUser() { std::lock_guard<std::mutex> l (lock); ++users; }
    void removeUser()
    {
        std::lock_guard<std::mutex> l (lock);
        if (--users > 0) return;
        stop.store (true);
        if (worker.joinable()) worker.join();
        started = false;
    }
    bool ready (int table) const { return done[(size_t) table].load (std::memory_order_acquire); }
    ~WavetableBank() { if (worker.joinable()) worker.detach(); }

    /** Liest die Tabelle an Phase ph (0..1), Position pos (0..1). dt = Phasenschritt je Sample. */
    float read (int table, float pos, float ph, float dt) const
    {
        const auto& t = data[(size_t) table];
        int mip = 0;
        while (mip < WT_MIPS - 1 && (float) (WT_SIZE / 2 >> mip) * dt > 0.48f) ++mip;
        const float f = std::clamp (pos, 0.0f, 1.0f) * (float) (WT_FRAMES - 1);
        const int a = std::min ((int) f, WT_FRAMES - 2);
        const float mixF = f - (float) a, x = ph * (float) WT_SIZE;
        const int i = (int) x & (WT_SIZE - 1), j = (i + 1) & (WT_SIZE - 1);
        const float fr = x - std::floor (x);
        const float* p0 = t.data() + index (a, mip), *p1 = t.data() + index (a + 1, mip);
        const float s0 = p0[i] + (p0[j] - p0[i]) * fr, s1 = p1[i] + (p1[j] - p1[i]) * fr;
        return s0 + (s1 - s0) * mixF;
    }
    /** Eine Periode zum Zeichnen */
    float shape (int table, float pos, float ph) const { return read (table, pos, ph, 0.0f); }

private:
    static size_t index (int frame, int mip) { return (size_t) (frame * WT_MIPS + mip) * (size_t) WT_SIZE; }
    void build()
    {
        std::vector<std::complex<float>> spec ((size_t) WT_SIZE), tmp ((size_t) WT_SIZE);
        for (int t = 0; t < NUM_TABLES && ! stop.load(); ++t)
        {
            if (done[(size_t) t].load()) continue;
            auto& d = data[(size_t) t];
            d.assign ((size_t) (WT_FRAMES * WT_MIPS * WT_SIZE), 0.0f);
            if (t != WT_NOISE_INDEX)
                for (int fr = 0; fr < WT_FRAMES; ++fr)
                {
                    const float m = (float) fr / (float) (WT_FRAMES - 1);
                    float top = 1.0e-6f;
                    std::vector<float> raw ((size_t) WT_SIZE);
                    for (int i = 0; i < WT_SIZE; ++i) raw[(size_t) i] = waveFormula (t, (float) i / (float) WT_SIZE, m);
                    float mean = 0; for (auto v : raw) mean += v; mean /= (float) WT_SIZE;
                    for (int i = 0; i < WT_SIZE; ++i) { spec[(size_t) i] = raw[(size_t) i] - mean; top = std::max (top, std::fabs (raw[(size_t) i] - mean)); }
                    wtFft (spec, false);
                    for (int mip = 0; mip < WT_MIPS; ++mip)
                    {
                        const int keep = (WT_SIZE / 2) >> mip;
                        std::fill (tmp.begin(), tmp.end(), std::complex<float> (0, 0));
                        for (int k = 1; k < keep; ++k) { tmp[(size_t) k] = spec[(size_t) k]; tmp[(size_t) (WT_SIZE - k)] = spec[(size_t) (WT_SIZE - k)]; }
                        wtFft (tmp, true);
                        float* out = d.data() + index (fr, mip);
                        for (int i = 0; i < WT_SIZE; ++i) out[i] = tmp[(size_t) i].real() / (float) WT_SIZE / top;
                    }
                }
            done[(size_t) t].store (true, std::memory_order_release);
        }
    }
    std::array<std::vector<float>, NUM_TABLES> data;
    std::array<std::atomic<bool>, NUM_TABLES> done {};
    std::atomic<bool> stop { false };
    bool started = false; int users = 0; std::mutex lock;
    std::thread worker;
};

enum WarpMode { WARP_NONE = 0, WARP_SYNC, WARP_BENDP, WARP_BENDM, WARP_PWM, WARP_MIRROR, WARP_FOLD, WARP_QUANT, WARP_ASYM, WARP_FM, WARP_COUNT };
inline const std::vector<std::string>& warpNames()
{
    static const std::vector<std::string> v { "Kein Warp", "Sync", "Bend +", "Bend -", "PWM", "Spiegel", "Fold", "Quantize", "Asym", "Self-FM" };
    return v;
}
/** Verbiegt die Lesephase, das ist das Herz von Serum- und Vital-Sounds. */
inline float warpPhase (float ph, int mode, float a, float last)
{
    switch (mode)
    {
        case WARP_SYNC:   { const float p = ph * (1.0f + a * 7.0f); return p - std::floor (p); }
        case WARP_BENDP:  return std::pow (ph, 1.0f + 3.0f * a);
        case WARP_BENDM:  return 1.0f - std::pow (1.0f - ph, 1.0f + 3.0f * a);
        case WARP_PWM:    { const float d = 0.5f - 0.47f * a; return ph < d ? 0.5f * ph / d : 0.5f + 0.5f * (ph - d) / (1.0f - d); }
        case WARP_MIRROR: { const float d = 0.5f + 0.49f * (1.0f - a); return ph < d ? ph / d : (1.0f - ph) / (1.0f - d); }
        case WARP_FOLD:   { float p = ph * (1.0f + a * 4.0f); p -= 2.0f * std::floor (p * 0.5f); return p > 1.0f ? 2.0f - p : p; }
        case WARP_QUANT:  { const float st = 32.0f - 29.0f * a; return std::floor (ph * st) / st; }
        case WARP_ASYM:   { const float d = 0.5f + 0.45f * a; return ph < d ? 0.5f * ph / d : 0.5f + 0.5f * (ph - d) / (1.0f - d); }
        case WARP_FM:     { const float p = ph + 0.35f * a * last; return p - std::floor (p); }
        default:          return ph;
    }
}
} // namespace tk
