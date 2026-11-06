#include "PluginProcessor.h"
#include "PluginEditor.h"
using namespace tk;

static juce::String toJ (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

static juce::String prettyValue (float v, const juce::String& unit)
{
    const float a = std::fabs (v);
    if (unit == "Hz" && a >= 1000.0f) return juce::String (v / 1000.0f, 2) + " kHz";
    if (unit == "ms" && a >= 1000.0f) return juce::String (v / 1000.0f, 2) + " s";
    if (unit.isEmpty()) return juce::String (v, 2);
    return juce::String (v, a >= 100.0f ? 0 : 1) + " " + unit;
}

juce::AudioProcessorValueTreeState::ParameterLayout TekkforgeProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto& table = params();
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        const auto& d = table[(size_t) i];
        const juce::ParameterID id { toJ (d.id), 1 };
        if (d.kind == CHOICE)
        {
            juce::StringArray items;
            for (auto& c : d.choices) items.add (toJ (c));
            layout.add (std::make_unique<juce::AudioParameterChoice> (id, toJ (d.name), items, (int) d.def));
        }
        else if (d.kind == INT)
        {
            auto attr = juce::AudioParameterIntAttributes().withStringFromValueFunction ([this] (int v, int) { return targetName (v); });
            layout.add (std::make_unique<juce::AudioParameterInt> (id, toJ (d.name), (int) d.min, (int) d.max, (int) d.def, attr));
        }
        else
        {
            // FX-Regler zeigen in FL Studio den echten Namen und Wert des gerade geladenen Effekts
            const bool isFx = i >= slotParam (0, S_P1) && i < LANE_BASE && (i - SLOT_BASE) % S_COUNT >= S_P1;
            const int slot = (i - SLOT_BASE) / S_COUNT, k = (i - SLOT_BASE) % S_COUNT - S_P1;
            const juce::String unit = toJ (d.unit);
            auto attr = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([=] (float v, int)
            {
                if (isFx && raw[(size_t) slotParam (slot, S_TYPE)] != nullptr)
                {
                    const auto& p = fxInfo ((int) rawValue (slotParam (slot, S_TYPE)))[k];
                    if (p.label[0] == 0) return juce::String ("-");
                    const float real = fxReal (p, v);
                    if (p.items != nullptr)
                        return juce::StringArray::fromTokens (p.items, "|", "")[(int) (real - p.min)];
                    return juce::String::fromUTF8 (p.label) + " " + prettyValue (real, p.unit);
                }
                return prettyValue (v, unit);
            });
            layout.add (std::make_unique<juce::AudioParameterFloat> (id, toJ (d.name),
                juce::NormalisableRange<float> (d.min, d.max, 0.0f, d.skew), d.def, attr));
        }
    }
    return layout;
}

TekkforgeProcessor::TekkforgeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TEKKFORGE", makeLayout())
{
    const auto& table = params();
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        const auto id = toJ (table[(size_t) i].id);
        raw[(size_t) i] = apvts.getRawParameterValue (id);
        ptrs[(size_t) i] = apvts.getParameter (id);
    }
    for (auto& m : modView) m.store (0.0f);
    for (auto& s : scope) s.store (0.0f);
    for (int l = 0; l < NUM_LANES; ++l) shapeLane (l, 0);
    engine.curves = &curves;
    engine.modView = &modView;
    engine.samples = samples;
    for (auto& s : samples) s.store (nullptr);
    liveSamples.resize (NUM_MODES);
    formats.registerBasicFormats();
}
TekkforgeProcessor::~TekkforgeProcessor() { cancelPendingUpdate(); }

void TekkforgeProcessor::installSample (int mode, std::unique_ptr<SampleData> data)
{
    const auto now = juce::Time::getMillisecondCounter();
    oldSamples.erase (std::remove_if (oldSamples.begin(), oldSamples.end(), [now] (const Old& o) { return now - o.when > 5000; }), oldSamples.end());
    samples[mode].store (data.get(), std::memory_order_release);
    if (liveSamples[(size_t) mode] != nullptr) oldSamples.push_back ({ std::move (liveSamples[(size_t) mode]), now });
    liveSamples[(size_t) mode] = std::move (data);
    ++sampleVersion;
}
void TekkforgeProcessor::clearSample (int mode) { installSample (mode, nullptr); }

