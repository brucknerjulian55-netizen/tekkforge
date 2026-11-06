// Prueft die Klangerzeugung ohne JUCE: g++ -std=c++17 -O2 -ISource Tests/engine_test.cpp -o engine_test
#include "dsp/SampleAnalysis.h"
#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>
using namespace tk;

static std::vector<float> defaults()
{
    std::vector<float> v ((size_t) NUM_PARAMS);
    const auto& t = params();
    for (int i = 0; i < NUM_PARAMS; ++i) v[(size_t) i] = t[(size_t) i].def;
    return v;
}
static void render (Engine& e, std::vector<float>& v, int note, float secs, float noteLen, float sr, std::vector<float>& L, std::vector<float>& R)
{
    const int n = (int) (secs * sr), offAt = (int) (noteLen * sr);
    L.assign ((size_t) n, 0); R.assign ((size_t) n, 0);
    e.noteOn (note, 1.0f, v.data());
    bool off = false;
    for (int i = 0; i < n; i += 256)
    {
        if (! off && i >= offAt) { e.noteOff (note, v.data()); off = true; }
        e.process (L.data() + i, R.data() + i, std::min (256, n - i), v.data(), 170.0, -1.0);
    }
}
int main()
{
    const float sr = 88200;
    int fails = 0;
    auto eng = std::make_unique<Engine>();
    eng->allocate (sr);
    std::printf ("params=%d\n", NUM_PARAMS);

    const float lens[5] = { 0.2f, 0.2f, 0.1f, 0.25f, 0.3f };
    const int notes[5] = { 60, 60, 41, 29, 29 };
    for (int m = 0; m < NUM_MODES; ++m)
    {
        auto v = defaults(); v[G_MODE] = (float) m;
        eng->setSampleRate (sr); eng->reset();
        std::vector<float> L, R;
        render (*eng, v, notes[m], 1.5f, lens[m], sr, L, R);
        float pk = 0, rms = 0; bool bad = false; size_t last = 0;
        for (size_t i = 0; i < L.size(); ++i)
        {
            if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) bad = true;
            pk = std::max (pk, std::fabs (L[i])); rms += L[i] * L[i];
            if (std::fabs (L[i]) > 0.001f) last = i;
        }
        std::printf ("%-14s peak %.3f len %4.0f ms voices_end %d nan %d\n", modeName (m), pk, (float) last / sr * 1000, eng->activeVoices(), bad);
        if (bad || pk < 0.05f || pk > 1.001f || eng->activeVoices() != 0) ++fails;
        std::ofstream ("render_" + std::to_string (m) + ".raw", std::ios::binary).write ((const char*) L.data(), (std::streamsize) (L.size() * 4));
    }

    // Stimmung: Kick-Schwanz F1 muss 43.65 Hz treffen (Nulldurchgaenge im Schwanz, ohne Click und Drive)
    {
        auto v = defaults(); v[G_MODE] = FULLKICK;
        v[modeParam (FULLKICK, M_DRV)] = 0; v[modeParam (FULLKICK, M_CLKLVL)] = 0; v[modeParam (FULLKICK, M_DEC)] = 900; v[modeParam (FULLKICK, M_LOWCUT)] = 20;
        eng->reset(); std::vector<float> L, R;
        render (*eng, v, 29, 1.0f, 1.0f, sr, L, R);
        const size_t a = (size_t) (0.15f * sr), b = (size_t) (0.7f * sr);
        std::vector<double> cross;
        for (size_t i = a; i < b; ++i)
            if (L[i - 1] <= 0 && L[i] > 0) cross.push_back ((double) (i - 1) + (double) (-L[i - 1] / (L[i] - L[i - 1])));
        const double hz = (double) sr * (double) (cross.size() - 1) / (cross.back() - cross.front());
        std::printf ("Kick-Schwanz F1: %.2f Hz (Soll 43.65)\n", hz);
        if (std::fabs (hz - 43.65) > 0.3) ++fails;
    }

    // Jeder Effekt doppelt in zwei Slots, bei Extremwerten
    for (int type = 1; type < FX_TYPES; ++type)
        for (int pattern = 0; pattern < 3; ++pattern)
        {
            auto v = defaults(); v[G_MODE] = STAB;
            for (int s = 0; s < 2; ++s)
            {
                v[slotParam (s, S_TYPE)] = (float) type; v[slotParam (s, S_MIX)] = 1;
                for (int k = 0; k < FX_PARAMS; ++k)
                    v[slotParam (s, S_P1 + k)] = pattern == 0 ? fxNorm (fxInfo (type)[k], fxInfo (type)[k].def) : pattern == 1 ? 1.0f : 0.0f;
            }
            v[G_LIMIT] = 0;
            eng->reset(); std::vector<float> L, R;
            render (*eng, v, 48, 1.2f, 0.3f, sr, L, R);
            float pk = 0; bool bad = false;
            for (size_t i = 0; i < L.size(); ++i) { if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) bad = true; pk = std::max (pk, std::fabs (L[i])); }
            if (bad || pk > 300) { ++fails; std::printf ("FX FAIL "); }
            if (pattern == 0 || bad || pk > 300) std::printf ("fx %-11s pattern %d peak %.2f nan %d\n", fxNames()[(size_t) type].c_str(), pattern, pk, bad);
        }

    // Interne Automation: Rampe auf Cutoff muss den Parameter wirklich bewegen
    {
        std::array<std::array<std::atomic<float>, LANE_POINTS>, NUM_LANES> curves;
        std::array<std::atomic<float>, NUM_PARAMS> view;
        for (auto& c : curves) for (int i = 0; i < LANE_POINTS; ++i) c[(size_t) i].store (1.0f - (float) i / (LANE_POINTS - 1));
        for (auto& x : view) x.store (0);
        eng->curves = &curves; eng->modView = &view;
        auto v = defaults(); v[G_MODE] = STAB;
        const int target = modeParam (STAB, M_CUT);
        v[laneParam (0, L_ON)] = 1; v[(size_t) laneParam (0, L_TARGET)] = (float) target; v[laneParam (0, L_AMOUNT)] = 0.5f; v[laneParam (0, L_RATE)] = 4;
        eng->reset(); eng->noteOn (60, 1, v.data());
        std::vector<float> L (256), R (256);
        eng->process (L.data(), R.data(), 256, v.data(), 170, -1);
        const float early = view[(size_t) target].load();
        for (int i = 0; i < 400; ++i) eng->process (L.data(), R.data(), 256, v.data(), 170, -1);
        const float late = view[(size_t) target].load(), base = params()[(size_t) target].toNorm (v[(size_t) target]);
        std::printf ("Automation Cutoff norm: Basis %.3f frueh %.3f spaet %.3f\n", base, early, late);
        if (! (early > late + 0.3f) || std::fabs (late - base) > 0.02f) ++fails;
    }
    // Sample: Sinus 440 Hz, Grundton A4, gespielt auf A5 muss 880 Hz ergeben und ganz durchlaufen
    {
        SampleData sd; sd.rate = 44100; sd.l.resize (44100);
        for (int i = 0; i < 44100; ++i) sd.l[(size_t) i] = 0.5f * std::sin (2 * kPi * 440.0f * (float) i / 44100.0f);
        std::atomic<const SampleData*> slots[NUM_MODES]; for (auto& x : slots) x.store (&sd);
        eng->curves = nullptr; eng->modView = nullptr; eng->samples = slots;
        auto v = defaults(); v[G_MODE] = MELO;
        const auto M = [&] (int p) -> float& { return v[(size_t) modeParam (MELO, p)]; };
        M (M_OSCLVL) = 0; M (M_SMPROOT) = 69; M (M_SMPPITCH) = 1; M (M_SMPFREE) = 1; M (M_CHORD) = 0; M (M_LOWCUT) = 20;
        M (M_CUT) = 20000; M (M_FENV) = 0; M (M_DRV) = 0; M (M_LEVEL) = 0; v[G_LIMIT] = 0;
        eng->reset(); std::vector<float> L, R;
        render (*eng, v, 81, 0.8f, 0.1f, sr, L, R);
        std::vector<double> cross;
        for (size_t i = (size_t) (0.05f * sr); i < (size_t) (0.4f * sr); ++i)
            if (L[i - 1] <= 0 && L[i] > 0) cross.push_back ((double) (i - 1) + (double) (-L[i - 1] / (L[i] - L[i - 1])));
        const double hz = (double) sr * (double) (cross.size() - 1) / (cross.back() - cross.front());
        size_t last = 0; for (size_t i = 0; i < L.size(); ++i) if (std::fabs (L[i]) > 0.01f) last = i;
        std::printf ("Sample A5: %.1f Hz (Soll 880), Ende %.0f ms (Soll 500), Stimmen %d\n", hz, (float) last / sr * 1000, eng->activeVoices());
        if (std::fabs (hz - 880) > 2 || std::fabs ((float) last / sr - 0.5f) > 0.02f || eng->activeVoices() != 0) ++fails;
        eng->samples = nullptr;
    }

    // Env 1 auf Tune: Tonhoehe muss zu Beginn hoeher sein. LFO 1 auf Pegel: Pegel muss schwanken.
    {
        auto v = defaults(); v[G_MODE] = KICKBASS;
        const auto M = [&] (int p) -> float& { return v[(size_t) modeParam (KICKBASS, p)]; };
        M (M_PAMT) = 0; M (M_DRV) = 0; M (M_CLKLVL) = 0; M (M_SUBLVL) = 0; M (M_WAVE) = 0; M (M_CUT) = 20000; M (M_GATE) = 0;
        M (M_HOLD) = 900; M (M_DEC) = 50;
        v[(size_t) modSlot (0, MD_SOURCE)] = NUM_LFOS; v[(size_t) modSlot (0, MD_TARGET)] = (float) modeParam (KICKBASS, M_TUNE); v[(size_t) modSlot (0, MD_AMOUNT)] = 0.5f;
        v[(size_t) envParam (0, EV_DEC)] = 300;
        eng->reset(); std::vector<float> L, R;
        render (*eng, v, 45, 0.9f, 0.9f, sr, L, R);
        const auto cycles = [&] (float a, float b) { int c = 0; for (size_t i = (size_t) (a * sr) + 1; i < (size_t) (b * sr); ++i) c += L[i - 1] <= 0 && L[i] > 0; return (float) c / (b - a); };
        const float early = cycles (0.0f, 0.05f), late = cycles (0.5f, 0.8f);
        std::printf ("Env auf Tune: frueh %.0f Hz, spaet %.0f Hz\n", early, late);
        if (early < late * 2.5f || std::fabs (late - 110) > 2) ++fails;

        v[(size_t) modSlot (0, MD_SOURCE)] = 0; v[(size_t) modSlot (0, MD_TARGET)] = (float) modeParam (KICKBASS, M_LEVEL); v[(size_t) modSlot (0, MD_AMOUNT)] = 0.45f;
        v[(size_t) lfoParam (0, LF_SYNC)] = 0; v[(size_t) lfoParam (0, LF_HZ)] = 8; M (M_LEVEL) = -18;
        eng->reset(); render (*eng, v, 45, 0.8f, 0.8f, sr, L, R);
        float mn = 9, mx = 0;
        for (int w = 0; w < 14; ++w) { float pk = 0; for (size_t i = 0; i < (size_t) (0.02f * sr); ++i) pk = std::max (pk, std::fabs (L[(size_t) ((float) w * 0.05f * sr) + i])); mn = std::min (mn, pk); mx = std::max (mx, pk); }
        std::printf ("LFO auf Pegel: min %.3f max %.3f\n", mn, mx);
        if (mx < mn * 3) ++fails;
    }
    // Fertig rendern: Sample muss nach dem Zuruecksetzen identisch zum Originalsound klingen
    for (int m : { FULLKICK, KICKBASS, STAB })
    {
        auto v = defaults(); v[G_MODE] = (float) m;
        v[(size_t) slotParam (0, S_TYPE)] = FX_DESTRUCTOR;
        for (int k = 0; k < FX_PARAMS; ++k) v[(size_t) slotParam (0, S_P1 + k)] = fxNorm (fxInfo (FX_DESTRUCTOR)[k], fxInfo (FX_DESTRUCTOR)[k].def);
        v[(size_t) slotParam (0, S_MIX)] = 0.4f;
        const int note = m == STAB ? 60 : 29;
        eng->reset(); std::vector<float> L, R;
        render (*eng, v, note, 1.2f, 0.6f, sr, L, R);
        SampleData sd; sd.rate = sr; sd.l = L; sd.r = R;
        std::atomic<const SampleData*> slots[NUM_MODES]; for (auto& x : slots) x.store (&sd);
        eng->samples = slots;
        auto f = defaults();
        for (const auto& e : frozenValues (m, note)) f[(size_t) e.index] = e.value;
        f[G_QUALITY] = 0;
        eng->reset(); std::vector<float> L2, R2;
        render (*eng, f, note, 1.1f, 1.1f, sr, L2, R2);
        double err = 0, ref = 0;
        for (size_t i = 0; i < L2.size(); ++i) { const double e = L2[i] - L[i], q = R2[i] - R[i]; err += e * e + q * q; ref += L[i] * L[i] + R[i] * R[i]; }
        const double db = 10 * std::log10 (err / ref + 1e-30);
        std::printf ("Render %-10s Abweichung %.1f dB\n", modeName (m), db);
        if (db > -80) ++fails;
        eng->samples = nullptr;
    }
    // Analyse: Grundton, Noise und Vorschlaege
    {
        const auto make = [] (int kind)
        {
            SampleData d; d.rate = 44100; d.l.resize (22050); unsigned r = 123;
            for (int i = 0; i < 22050; ++i)
            {
                const float t = (float) i / 44100.0f, e = std::exp (-t * 6.0f);
                r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                float x = 0;
                if (kind == 0) x = std::sin (2 * kPi * 110.0f * t);                                   // reiner Ton
                if (kind == 1) x = softClip (6.0f * std::sin (2 * kPi * 233.08f * t));                // verzerrter Ton
                if (kind == 2) x = (float) (r & 0xffff) / 32768.0f - 1.0f;                            // Rauschen
                d.l[(size_t) i] = 0.8f * e * x;
            }
            return d;
        };
        const auto a = analyzeSample (make (0)), b = analyzeSample (make (1)), c = analyzeSample (make (2));
        std::printf ("Analyse: Ton %.1f Hz (110) Note %d, Verzerrt %.1f Hz (233.1) Note %d, Rauschen tonal=%d\n", a.hz, a.note, b.hz, b.note, c.tonal);
        if (! a.tonal || std::fabs (a.hz - 110) > 1 || a.note != 45 || ! b.tonal || std::fabs (b.hz - 233.08f) > 2 || b.note != 58 || c.tonal) ++fails;
        const auto sg = suggestSettings (KICKBASS, a);
        std::printf ("Kickbass-Vorschlag: spiele Note %d, %d Werte\n", sg.playNote, (int) sg.items.size());
        if (sg.playNote != 33) ++fails;   // 110 Hz liegt eine Oktave ueber dem Ziel, also A1 = 55 Hz
        for (int m = 0; m < NUM_MODES; ++m)
            for (const auto& e : suggestSettings (m, a).items)
                if (e.value < params()[(size_t) e.index].min || e.value > params()[(size_t) e.index].max) { ++fails; std::printf ("Bereich verletzt: %s\n", params()[(size_t) e.index].id.c_str()); }
    }
    std::printf (fails ? "FEHLER: %d\n" : "alles ok (%d Fehler)\n", fails);
    return fails != 0;
}
