// Prueft die Klangerzeugung ohne JUCE: g++ -std=c++17 -O2 -ISource Tests/engine_test.cpp -o engine_test
#include "dsp/SampleAnalysis.h"
#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>
using namespace tk;

constexpr float SR = 88200;
static std::vector<float> defaults()
{
    std::vector<float> v ((size_t) NUM_PARAMS);
    const auto& t = params();
    for (int i = 0; i < NUM_PARAMS; ++i) v[(size_t) i] = t[(size_t) i].def;
    return v;
}
static void render (Engine& e, std::vector<float>& v, int note, float secs, float noteLen, std::vector<float>& L, std::vector<float>& R)
{
    const int n = (int) (secs * SR), offAt = (int) (noteLen * SR);
    L.assign ((size_t) n, 0); R.assign ((size_t) n, 0);
    e.noteOn (note, 1.0f, v.data());
    bool off = false;
    for (int i = 0; i < n; i += 256)
    {
        if (! off && i >= offAt) { e.noteOff (note, v.data()); off = true; }
        e.process (L.data() + i, R.data() + i, std::min (256, n - i), v.data(), 170.0, -1.0);
    }
}
static double tone (const std::vector<float>& x, float hz, float a0, float b0)
{
    double re = 0, im = 0; const size_t a = (size_t) (a0 * SR), b = (size_t) (b0 * SR);
    for (size_t i = a; i < b; ++i) { const double w = 6.283185307 * hz * (double) i / SR; re += x[i] * std::cos (w); im += x[i] * std::sin (w); }
    return 2.0 * std::sqrt (re * re + im * im) / (double) (b - a);
}
static double hzOf (const std::vector<float>& x, float a0, float b0)
{
    std::vector<double> c;
    for (size_t i = (size_t) (a0 * SR); i < (size_t) (b0 * SR); ++i)
        if (x[i - 1] <= 0 && x[i] > 0) c.push_back ((double) (i - 1) + (double) (-x[i - 1] / (x[i] - x[i - 1])));
    return c.size() < 2 ? 0.0 : SR * (double) (c.size() - 1) / (c.back() - c.front());
}
static float lastSound (const std::vector<float>& x, float limit = 0.002f)
{
    size_t l = 0; for (size_t i = 0; i < x.size(); ++i) if (std::fabs (x[i]) > limit) l = i;
    return (float) l / SR * 1000.0f;
}
static float peak (const std::vector<float>& x, float a, float b)
{
    float p = 0; for (size_t i = (size_t) (a * SR); i < (size_t) (b * SR) && i < x.size(); ++i) p = std::max (p, std::fabs (x[i])); return p;
}