bool TekkforgeProcessor::loadSample (int mode, const juce::File& file, juce::String& message)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) { message = juce::String::fromUTF8 ("Diese Datei lässt sich nicht lesen. Nimm WAV, AIFF, FLAC oder OGG."); return false; }
    constexpr double maxSeconds = 30.0;
    const auto total = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * maxSeconds));
    juce::AudioBuffer<float> b (2, total);
    reader->read (&b, 0, total, 0, true, true);
    auto d = std::make_unique<SampleData>();
    d->rate = (float) reader->sampleRate;
    d->name = file.getFileName().toStdString();
    d->l.assign (b.getReadPointer (0), b.getReadPointer (0) + total);
    if (reader->numChannels > 1) d->r.assign (b.getReadPointer (1), b.getReadPointer (1) + total);
    installSample (mode, std::move (d));
    message = reader->lengthInSamples > total ? juce::String::fromUTF8 ("Nur die ersten 30 Sekunden wurden geladen.") : juce::String();
    return true;
}

void TekkforgeProcessor::connect (int source, int target)
{
    int free = -1;
    for (int i = NUM_MODS - 1; i >= 0; --i)
    {
        const int t = (int) rawValue (modSlot (i, MD_TARGET));
        if (t < 0) free = i;
        else if (t == target && (int) rawValue (modSlot (i, MD_SOURCE)) == source) return;
    }
    if (free < 0) return;
    const auto set = [] (juce::RangedAudioParameter* p, float real)
    { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (real)); p->endChangeGesture(); };
    set (param (modSlot (free, MD_SOURCE)), (float) source);
    set (param (modSlot (free, MD_AMOUNT)), source < NUM_LFOS ? 0.15f : 0.4f);
    set (param (modSlot (free, MD_TARGET)), (float) target);
}
void TekkforgeProcessor::disconnect (int slot)
{
    auto* p = param (modSlot (slot, MD_TARGET));
    p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (-1.0f)); p->endChangeGesture();
}

void TekkforgeProcessor::shapeLane (int lane, int shape)
{
    juce::Random r (lane * 77 + 5);
    for (int i = 0; i < LANE_POINTS; ++i)
    {
        const float x = (float) i / (float) (LANE_POINTS - 1);
        float y = 0;
        switch (shape)
        {
            case 0: y = 1.0f - x; break;                                   // Rampe runter
            case 1: y = x; break;                                          // Rampe hoch
            case 2: y = 1.0f - std::fabs (2.0f * x - 1.0f); break;         // Dreieck
            case 3: y = 0.5f - 0.5f * std::cos (2.0f * kPi * x); break;    // Sinus
            case 4: y = std::fmod (x * 4.0f, 1.0f) < 0.5f ? 1.0f : 0.0f; break; // Gate
            case 5: y = std::floor (x * 16.0f) == std::floor (((float) i - 1) / 63.0f * 16.0f) && i > 0
                          ? curves[(size_t) lane][(size_t) i - 1].load() : r.nextFloat(); break;  // Zufall in Stufen
            default: y = 0; break;                                         // Leer
        }
        curves[(size_t) lane][(size_t) i].store (y);
    }
}

juce::String TekkforgeProcessor::targetName (int index) const
{
    if (index < 0 || index >= NUM_PARAMS) return juce::String::fromUTF8 ("Kein Ziel");
    if (index >= SLOT_BASE && index < LANE_BASE)
    {
        const int slot = (index - SLOT_BASE) / S_COUNT, k = (index - SLOT_BASE) % S_COUNT;
        const int type = raw[(size_t) slotParam (slot, S_TYPE)] != nullptr ? (int) rawValue (slotParam (slot, S_TYPE)) : 0;
        const juce::String head = "FX" + juce::String (slot + 1) + " " + toJ (fxNames()[(size_t) type]) + " ";
        if (k == S_MIX) return head + "Mix";
        if (k >= S_P1) return head + juce::String::fromUTF8 (fxInfo (type)[k - S_P1].label);
    }
    return toJ (params()[(size_t) index].name);
}

void TekkforgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    baseRate = sampleRate; maxBlock = samplesPerBlock;
    for (int q = 1; q < 3; ++q)
    {
        os[q] = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) q, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        os[q]->initProcessing ((size_t) samplesPerBlock);
    }
    work.setSize (2, samplesPerBlock);
    engine.allocate ((float) sampleRate * 4.0f);
    quality = -1;
    applyQuality ((int) rawValue (G_QUALITY));
}

