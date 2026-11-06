// Tekkforge und Tekkdeck - fragt beim Oeffnen GitHub nach der neuesten Version und zeigt nur einen Hinweis.
// Es wird nichts automatisch heruntergeladen oder installiert. Ohne Internet bleibt der Hinweis einfach weg.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace tk
{
/** true, wenn "v1.2.0" neuer ist als "1.1.9". */
inline bool versionIsNewer (juce::String remote, juce::String local)
{
    const auto parts = [] (juce::String s)
    {
        auto a = juce::StringArray::fromTokens (s.trimCharactersAtStart ("vV"), ".", "");
        while (a.size() < 3) a.add ("0");
        return a;
    };
    const auto r = parts (remote), l = parts (local);
    for (int i = 0; i < 3; ++i)
    {
        if (r[i].getIntValue() > l[i].getIntValue()) return true;
        if (r[i].getIntValue() < l[i].getIntValue()) return false;
    }
    return false;
}

class UpdateChecker : private juce::Thread
{
public:
    UpdateChecker (juce::String ownerAndRepo, juce::String currentVersion)
        : juce::Thread ("Update-Pruefung"), repo (std::move (ownerAndRepo)), current (std::move (currentVersion))
    {
        startThread (juce::Thread::Priority::low);
    }
    ~UpdateChecker() override { stopThread (7000); }

    bool available() const { return ready.load(); }
    juce::String version() const { const juce::ScopedLock l (lock); return latest; }
    juce::String link() const { const juce::ScopedLock l (lock); return page; }

private:
    void run() override
    {
        int status = 0;
        auto stream = juce::URL ("https://api.github.com/repos/" + repo + "/releases/latest")
            .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs (5000)
                .withStatusCode (&status)
                .withExtraHeaders ("User-Agent: Tekk-Plugin\r\nAccept: application/vnd.github+json"));
        if (stream == nullptr || status != 200 || threadShouldExit()) return;
        const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
        const auto tag = json.getProperty ("tag_name", {}).toString();
        const auto url = json.getProperty ("html_url", {}).toString();
        // Nur eine echte GitHub-Seite oeffnen
        if (tag.isEmpty() || ! url.startsWith ("https://github.com/") || ! versionIsNewer (tag, current)) return;
        { const juce::ScopedLock l (lock); latest = tag.trimCharactersAtStart ("vV"); page = url; }
        ready.store (true);
    }
    juce::String repo, current, latest, page;
    juce::CriticalSection lock;
    std::atomic<bool> ready { false };
};

/** Hinweis im Fenster. Erst sichtbar, wenn eine neue Version gefunden wurde. Klick oeffnet die Download-Seite. */
class UpdateBanner : public juce::Component, private juce::Timer
{
public:
    UpdateBanner (juce::String ownerAndRepo, juce::String currentVersion) : checker (std::move (ownerAndRepo), std::move (currentVersion))
    {
        setVisible (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (1);
    }
    std::function<juce::Colour()> fill, text;

    void paint (juce::Graphics& g) override
    {
        g.setColour (fill != nullptr ? fill() : juce::Colours::orange);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (text != nullptr ? text() : juce::Colours::black);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawFittedText (juce::String::fromUTF8 ("Update ") + checker.version() + juce::String::fromUTF8 (" verfügbar"),
                          getLocalBounds().reduced (6, 0), juce::Justification::centred, 1, 0.8f);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasDraggedSinceMouseDown() && checker.available())
            juce::URL (checker.link()).launchInDefaultBrowser();
    }
private:
    void timerCallback() override
    {
        if (checker.available()) { stopTimer(); setVisible (true); repaint(); }
    }
    UpdateChecker checker;
};
} // namespace tk
