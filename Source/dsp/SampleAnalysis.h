// Tekkforge - misst ein Sample und leitet daraus Einstellungen je Modus ab (ohne JUCE, damit es testbar bleibt).
#pragma once
#include "Engine.h"
#include <complex>

namespace tk
{
struct SampleReport
{
    float seconds = 0, peak = 0, attackMs = 0;
    float band[5] {};                  // Anteile: unter 40 Hz, 40-120, 120-500, 500-3k, ueber 3k
    bool tonal = false;                // klarer Grundton gefunden
    float hz = 0, confidence = 0, cents = 0;
    int note = 0;                      // naechster MIDI-Ton
};

inline void simpleFft (std::vector<std::complex<float>>& a)
{
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
        const std::complex<float> step = std::polar (1.0f, -2.0f * kPi / (float) len);
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

inline SampleReport analyzeSample (const SampleData& s)
{
    SampleReport r;
    const int n = s.length();
    if (n < 64) return r;
    r.seconds = (float) n / s.rate;

    std::vector<float> m ((size_t) n);
    for (int i = 0; i < n; ++i) m[(size_t) i] = s.r.empty() ? s.l[(size_t) i] : 0.5f * (s.l[(size_t) i] + s.r[(size_t) i]);

    int peakAt = 0;
    for (int i = 0; i < n; ++i) if (std::fabs (m[(size_t) i]) > r.peak) { r.peak = std::fabs (m[(size_t) i]); peakAt = i; }
    if (r.peak < 1.0e-5f) return r;
    // Attack: Zeit bis zur halben Spitze. Die absolute Spitze kann bei Kicks viel spaeter im Schwanz liegen.
    for (int i = 0; i < n; ++i) if (std::fabs (m[(size_t) i]) >= r.peak * 0.5f) { r.attackMs = (float) i / s.rate * 1000.0f; break; }

    // Frequenzverteilung
    {
        size_t size = 4096;
        while (size < (1u << 17) && size * 2 <= (size_t) n) size *= 2;
        std::vector<std::complex<float>> a (size);
        const size_t use = std::min (size, (size_t) n);
        for (size_t i = 0; i < use; ++i) a[i] = m[i] * (0.5f + 0.5f * std::cos (kPi * (float) i / (float) use));
        simpleFft (a);
        static const float edge[6] = { 0, 40, 120, 500, 3000, 1e9f };
        float total = 1.0e-12f;
        for (size_t i = 1; i < size / 2; ++i)
        {
            const float hz = (float) i * s.rate / (float) size, e = std::norm (a[i]);
            for (int b = 0; b < 5; ++b) if (hz >= edge[b] && hz < edge[b + 1]) r.band[b] += e;
            total += e;
        }
        for (auto& b : r.band) b /= total;
    }

    // Grundton: tiefpassen, auf rund 4 kHz reduzieren, dann normierte Autokorrelation ueber den Ausklang
    const int dec = std::max (1, (int) (s.rate / 4000.0f));
    const float rate = s.rate / (float) dec;
    std::vector<float> d;
    for (int i = 0; i + dec <= n; i += dec)
    {
        float sum = 0;
        for (int k = 0; k < dec; ++k) sum += m[(size_t) (i + k)];
        d.push_back (sum / (float) dec);
    }
    const int dn = (int) d.size(), dp = peakAt / dec;
    float dpk = 0; for (auto v : d) dpk = std::max (dpk, std::fabs (v));
    int last = dn - 1;
    while (last > dp && std::fabs (d[(size_t) last]) < dpk * 0.03f) --last;
    const int from = dp + (last - dp) * 15 / 100, seg = std::min (last - from, 8192);
    const int maxLag = std::min (seg / 2, (int) (rate / 30.0f)), minLag = std::max (2, (int) (rate / 1500.0f));
    if (maxLag <= minLag + 2) return r;

    std::vector<float> nsdf ((size_t) maxLag + 2, 0.0f);
    float best = 0;
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        float ac = 0, en = 1.0e-12f;
        for (int i = from; i + lag < from + seg; ++i)
        {
            const float a = d[(size_t) i], b = d[(size_t) (i + lag)];
            ac += a * b; en += a * a + b * b;
        }
        nsdf[(size_t) lag] = 2.0f * ac / en;
    }
    for (int lag = minLag + 1; lag <= maxLag; ++lag) best = std::max (best, nsdf[(size_t) lag]);
    if (best < 0.6f) { r.confidence = best; return r; }
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
    {
        const float a = nsdf[(size_t) lag - 1], b = nsdf[(size_t) lag], c = nsdf[(size_t) lag + 1];
        if (b >= a && b > c && b >= best * 0.9f)
        {
            const float denom = a - 2 * b + c, off = denom != 0 ? 0.5f * (a - c) / denom : 0.0f;
            r.hz = rate / ((float) lag + off);
            r.confidence = b;
            r.tonal = true;
            const float midi = 69.0f + 12.0f * std::log2 (r.hz / 440.0f);
            r.note = (int) std::round (midi);
            r.cents = (midi - (float) r.note) * 100.0f;
            break;
        }
    }
    return r;
}

struct Suggestion { int index; float value; std::string why; };
struct SuggestionSet { std::vector<Suggestion> items; int playNote = 29; };

/** Vorschlaege fuer den aktuellen Modus. Faustregeln als Startpunkt, kein Ersatz fuers Ohr. */
inline SuggestionSet suggestSettings (int mode, const SampleReport& r)
{
    SuggestionSet out;
    const auto add = [&] (int p, float v, std::string why = {}) { out.items.push_back ({ modeParam (mode, p), v, std::move (why) }); };
    const float bass = r.band[0] + r.band[1];

    const auto tuneTo = [&] (float targetHz)
    {
        add (M_SMPPITCH, 1);
        if (! r.tonal) { add (M_SMPROOT, 29); add (M_SMPTUNE, 0); out.playNote = 29; return; }
        const int oct = (int) std::round (std::log2 (r.hz / targetHz));
        add (M_SMPROOT, (float) r.note, "Grundton auf den gemessenen Ton gesetzt");
        add (M_SMPTUNE, -r.cents / 100.0f, "Feinstimmung gleicht die Abweichung aus");
        out.playNote = std::clamp (r.note - 12 * oct, 12, 108);
    };

    switch (mode)
    {
        case KICKBASS:
            tuneTo (52.0f);
            add (M_SMPFREE, 0, "Die Hüllkurve bestimmt die Länge, damit der Schwanz vor der nächsten Kick endet");
            add (M_GATE, 1); add (M_ATT, 0); add (M_HOLD, 60); add (M_DEC, 180); add (M_SUS, 0); add (M_REL, 20);
            add (M_FTYPE, 0); add (M_CUT, 2200, "Tiefpass nimmt Höhen raus, der Bass bleibt im Fokus"); add (M_RES, 0.1f);
            add (M_LOWCUT, 30, "Unter 30 Hz kostet nur Headroom");
            add (M_SUBLVL, bass < 0.5f ? 0.7f : 0.35f, bass < 0.5f ? "Das Sample hat wenig Bass, der Sub trägt das Fundament" : "Das Sample hat schon Bass, der Sub stützt nur");
            add (M_CLKLVL, r.attackMs > 5 ? 0.3f : 0.1f, r.attackMs > 5 ? "Weicher Anfang im Sample, der Click gibt Attack" : "Das Sample hat genug Attack");
            add (M_DRV, 0.45f, "Verzerrung macht den Bass auf kleinen Boxen hörbar"); add (M_DRVTYPE, 3); add (M_DRVTONE, 5000);
            break;
        case FULLKICK:
            tuneTo (52.0f);
            add (M_SMPFREE, 0); add (M_GATE, 0); add (M_ATT, 0); add (M_HOLD, 40); add (M_DEC, 320, "Rund 320 ms passen zu etwa 180 BPM");
            add (M_FTYPE, 3); add (M_LOWCUT, 30);
            add (M_SUBLVL, bass < 0.4f ? 0.5f : 0.2f, bass < 0.4f ? "Wenig Bass im Sample, der Sub ergänzt ihn" : "Genug Bass im Sample");
            add (M_CLKLVL, r.attackMs > 3 ? 0.5f : 0.15f, r.attackMs > 3 ? "Weicher Anfang, der Click schärft den Attack" : "Der Attack ist schon da");
            add (M_DRV, 0.5f); add (M_DRVTYPE, 3); add (M_DRVTONE, 7000);
            break;
        case TOPKICK:
            add (M_SMPPITCH, 0); add (M_SMPFREE, 0, "Kurze Hüllkurve lässt nur den Anschlag übrig");
            add (M_GATE, 0); add (M_ATT, 0); add (M_HOLD, 5); add (M_DEC, 70);
            add (M_FTYPE, 3); add (M_LOWCUT, 240, "Der Bass gehört der Kick, hier wird alles darunter entfernt");
            add (M_SUBLVL, 0); add (M_CLKLVL, 0.3f); add (M_DRV, 0.5f); add (M_DRVTYPE, 3); add (M_DRVTONE, 7000);
            out.playNote = 41;
            break;
        case MELO:
            add (M_SMPPITCH, 1); add (M_SMPROOT, (float) (r.tonal ? r.note : 60)); add (M_SMPTUNE, r.tonal ? -r.cents / 100.0f : 0.0f);
            add (M_SMPFREE, 1); add (M_FTYPE, 3);
            add (M_LOWCUT, r.tonal ? std::clamp (r.hz * 0.7f, 120.0f, 300.0f) : 150.0f, "Lässt unten Platz für den Kick-Bass");
            add (M_SUBLVL, 0); add (M_CLKLVL, 0); add (M_DRV, 0.1f);
            out.playNote = r.tonal ? r.note : 60;
            break;
        default:
            add (M_SMPPITCH, 1); add (M_SMPROOT, (float) (r.tonal ? r.note : 60)); add (M_SMPTUNE, r.tonal ? -r.cents / 100.0f : 0.0f);
            add (M_SMPFREE, 0, "Stabs leben von einer kurzen Hüllkurve"); add (M_GATE, 0);
            add (M_ATT, 1); add (M_HOLD, 20); add (M_DEC, 260); add (M_REL, 120);
            add (M_FTYPE, 0); add (M_CUT, 3000); add (M_RES, 0.2f); add (M_FENV, 2); add (M_FDEC, 160);
            add (M_LOWCUT, 180, "Unter 180 Hz stört ein Stab nur den Bass");
            add (M_SUBLVL, 0); add (M_CLKLVL, 0);
            out.playNote = r.tonal ? r.note : 60;
            break;
    }
    return out;
}
} // namespace tk
