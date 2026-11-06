#pragma once
#include "PluginProcessor.h"
#include "ui/UpdateCheck.h"

class TopBar;
class TkLook;

class TekkforgeEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer,
                        public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit TekkforgeEditor (TekkforgeProcessor&);
    ~TekkforgeEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    void showPage (int page);
    int getPage() const { return pageIndex; }
    TekkforgeProcessor& proc;

    /** Lane, die gerade auf einen Knob-Klick wartet (-1 = keine). */
    int learnLane = -1;
    int selectedSlot = 0;
    void rebuild();
    void requestRebuild();
    void tickNow() { timerCallback(); }

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int, int) override;
    void loadSampleFile (const juce::File&);
    void chooseSample();
    void chooseExport();
    void chooseImport();
    void askRenderSound();
    juce::String status;     // Hinweistext auf den Seiten Sample und Presets
    int specMode = 0, sampleView = 0, guideView = 0, recipe = 0;   // gemerkte Ansichten

private:
    void timerCallback() override;

    std::unique_ptr<TkLook> look;
    std::unique_ptr<TopBar> top;
    std::unique_ptr<juce::Component> page, sources;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::MidiKeyboardComponent> keys;
    std::unique_ptr<tk::UpdateBanner> update;
    struct Preview;
    std::unique_ptr<struct Ed> ctx;
    float lastValues[tk::NUM_PARAMS] {};
    int lastPreviewNote = -1;
    std::unique_ptr<Preview> preview;
    int pageIndex = 0, lastMode = -1, lastSample = -1, lastTheme = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TekkforgeEditor)
};