int main()
{
    int fails = 0;
    const auto check = [&] (bool ok, const char* what) { if (! ok) { ++fails; std::printf ("FEHLER: %s\n", what); } };
    WavetableBank::get().wait();
    auto eng = std::make_unique<Engine>();
    eng->allocate (SR); eng->setSampleRate (SR);
    std::printf ("params=%d\n", NUM_PARAMS);

    for (int m = 0; m < NUM_MODES; ++m)
    {
        auto v = defaults(); v[G_MODE] = (float) m;
        eng->reset(); std::vector<float> L, R;
        render (*eng, v, m == KICK ? 29 : 60, 1.5f, m == KICK ? 0.3f : 0.2f, L, R);
        bool bad = false; for (auto x : L) if (! std::isfinite (x)) bad = true;
        const float pk = peak (L, 0, 1.5f);
        std::printf ("%-5s peak %.3f len %4.0f ms Stimmen %d\n", modeName (m), pk, lastSound (L), eng->activeVoices());
        check (! bad && pk > 0.05f && pk < 1.001f && eng->activeVoices() == 0, "Startklang");
    }

    // Kick: Solo trennt die Ebenen, jede Ebene hat ihre eigene Laenge
    {
        const auto Yp = [] (int l, int p) { return (size_t) modeParam (KICK, Y (l, p)); };
        auto v = defaults(); v[G_MODE] = KICK; v[G_LIMIT] = 0;
        std::vector<float> L, R;
        auto a = v; a[Yp (0, LY_SOLO)] = 1;
        eng->reset(); render (*eng, a, 29, 1.0f, 0.6f, L, R); const float top = lastSound (L);
        auto b = v; b[Yp (1, LY_SOLO)] = 1;
        eng->reset(); render (*eng, b, 29, 1.0f, 0.6f, L, R); const float bass = lastSound (L);
        auto c = b; c[Yp (1, LY_LEN)] = 800;
        eng->reset(); render (*eng, c, 29, 1.0f, 0.95f, L, R); const float longB = lastSound (L);
        std::printf ("Kick-Ebenen: Topkick %.0f ms, Bass %.0f ms, Bass Laenge 800 = %.0f ms\n", top, bass, longB);
        check (top > 40 && top < 110, "Topkick-Laenge"); check (bass > 200 && bass < 340, "Bass-Laenge"); check (longB > 500 && longB < 850, "Laenge-Regler");
    }

    // Spuerbarkeit: Jeder Hauptregler muss das Signal messbar aendern
    {
        const auto Yp = [] (int p) { return (size_t) modeParam (KICK, Y (1, p)); };
        auto base = defaults(); base[G_MODE] = KICK; base[(size_t) modeParam (KICK, Y (1, LY_SOLO))] = 1;
        std::vector<float> A, R, B;
        eng->reset(); render (*eng, base, 29, 0.5f, 0.5f, A, R);
        const auto effect = [&] (int p, float value, const char* name)
        {
            auto v = base; v[Yp (p)] = value;
            eng->reset(); render (*eng, v, 29, 0.5f, 0.5f, B, R);
            double diff = 0, ref = 1e-9; for (size_t i = 0; i < A.size(); ++i) { diff += (A[i] - B[i]) * (A[i] - B[i]); ref += A[i] * A[i]; }
            const double db = 10 * std::log10 (diff / ref + 1e-12);
            std::printf ("  %-10s Aenderung %6.1f dB\n", name, db);
            check (db > -22, name);
        };
        effect (LY_TABLE, 2.0f, "Welle"); effect (LY_DRV, 0.8f, "Drive"); effect (LY_CUT, 300.0f, "Cutoff"); effect (LY_PAMT, 60.0f, "Pitch");
        effect (LY_PTIME, 150.0f, "Pitch-Zeit"); effect (LY_CURVE, 0.9f, "Kurve"); effect (LY_LVL, 0.5f, "Pegel"); effect (LY_SUB, 0.8f, "Sub");
        effect (LY_TUNE, 7.0f, "Tune");
    }

    // Bass-Lage: Jede Taste muss zwischen 40 und 80 Hz landen, ohne Tune anzufassen
    {
        const auto Yp = [] (int l, int p) { return (size_t) modeParam (KICK, Y (l, p)); };
        auto v = defaults(); v[G_MODE] = KICK;
        v[Yp (1, LY_SOLO)] = 1; v[Yp (1, LY_DRV)] = 0; v[Yp (1, LY_LEN)] = 2000; v[Yp (1, LY_PAMT)] = 0;
        for (int note : { 29, 48, 60, 72, 84 })
        {
            eng->reset(); std::vector<float> L, R;
            render (*eng, v, note, 0.5f, 0.5f, L, R);
            const double hz = hzOf (L, 0.1f, 0.45f);
            std::printf ("Bass-Lage Note %d: %.1f Hz\n", note, hz);
            check (hz >= 39.5 && hz < 81, "Bass-Lage");
        }
    }

    // Wellen: Tonhoehe und keine harten Spruenge bis in hohe Lagen, Warp veraendert den Klang
    {
        auto v = defaults(); v[G_MODE] = MELO; v[G_LIMIT] = 0;
        const auto M = [&] (int p) -> float& { return v[(size_t) modeParam (MELO, p)]; };
        for (auto& e : std::initializer_list<std::pair<int, float>> { { LY_UNI, 1 }, { LY_DRV, 0 }, { LY_FTYPE, 9 }, { LY_LOWCUT, 20 }, { LY_HOLD, 1 }, { LY_SUS, 1 }, { LY_FAMT, 0 } }) M (Y (0, e.first)) = e.second;
        int bad = 0;
        for (int t = 0; t < NUM_TABLES; ++t)
        {
            if (! std::fabs (0.0f) && false) continue;
            M (Y (0, LY_TABLE)) = (float) t; M (Y (0, LY_WAVE)) = 0.5f;
            eng->reset(); std::vector<float> L, R;
            render (*eng, v, 60, 0.3f, 0.3f, L, R);
            float pk = peak (L, 0.05f, 0.25f); bool nanv = false; for (auto x : L) nanv = nanv || ! std::isfinite (x);
            if (pk < 0.05f || nanv) { ++bad; std::printf ("Welle %d still (%s)\n", t, waveInfos()[t].name); }
        }
        std::printf ("%d Wellen geprueft, Probleme %d\n", NUM_TABLES, bad); check (bad == 0, "Wellen");

        M (Y (0, LY_TABLE)) = 2; M (Y (0, LY_WAVE)) = 0;
        eng->reset(); std::vector<float> A, R, B;
        render (*eng, v, 60, 0.3f, 0.3f, A, R);
        for (int w = 1; w < WARP_COUNT; ++w)
        {
            M (Y (0, LY_WARP)) = (float) w; M (Y (0, LY_WARPAMT)) = 0.6f;
            eng->reset(); render (*eng, v, 60, 0.3f, 0.3f, B, R);
            double diff = 0, ref = 1e-9; for (size_t i = 0; i < A.size(); ++i) { diff += (A[i] - B[i]) * (A[i] - B[i]); ref += A[i] * A[i]; }
            check (10 * std::log10 (diff / ref + 1e-12) > -20, warpNames()[(size_t) w].c_str());
        }
        M (Y (0, LY_WARP)) = 0;
        // Alias-Test: Saege auf C8 bei 88,2 kHz, Energie zwischen den Teiltoenen muss klein bleiben
        M (Y (0, LY_TABLE)) = 2;
        eng->reset(); render (*eng, v, 108, 0.3f, 0.3f, A, R);
        const double fund = tone (A, 4186.0f, 0.05f, 0.25f), between = tone (A, 6279.0f, 0.05f, 0.25f);
        std::printf ("Saege C8: Grundton %.3f, zwischen den Teiltoenen %.5f\n", fund, between);
        check (between < fund * 0.01, "Alias");
    }

    // Filter, Verzerrung, Bass-Charaktere
    {
        const auto Yp = [] (int p) { return (size_t) modeParam (KICK, Y (1, p)); };
        auto base = defaults(); base[G_MODE] = KICK; base[Yp (LY_SOLO)] = 1; base[Yp (LY_LEN)] = 400; base[Yp (LY_TABLE)] = 2; base[Yp (LY_KEY)] = 0;
        for (int f = 0; f < 9; ++f)
        {
            auto v = base; v[Yp (LY_FTYPE)] = (float) f; v[Yp (LY_CUT)] = 900; v[Yp (LY_RES)] = 0.6f;
            eng->reset(); std::vector<float> L, R; render (*eng, v, 45, 0.4f, 0.4f, L, R);
            bool bad = false; for (auto x : L) bad = bad || ! std::isfinite (x);
            check (! bad && peak (L, 0.02f, 0.2f) > 0.01f, "Filtertyp");
        }
        for (int d = 0; d < 10; ++d)
        {
            auto v = base; v[Yp (LY_DRVTYPE)] = (float) d; v[Yp (LY_DRV)] = 0.7f; v[Yp (LY_DRV2)] = 0.5f; v[Yp (LY_PREAMT)] = 12;
            eng->reset(); std::vector<float> L, R; render (*eng, v, 45, 0.4f, 0.4f, L, R);
            bool bad = false; for (auto x : L) bad = bad || ! std::isfinite (x);
            check (! bad && peak (L, 0.02f, 0.2f) > 0.01f && peak (L, 0.02f, 0.2f) < 2.5f, "Drive-Art");
        }
        for (const auto& pt : bassPatches())
        {
            auto v = defaults(); v[G_MODE] = KICK; v[Yp (LY_SOLO)] = 1;
            for (auto& e : patchValues (pt)) v[Yp (e.first)] = e.second;
            eng->reset(); std::vector<float> L, R; render (*eng, v, 60, 0.5f, 0.3f, L, R);
            const double low = tone (L, 65.4f, 0.08f, 0.25f) + tone (L, 41.2f, 0.08f, 0.25f);
            std::printf ("  %-16s Spitze %.2f\n", pt.name, peak (L, 0, 0.5f));
            check (peak (L, 0, 0.5f) > 0.1f && peak (L, 0, 0.5f) < 1.6f, pt.name); (void) low;
        }
    }

    // EQ: Glocke, Mid/Side, Cut-Flanke, Dynamik
    {
        auto v = defaults(); v[G_MODE] = MELO; v[G_LIMIT] = 0;
        const auto M = [&] (int p) -> float& { return v[(size_t) modeParam (MELO, p)]; };
        for (auto& e : std::initializer_list<std::pair<int, float>> { { LY_TABLE, 0 }, { LY_UNI, 1 }, { LY_DRV, 0 }, { LY_FTYPE, 9 }, { LY_LOWCUT, 20 }, { LY_HOLD, 1 }, { LY_SUS, 1 }, { LY_FAMT, 0 } }) M (Y (0, e.first)) = e.second;
        M (M_LEVEL) = 0; M (M_VEL) = 0;
        const auto level = [&] (std::vector<float>& val)
        {
            eng->reset(); std::vector<float> L, R; render (*eng, val, 69, 0.4f, 0.4f, L, R);
            return 20 * std::log10 (tone (L, 440, 0.15f, 0.35f) + 1e-9);
        };
        auto flat = v; const double base = level (flat);
        auto a = v; a[(size_t) eqParam (0, 0, E_ON)] = 1; a[(size_t) eqParam (0, 0, E_FREQ)] = 440; a[(size_t) eqParam (0, 0, E_GAIN)] = 12; a[(size_t) eqParam (0, 0, E_Q)] = 2;
        const double bell = level (a) - base;
        auto b = v; b[(size_t) eqParam (0, 0, E_ON)] = 1; b[(size_t) eqParam (0, 0, E_TYPE)] = EQ_LOWCUT; b[(size_t) eqParam (0, 0, E_FREQ)] = 880; b[(size_t) eqParam (0, 0, E_SLOPE)] = 2;
        const double cut48 = level (b) - base;
        b[(size_t) eqParam (0, 0, E_SLOPE)] = 0; const double cut12 = level (b) - base;
        auto c = a; c[(size_t) eqParam (0, 0, E_STEREO)] = ES_SIDE; const double side = level (c) - base;
        auto d = v; d[(size_t) eqParam (0, 0, E_ON)] = 1; d[(size_t) eqParam (0, 0, E_FREQ)] = 440; d[(size_t) eqParam (0, 0, E_DYN)] = -12; d[(size_t) eqParam (0, 0, E_THR)] = -30;
        const double dynG = level (d) - base;
        auto e = v; e[(size_t) eqParam (2, 0, E_ON)] = 1; e[(size_t) eqParam (1, 0, E_ON)] = 1; e[(size_t) eqParam (1, 0, E_FREQ)] = 440; e[(size_t) eqParam (1, 0, E_GAIN)] = -12; e[(size_t) eqParam (1, 0, E_Q)] = 2;
        const double layerEq = level (e) - base;
        std::printf ("EQ: Glocke +12 = %+.1f, Low Cut 48 dB %.1f, 12 dB %.1f, nur Side %+.1f, Dynamik %+.1f, Ebenen-EQ %+.1f\n", bell, cut48, cut12, side, dynG, layerEq);
        check (std::fabs (bell - 12) < 0.5, "EQ Glocke"); check (cut48 < -22 && cut12 > -16 && cut12 < -8, "EQ Flanke"); check (std::fabs (side) < 0.5, "EQ Side");
        check (dynG < -5, "EQ Dynamik"); check (std::fabs (layerEq + 12) < 0.5, "EQ Ebene");
    }

    // Neue Effekte: Alle Regler am Anschlag duerfen weder NaN noch Spruenge erzeugen, Standard darf nicht verstummen
    {
        for (int t = FX_OTT; t < FX_TYPES; ++t)
            for (int vals = 0; vals < 3; ++vals)
            {
                auto v = defaults(); v[G_MODE] = MELO; v[G_LIMIT] = 0;
                v[(size_t) slotParam (0, S_TYPE)] = (float) t; v[(size_t) slotParam (0, S_MIX)] = 1;
                const auto* info = fxInfo (t);
                for (int k = 0; k < FX_PARAMS; ++k)
                    v[(size_t) slotParam (0, S_P1 + k)] = vals == 0 ? info[k].def : vals == 1 ? info[k].min : info[k].max;
                eng->reset(); std::vector<float> L, R; render (*eng, v, 60, 0.4f, 0.4f, L, R);
                bool bad = false; float pk = 0; for (auto x : L) { bad = bad || ! std::isfinite (x); pk = std::max (pk, std::fabs (x)); }
                if (bad || pk > 30 || (vals == 0 && pk < 0.005f)) std::printf ("FX %s Satz %d: Spitze %.3f\n", fxNames()[(size_t) t].c_str(), vals, pk);
                check (! bad && pk <= 30, fxNames()[(size_t) t].c_str());
                if (vals == 0) check (pk > 0.005f, "FX klingt");
            }
    }

    // Stimmung
    {
        auto v = defaults(); v[G_MODE] = KICK;
        const auto Yp = [] (int l, int p) { return (size_t) modeParam (KICK, Y (l, p)); };
        v[Yp (1, LY_SOLO)] = 1; v[Yp (1, LY_DRV)] = 0; v[Yp (1, LY_LEN)] = 2000; v[Yp (1, LY_LOWCUT)] = 20; v[Yp (1, LY_KEY)] = 0;
        eng->reset(); std::vector<float> L, R;
        render (*eng, v, 29, 1.0f, 1.0f, L, R);
        const double hz = hzOf (L, 0.15f, 0.7f);
        std::printf ("Bass F1: %.2f Hz (Soll 43.65)\n", hz); check (std::fabs (hz - 43.65) < 0.3, "Stimmung");
    }

    // Jeder Effekt doppelt, im Master und in einer Ebene
    for (int type = 1; type < FX_TYPES; ++type)
        for (int pattern = 0; pattern < 3; ++pattern)
        {
            auto v = defaults(); v[G_MODE] = STAB; v[G_LIMIT] = 0;
            for (int s : { 0, layerSlot (0, 0) })
            {
                v[(size_t) slotParam (s, S_TYPE)] = (float) type; v[(size_t) slotParam (s, S_MIX)] = 1;
                for (int k = 0; k < FX_PARAMS; ++k)
                    v[(size_t) slotParam (s, S_P1 + k)] = pattern == 0 ? fxNorm (fxInfo (type)[k], fxInfo (type)[k].def) : pattern == 1 ? 1.0f : 0.0f;
            }
            eng->reset(); std::vector<float> L, R;
            render (*eng, v, 48, 0.8f, 0.3f, L, R);
            bool bad = false; for (size_t i = 0; i < L.size(); ++i) if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) bad = true;
            check (! bad && peak (L, 0, 0.8f) < 3000, fxNames()[(size_t) type].c_str());
        }

    // Modulation pro Stimme: Env auf Tune hebt jede Stimme einzeln, gezeichneter LFO bewegt den Pegel
    {
        Curves curves;
        for (auto& c : curves) for (int i = 0; i < LFO_POINTS; ++i) c[(size_t) i].store (i < LFO_POINTS / 2 ? 1.0f : 0.0f);
        std::array<std::atomic<float>, NUM_PARAMS> view; for (auto& x : view) x.store (0);
        eng->curves = &curves; eng->modView = &view;
        auto v = defaults(); v[G_MODE] = KICK; v[G_LIMIT] = 0;
        const auto Yp = [] (int l, int p) { return (size_t) modeParam (KICK, Y (l, p)); };
        v[Yp (1, LY_SOLO)] = 1; v[Yp (1, LY_PAMT)] = 0; v[Yp (1, LY_DRV)] = 0; v[Yp (1, LY_LEN)] = 2000; v[Yp (1, LY_LOWCUT)] = 20; v[Yp (1, LY_KEY)] = 0;
        v[(size_t) modSlot (0, MD_SOURCE)] = NUM_LFOS; v[(size_t) modSlot (0, MD_TARGET)] = (float) modeParam (KICK, Y (1, LY_TUNE)); v[(size_t) modSlot (0, MD_AMOUNT)] = 0.5f;
        v[(size_t) envParam (0, EV_DEC)] = 300;
        eng->reset(); std::vector<float> L, R;
        render (*eng, v, 45, 1.0f, 1.0f, L, R);
        const double e = hzOf (L, 0.002f, 0.05f), late = hzOf (L, 0.5f, 0.8f);
        std::printf ("Env auf Tune: frueh %.0f Hz, spaet %.0f Hz\n", e, late);
        check (e > late * 2 && std::fabs (late - 110) < 2, "Env Mod");

        v[(size_t) modSlot (0, MD_SOURCE)] = 0; v[(size_t) modSlot (0, MD_TARGET)] = (float) modeParam (KICK, Y (1, LY_LVL)); v[(size_t) modSlot (0, MD_AMOUNT)] = -1.0f;
        v[(size_t) lfoParam (0, LF_SYNC)] = 0; v[(size_t) lfoParam (0, LF_HZ)] = 4;
        eng->reset(); render (*eng, v, 45, 1.0f, 1.0f, L, R);
        const float a = peak (L, 0.02f, 0.1f), b = peak (L, 0.14f, 0.23f);
        std::printf ("Gezeichneter LFO auf Pegel: Rechteck-Teil %.3f, offen %.3f\n", a, b);
        check (a < b * 0.2f, "LFO Mod");
        eng->curves = nullptr; eng->modView = nullptr;
    }

    // Sample auf Ebene B, Ton aus Ebene A: beide klingen gemeinsam
    {
        SampleData sd; sd.rate = 44100; sd.l.resize (44100);
        for (int i = 0; i < 44100; ++i) sd.l[(size_t) i] = 0.6f * std::sin (2 * kPi * 1000.0f * (float) i / 44100.0f);
        std::atomic<const SampleData*> slots[NUM_MODES * NUM_LAYERS]; for (auto& x : slots) x.store (nullptr);
        slots[sampleSlot (MELO, 1)].store (&sd);
        eng->samples = slots;
        auto v = defaults(); v[G_MODE] = MELO; v[G_LIMIT] = 0;
        const auto M = [&] (int p) -> float& { return v[(size_t) modeParam (MELO, p)]; };
        M (M_LEVEL) = 0; M (M_VEL) = 0;
        for (auto& e : std::initializer_list<std::pair<int, float>> { { LY_TABLE, 0 }, { LY_UNI, 1 }, { LY_DRV, 0 }, { LY_FTYPE, 9 }, { LY_LOWCUT, 20 }, { LY_HOLD, 1 }, { LY_SUS, 1 }, { LY_FAMT, 0 } })
            M (Y (0, e.first)) = e.second;
        M (Y (1, LY_MUTE)) = 0; M (Y (1, LY_LVL)) = 0.8f; M (Y (1, LY_SRC)) = 1; M (Y (1, LY_PITCH)) = 0; M (Y (1, LY_FREE)) = 1;
        M (Y (1, LY_FTYPE)) = 9; M (Y (1, LY_LOWCUT)) = 20; M (Y (1, LY_DRV)) = 0;
        eng->reset(); std::vector<float> L, R;
        render (*eng, v, 57, 0.6f, 0.6f, L, R);
        const double a = tone (L, 220, 0.1f, 0.4f), c = tone (L, 1000, 0.1f, 0.4f);
        std::printf ("Ebenen: A 220 Hz %.2f, B Sample 1 kHz %.2f\n", a, c);
        check (std::fabs (a - 1.0) < 0.1 && std::fabs (c - 0.48) < 0.08, "Ebenen mischen");
        eng->samples = nullptr;
    }

    // Fertig rendern: Sample klingt nach dem Zuruecksetzen identisch
    for (int m : { KICK, STAB })
    {
        auto v = defaults(); v[G_MODE] = (float) m;
        v[(size_t) slotParam (0, S_TYPE)] = FX_DESTRUCTOR; v[(size_t) slotParam (0, S_MIX)] = 0.4f;
        for (int k = 0; k < FX_PARAMS; ++k) v[(size_t) slotParam (0, S_P1 + k)] = fxNorm (fxInfo (FX_DESTRUCTOR)[k], fxInfo (FX_DESTRUCTOR)[k].def);
        const int note = m == STAB ? 60 : 29;
        eng->reset(); std::vector<float> L, R;
        render (*eng, v, note, 1.2f, 0.6f, L, R);
        SampleData sd; sd.rate = SR; sd.l = L; sd.r = R;
        std::atomic<const SampleData*> slots[NUM_MODES * NUM_LAYERS]; for (auto& x : slots) x.store (&sd);
        eng->samples = slots;
        auto f = defaults();
        for (const auto& e : frozenValues (m, note)) f[(size_t) e.index] = e.value;
        eng->reset(); std::vector<float> L2, R2;
        render (*eng, f, note, 1.1f, 1.1f, L2, R2);
        double err = 0, ref = 0;
        for (size_t i = 0; i < L2.size(); ++i) { const double a = L2[i] - L[i], b = R2[i] - R[i]; err += a * a + b * b; ref += L[i] * L[i] + R[i] * R[i]; }
        const double db = 10 * std::log10 (err / ref + 1e-30);
        std::printf ("Render %-5s Abweichung %.1f dB\n", modeName (m), db); check (db < -80, "Render");
        eng->samples = nullptr;
    }

    // Sidechain in der Bass-Ebene
    {
        auto v = defaults(); v[G_MODE] = KICK; v[G_LIMIT] = 0;
        const auto Yp = [] (int l, int p) { return (size_t) modeParam (KICK, Y (l, p)); };
        v[Yp (1, LY_SOLO)] = 1; v[Yp (1, LY_PAMT)] = 0; v[Yp (1, LY_DRV)] = 0; v[Yp (1, LY_SUS)] = 1;
        const int s = layerSlot (1, 0);
        v[(size_t) slotParam (s, S_TYPE)] = FX_SIDECHAIN; v[(size_t) slotParam (s, S_MIX)] = 1;
        for (int k = 0; k < FX_PARAMS; ++k) v[(size_t) slotParam (s, S_P1 + k)] = fxNorm (fxInfo (FX_SIDECHAIN)[k], fxInfo (FX_SIDECHAIN)[k].def);
        eng->reset(); eng->noteOn (45, 1, v.data());
        std::vector<float> L ((size_t) SR, 0.0f), R ((size_t) SR, 0.0f);
        for (int i = 0; i + 256 <= (int) SR; i += 256) eng->process (L.data() + i, R.data() + i, 256, v.data(), 120, (double) i / SR * 2.0);
        const float duck = peak (L, 0.505f, 0.53f), open = peak (L, 0.72f, 0.75f);
        std::printf ("Sidechain: nach dem Beat %.3f, vor dem naechsten %.3f\n", duck, open);
        check (duck < open * 0.35f, "Sidechain");
    }

    // Analyse
    {
        SampleData d; d.rate = 44100; d.l.resize (22050);
        for (int i = 0; i < 22050; ++i) { const float t = (float) i / 44100.0f; d.l[(size_t) i] = 0.8f * std::exp (-t * 6.0f) * std::sin (2 * kPi * 110.0f * t); }
        const auto a = analyzeSample (d);
        const auto sg = suggestSettings (KICK, a, 1);
        std::printf ("Analyse: %.1f Hz, Bass-Ebene: spiele Note %d, %d Werte\n", a.hz, sg.playNote, (int) sg.items.size());
        check (std::fabs (a.hz - 110) < 1 && sg.playNote == 33, "Analyse");
        for (int m = 0; m < NUM_MODES; ++m)
            for (int l = 0; l < NUM_LAYERS; ++l)
                for (const auto& e : suggestSettings (m, a, l).items)
                    check (e.value >= params()[(size_t) e.index].min && e.value <= params()[(size_t) e.index].max, params()[(size_t) e.index].id.c_str());
    }
    std::printf (fails ? "FEHLER: %d\n" : "alles ok (%d Fehler)\n", fails);
    return fails != 0;
}