void TekkforgeProcessor::applyQuality (int q)
{
    if (q == quality) return;
    quality = q;
    engine.setSampleRate ((float) baseRate * (float) (1 << q));
    if (os[1] != nullptr) { os[1]->reset(); os[2]->reset(); }
    triggerAsyncUpdate();
}
void TekkforgeProcessor::handleAsyncUpdate()
{
    const int q = quality;
    setLatencySamples (q > 0 && os[q] != nullptr ? (int) std::round (os[q]->getLatencyInSamples()) : 0);
}

void TekkforgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n > maxBlock) return;   // JUCE-Hosts halten die zugesagte Blockgroesse ein

    snapshot (values);
    applyQuality ((int) values[G_QUALITY]);
    keyboard.processNextMidiBuffer (midi, 0, n, true);

    double bpm = 170.0, ppq = -1.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (pos->getIsPlaying()) if (auto p = pos->getPpqPosition()) ppq = *p;
        }

    lastBpm.store (bpm, std::memory_order_relaxed);
    const int factor = 1 << quality;
    float* L; float* R;
    juce::dsp::AudioBlock<float> outBlock (buffer);
    juce::dsp::AudioBlock<float> up = outBlock;
    if (quality > 0) up = os[quality]->processSamplesUp (outBlock);
    L = up.getChannelPointer (0); R = up.getChannelPointer (1);

    int done = 0;
    const auto render = [&] (int until)
    {
        if (until <= done) return;
        const double p = ppq < 0 ? -1.0 : ppq + (double) done / baseRate * bpm / 60.0;
        engine.process (L + done * factor, R + done * factor, (until - done) * factor, values, bpm, p);
        done = until;
    };
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        render (juce::jlimit (0, n, m.samplePosition));
        if (msg.isNoteOn()) { engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity(), values); lastNote.store (msg.getNoteNumber()); }
        else if (msg.isNoteOff()) engine.noteOff (msg.getNoteNumber(), values);
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) engine.allNotesOff();
    }
    render (n);
    if (quality > 0) os[quality]->processSamplesDown (outBlock);

    auto* oL = buffer.getReadPointer (0); auto* oR = buffer.getReadPointer (1);
    float pkL = 0, pkR = 0; int w = scopeWrite.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        pkL = std::max (pkL, std::fabs (oL[i])); pkR = std::max (pkR, std::fabs (oR[i]));
        scope[(size_t) w].store ((oL[i] + oR[i]) * 0.5f, std::memory_order_relaxed);
        w = (w + 1) % SCOPE;
    }
    scopeWrite.store (w, std::memory_order_release);
    meterL.store (std::max (pkL, meterL.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    meterR.store (std::max (pkR, meterR.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
}

std::unique_ptr<juce::XmlElement> TekkforgeProcessor::stateToXml()
{
    auto state = apvts.copyState();
    for (const char* n : { "CURVES", "SAMPLES" }) state.removeChild (state.getChildWithName (n), nullptr);
    juce::ValueTree c ("CURVES");
    for (int l = 0; l < NUM_LANES; ++l)
    {
        juce::StringArray s;
        for (int i = 0; i < LANE_POINTS; ++i) s.add (juce::String (curves[(size_t) l][(size_t) i].load(), 4));
        c.setProperty ("l" + juce::String (l), s.joinIntoString (","), nullptr);
    }
    state.addChild (c, -1, nullptr);

    // Samples wandern mit in Projekt und Preset, die Originaldatei wird danach nicht mehr gebraucht
    juce::ValueTree smp ("SAMPLES");
    for (int m = 0; m < NUM_MODES; ++m)
        if (const auto* d = liveSamples[(size_t) m].get())
        {
            juce::ValueTree e ("S");
            e.setProperty ("mode", m, nullptr); e.setProperty ("rate", d->rate, nullptr);
            e.setProperty ("name", juce::String::fromUTF8 (d->name.c_str()), nullptr);
            e.setProperty ("l", juce::MemoryBlock (d->l.data(), d->l.size() * sizeof (float)).toBase64Encoding(), nullptr);
            if (! d->r.empty()) e.setProperty ("r", juce::MemoryBlock (d->r.data(), d->r.size() * sizeof (float)).toBase64Encoding(), nullptr);
            smp.addChild (e, -1, nullptr);
        }
    state.addChild (smp, -1, nullptr);
    return state.createXml();
}

void TekkforgeProcessor::stateFromXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (xml);
    if (auto c = state.getChildWithName ("CURVES"); c.isValid())
    {
        for (int l = 0; l < NUM_LANES; ++l)
        {
            auto s = juce::StringArray::fromTokens (c.getProperty ("l" + juce::String (l)).toString(), ",", "");
            for (int i = 0; i < juce::jmin (LANE_POINTS, s.size()); ++i)
                curves[(size_t) l][(size_t) i].store (s[i].getFloatValue());
        }
        state.removeChild (c, nullptr);
    }
    for (int m = 0; m < NUM_MODES; ++m) if (liveSamples[(size_t) m] != nullptr) clearSample (m);
    if (auto smp = state.getChildWithName ("SAMPLES"); smp.isValid())
    {
        for (auto e : smp)
        {
            auto d = std::make_unique<SampleData>();
            d->rate = (float) e.getProperty ("rate");
            d->name = e.getProperty ("name").toString().toStdString();
            const auto read = [] (const juce::var& v, std::vector<float>& out)
            {
                juce::MemoryBlock mb;
                if (! mb.fromBase64Encoding (v.toString())) return;
                out.resize (mb.getSize() / sizeof (float));
                std::memcpy (out.data(), mb.getData(), out.size() * sizeof (float));
            };
            read (e.getProperty ("l"), d->l);
            if (e.hasProperty ("r")) read (e.getProperty ("r"), d->r);
            installSample (juce::jlimit (0, NUM_MODES - 1, (int) e.getProperty ("mode")), std::move (d));
        }
        state.removeChild (smp, nullptr);
    }
    apvts.replaceState (state);
}

void TekkforgeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = stateToXml()) copyXmlToBinary (*xml, dest);
}
void TekkforgeProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size)) stateFromXml (*xml);
}

