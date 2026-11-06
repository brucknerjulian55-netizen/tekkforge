#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/Engine.h"

class TekkforgeProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    TekkforgeProcessor();
    ~TekkforgeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto in = l.getChannelSet (true, 0);
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && (in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Tekkforge"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Tekkforge"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboard;
    std::array<std::array<std::atomic<float>, tk::LANE_POINTS>, tk::NUM_LANES> curves;
    std::array<std::atomic<float>, tk::NUM_PARAMS> modView;
    tk::Engine engine;

    // Daten fuer die Anzeigen
    static constexpr int SCOPE = 8192;
    std::array<std::atomic<float>, SCOPE> scope;
    std::atomic<int> scopeWrite { 0 };
    std::atomic<float> meterL { 0 }, meterR { 0 };
    std::atomic<int> lastNote { -1 };
    double currentSampleRate() const { return baseRate; }

    juce::RangedAudioParameter* param (int i) const { return ptrs[(size_t) i]; }
    float rawValue (int i) const { return raw[(size_t) i]->load (std::memory_order_relaxed); }
    void snapshot (float* out) const { for (int i = 0; i < tk::NUM_PARAMS; ++i) out[i] = rawValue (i); }
    void shapeLane (int lane, int shape);

    // Samples: je Modus und Oszillator; werden beim Speichern eingebettet
    std::atomic<const tk::SampleData*> samples[tk::NUM_MODES * tk::NUM_OSCS];
    std::atomic<int> sampleVersion { 0 };
    bool loadSample (int mode, int osc, const juce::File&, juce::String& message);
    void clearSample (int mode, int osc);

    // Presets
    juce::File presetFolder() const;
    juce::Array<juce::File> listPresets() const;
    bool savePresetFile (const juce::File&);
    bool loadPresetFile (const juce::File&);
    void resetToDefaults();

    /** Rechnet den aktuellen Sound komplett in ein Sample. Aendert noch nichts am Plugin. */
    std::unique_ptr<tk::SampleData> renderSound (int note, juce::String& message);
    /** Setzt alles auf Standard und laesst nur das gerenderte Sample im aktuellen Modus uebrig. */
    void applyRendered (std::unique_ptr<tk::SampleData>, int mode, int note);
    std::atomic<double> lastBpm { 170.0 };
    juce::String currentPreset;

    // gezogene Modulationen
    void connect (int source, int target);
    void disconnect (int slot);
    juce::String targetName (int index) const;

private:
    void handleAsyncUpdate() override;
    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void applyQuality (int q);
    void installSample (int slot, std::unique_ptr<tk::SampleData>);
    std::unique_ptr<juce::XmlElement> stateToXml();
    void stateFromXml (const juce::XmlElement&);

    juce::AudioFormatManager formats;
    std::vector<std::unique_ptr<tk::SampleData>> liveSamples;     // aktuell benutzte Samples
    struct Old { std::unique_ptr<tk::SampleData> data; juce::uint32 when; };
    std::vector<Old> oldSamples;                                  // erst spaeter loeschen, der Audio-Thread koennte noch lesen

    std::array<std::atomic<float>*, tk::NUM_PARAMS> raw {};
    std::array<juce::RangedAudioParameter*, tk::NUM_PARAMS> ptrs {};
    std::unique_ptr<juce::dsp::Oversampling<float>> os[3];
    juce::AudioBuffer<float> work;
    float values[tk::NUM_PARAMS] {};
    double baseRate = 44100;
    int quality = -1, maxBlock = 512;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TekkforgeProcessor)
};