juce::File TekkforgeProcessor::presetFolder() const
{
    auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Tekkforge").getChildFile ("Presets");
    f.createDirectory();
    return f;
}
juce::Array<juce::File> TekkforgeProcessor::listPresets() const
{
    auto a = presetFolder().findChildFiles (juce::File::findFiles, false, "*.tkf");
    a.sort();
    return a;
}
bool TekkforgeProcessor::savePresetFile (const juce::File& file)
{
    auto xml = stateToXml();
    if (xml == nullptr) return false;
    juce::TemporaryFile tmp (file);
    {
        juce::FileOutputStream raw (tmp.getFile());
        if (raw.failedToOpen()) return false;
        juce::GZIPCompressorOutputStream gz (raw);
        gz.writeText (xml->toString(), false, false, nullptr);
    }
    if (! tmp.overwriteTargetFileWithTemporary()) return false;
    currentPreset = file.getFileNameWithoutExtension();
    return true;
}
bool TekkforgeProcessor::loadPresetFile (const juce::File& file)
{
    juce::FileInputStream raw (file);
    if (raw.failedToOpen()) return false;
    juce::GZIPDecompressorInputStream gz (raw);
    auto xml = juce::parseXML (gz.readEntireStreamAsString());
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;
    stateFromXml (*xml);
    currentPreset = file.getFileNameWithoutExtension();
    return true;
}
std::unique_ptr<SampleData> TekkforgeProcessor::renderSound (int note, juce::String& message)
{
    float v[NUM_PARAMS]; snapshot (v);
    const int mode = (int) v[G_MODE], q = (int) v[G_QUALITY], factor = 1 << q;
    const float* m = v + modeParam (mode, 0);
    const double rate = baseRate;
    constexpr int block = 512;
    constexpr double maxSeconds = 20.0;

    auto eng = std::make_unique<Engine>();
    eng->allocate ((float) rate * (float) factor); eng->setSampleRate ((float) rate * (float) factor);
    eng->curves = &curves; eng->samples = samples; eng->reset();

    std::unique_ptr<juce::dsp::Oversampling<float>> over;
    if (q > 0)
    {
        over = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) q, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        over->initProcessing ((size_t) block); over->reset();
    }

    // Die Stille muss laenger sein als jede Pause zwischen zwei Delay-Echos
    double silenceNeeded = 0.3;
    for (int s = 0; s < NUM_SLOTS; ++s)
        if ((int) v[slotParam (s, S_TYPE)] == FX_DELAY && v[slotParam (s, S_ON)] > 0.5f)
            silenceNeeded = juce::jmax (silenceNeeded, fxReal (fxInfo (FX_DELAY)[0], v[slotParam (s, S_P1)]) * 0.001 + 0.1);

    double noteLen = (double) (m[M_ATT] + m[M_HOLD] + m[M_DEC]) * 0.001;
    if (m[M_GATE] > 0.5f && m[M_SUS] > 0.0f) noteLen += 0.5;

    auto out = std::make_unique<SampleData>();
    out->rate = (float) rate;
    juce::AudioBuffer<float> buf (2, block);
    const auto bpm = lastBpm.load();
    const int latency = over != nullptr ? (int) std::round (over->getLatencyInSamples()) : 0;
    const int maxSamples = (int) (maxSeconds * rate), silenceTarget = (int) (silenceNeeded * rate);
    int done = 0, quiet = 0, lastLoud = 0;
    bool off = false;
    float peak = 0;
    eng->noteOn (note, 1.0f, v);
    while (done < maxSamples && (quiet < silenceTarget || ! off || eng->activeVoices() > 0))
    {
        if (! off && (double) done / rate >= noteLen) { eng->noteOff (note, v); off = true; }
        buf.clear();
        juce::dsp::AudioBlock<float> ab (buf), up = ab;
        if (over != nullptr) up = over->processSamplesUp (ab);
        eng->process (up.getChannelPointer (0), up.getChannelPointer (1), block * factor, v, bpm, -1.0);
        if (over != nullptr) over->processSamplesDown (ab);
        const auto* L = buf.getReadPointer (0); const auto* R = buf.getReadPointer (1);
        for (int i = 0; i < block; ++i)
        {
            const float a = juce::jmax (std::fabs (L[i]), std::fabs (R[i]));
            peak = juce::jmax (peak, a);
            if (a > 0.0002f) { lastLoud = done + i; quiet = 0; } else ++quiet;
        }
        out->l.insert (out->l.end(), L, L + block);
        out->r.insert (out->r.end(), R, R + block);
        done += block;
    }
    if (peak < 0.0002f) { message = juce::String::fromUTF8 ("Der Sound ist stumm, deshalb wurde nichts geändert."); return nullptr; }

    // Latenz vorn und Stille hinten abschneiden, Ende weich ausblenden
    const int end = juce::jmin ((int) out->l.size(), lastLoud + (int) (0.01 * rate));
    out->l.erase (out->l.begin() + end, out->l.end()); out->r.erase (out->r.begin() + end, out->r.end());
    out->l.erase (out->l.begin(), out->l.begin() + latency); out->r.erase (out->r.begin(), out->r.begin() + latency);
    const int fade = juce::jmin ((int) out->l.size(), (int) (0.005 * rate));
    for (int i = 0; i < fade; ++i)
    {
        const float g = (float) i / (float) fade;
        out->l[out->l.size() - 1 - (size_t) i] *= g; out->r[out->r.size() - 1 - (size_t) i] *= g;
    }
    bool mono = true;
    for (size_t i = 0; i < out->l.size() && mono; ++i) mono = std::fabs (out->l[i] - out->r[i]) < 1.0e-6f;
    if (mono) { out->r.clear(); out->r.shrink_to_fit(); }

    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    out->name = std::string (modeName (mode)) + " gerendert " + n[note % 12] + std::to_string (note / 12);
    return out;
}

void TekkforgeProcessor::applyRendered (std::unique_ptr<SampleData> data, int mode, int note)
{
    resetToDefaults();
    installSample (mode, std::move (data));
    for (const auto& f : frozenValues (mode, note))
    {
        auto* p = param (f.index);
        p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (f.value)); p->endChangeGesture();
    }
}

void TekkforgeProcessor::resetToDefaults()
{
    for (int i = 0; i < NUM_PARAMS; ++i) { auto* p = param (i); p->setValueNotifyingHost (p->getDefaultValue()); }
    for (int m = 0; m < NUM_MODES; ++m) clearSample (m);
    for (int l = 0; l < NUM_LANES; ++l) shapeLane (l, 0);
    currentPreset = {};
}

juce::AudioProcessorEditor* TekkforgeProcessor::createEditor() { return new TekkforgeEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TekkforgeProcessor(); }
