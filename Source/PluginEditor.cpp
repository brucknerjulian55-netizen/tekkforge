#include "PluginEditor.h"
#include "ui/GuideData.h"
#include "ui/Themes.h"
#include "dsp/SampleAnalysis.h"
#include <mutex>
using namespace tk;
using juce::Colour; using juce::Graphics; using juce::Rectangle; using juce::String; using juce::Path;

// ---------------- Farben und Schrift ---------------------------------------
namespace theme
{
    inline Colour plate, plateHi, well, line, ink, dim, hot;
    inline Colour accents[5];
    inline int current = -1;                      // gilt fuer alle offenen Instanzen
    inline juce::File choiceFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Tekkforge").getChildFile ("design.txt");
    }
    inline void apply (int i)
    {
        i = juce::jlimit (0, NUM_THEMES - 1, i);
        const auto& t = themeDef (i);
        plate = Colour (t.plate); plateHi = Colour (t.plateHi); well = Colour (t.well); line = Colour (t.line);
        ink = Colour (t.ink); dim = Colour (t.dim); hot = Colour (t.hot);
        for (int a = 0; a < 5; ++a) accents[a] = Colour (t.accent[a]);
        current = i;
    }
    inline void saveChoice() { choiceFile().getParentDirectory().createDirectory(); choiceFile().replaceWithText (juce::String (current)); }
    inline void loadChoice() { if (current < 0) apply (choiceFile().loadFileAsString().getIntValue()); }
    inline Colour accent (int mode) { static const int map[3] = { 0, 1, 4 }; return accents[map[juce::jlimit (0, 2, mode)]]; }
    inline juce::Font font (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }
}
static String U (const char* s) { return String::fromUTF8 (s); }
static String U (const std::string& s) { return String::fromUTF8 (s.c_str()); }

static String noteName (int note)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return String (n[note % 12]) + String (note / 12);     // Schreibweise wie in FL Studio: Note 60 = C5
}

struct Ed { TekkforgeEditor& e; TekkforgeProcessor& p; int mode; Colour acc; };

class TkLook : public juce::LookAndFeel_V4
{
public:
    TkLook() { setDefaultSansSerifTypefaceName ("Bahnschrift"); }
    Colour accent = theme::accent (2);

    void drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const auto b = Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float ang = a0 + pos * (a1 - a0);
        const auto arc = [&] (float from, float to, float rad, float t, Colour col)
        {
            Path p; p.addCentredArc (c.x, c.y, rad, rad, 0, from, to, true);
            g.setColour (col); g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        arc (a0, a1, r - 2, 4.0f, theme::well);
        arc (a0, ang, r - 2, 4.0f, accent);
        g.setColour (theme::plateHi); g.fillEllipse (c.x - r + 7, c.y - r + 7, (r - 7) * 2, (r - 7) * 2);
        Path tick; tick.addRoundedRectangle (-1.5f, -(r - 8), 3.0f, r * 0.42f, 1.5f);
        g.setColour (theme::ink); g.fillPath (tick, juce::AffineTransform::rotation (ang).translated (c));

        const float range = (float) s.getProperties().getWithDefault ("range", -1.0f);
        if (range >= 0.0f) arc (ang, a0 + range * (a1 - a0), r - 2, 4.0f, theme::ink.withAlpha (0.45f));
        const float mod = (float) s.getProperties().getWithDefault ("mod", -1.0f);
        if (mod >= 0.0f) { const float m = a0 + mod * (a1 - a0); g.setColour (theme::ink); g.fillEllipse (c.x + (r - 2) * std::sin (m) - 2.5f, c.y - (r - 2) * std::cos (m) - 2.5f, 5.0f, 5.0f); }
    }
};

// Wertzugriff mit Gesten, damit FL Studio Bewegungen als Automation aufnehmen kann
struct PRef
{
    juce::RangedAudioParameter* p = nullptr;
    float norm() const { return p->getValue(); }
    float real() const { return p->convertFrom0to1 (p->getValue()); }
    void setNorm (float n) { p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, n)); }
    void setReal (float v) { setNorm (p->convertTo0to1 (v)); }
    void begin() { p->beginChangeGesture(); }
    void end() { p->endChangeGesture(); }
};

// ---------------- Grundbausteine -------------------------------------------
class Ticking : public juce::Component
{
public:
    virtual void tick() { for (auto* c : getChildren()) if (auto* t = dynamic_cast<Ticking*> (c)) t->tick(); }
};

/** Kleiner Punkt am Regler fuer eine Verbindung: Ziehen stellt die Staerke ein, Rechtsklick entfernt sie. */
class ModDot : public juce::Component
{
public:
    ModDot (Ed& ed, int slotIndex, std::function<void (const String&)> show) : d (ed), slot (slotIndex), report (std::move (show)) {}
    PRef amount() const { return { d.p.param (modSlot (slot, MD_AMOUNT)) }; }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (d.acc); g.fillEllipse (b);
        g.setColour (theme::ink); g.drawEllipse (b, 1.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { d.p.disconnect (slot); return; }
        amount().begin(); start = amount().norm(); mouseDrag (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        amount().setNorm (start - (float) e.getDistanceFromDragStartY() / 160.0f);
        report (U (sourceName ((int) d.p.rawValue (modSlot (slot, MD_SOURCE)))) + " " + String (amount().real(), 2));
    }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) { amount().end(); report ({}); } }
    juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::UpDownResizeCursor; }
private:
    Ed& d; int slot; float start = 0; std::function<void (const String&)> report;
};

class Knob : public Ticking, public juce::DragAndDropTarget
{
public:
    Knob (Ed& ed, int index, String title) : d (ed), idx (index), name (std::move (title))
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRotaryParameters (juce::degreesToRadians (-135.0f) + juce::MathConstants<float>::twoPi,
                               juce::degreesToRadians (135.0f) + juce::MathConstants<float>::twoPi, true);
        s.setMouseDragSensitivity (160);
        addAndMakeVisible (s);
        auto* p = d.p.param (idx);
        att = std::make_unique<juce::SliderParameterAttachment> (*p, s, nullptr);
        s.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }
    void resized() override
    {
        auto b = getLocalBounds().removeFromTop (getHeight() - 30); const int z = juce::jmin (46, b.getHeight());
        s.setBounds (b.withSizeKeepingCentre (z, z));
        layoutDots();
    }
    void layoutDots()
    {
        for (size_t i = 0; i < dots.size(); ++i) dots[i]->setBounds (getWidth() - 12, 1 + (int) i * 12, 11, 11);
    }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().removeFromBottom (30);
        g.setColour (theme::dim); g.setFont (theme::font (12.0f));
        g.drawFittedText (name, b.removeFromTop (14), juce::Justification::centred, 1, 0.7f);
        g.setColour (override_.isNotEmpty() ? d.acc : theme::ink); g.setFont (theme::font (12.0f));
        g.drawFittedText (override_.isNotEmpty() ? override_ : shown, b, juce::Justification::centred, 1, 0.7f);
    }
    void tick() override
    {
        auto* p = d.p.param (idx);
        const String raw = p->getCurrentValueAsText(), tail = raw.fromFirstOccurrenceOf (name + " ", false, false);
        const String v = tail.isNotEmpty() ? tail : raw;
        if (v != shown) { shown = v; repaint(); }

        // Verbindungen dieses Reglers
        std::vector<int> now; float sum = 0;
        for (int m = 0; m < NUM_MODS; ++m)
            if ((int) d.p.rawValue (modSlot (m, MD_TARGET)) == idx) { now.push_back (m); sum += d.p.rawValue (modSlot (m, MD_AMOUNT)); }
        if (now != slots)
        {
            slots = now; dots.clear();
            for (int m : slots)
            {
                auto dot = std::make_unique<ModDot> (d, m, [this] (const String& t) { override_ = t; repaint(); });
                addAndMakeVisible (*dot); dots.push_back (std::move (dot));
            }
            layoutDots();
        }
        const float range = slots.empty() ? -1.0f : juce::jlimit (0.0f, 1.0f, p->getValue() + sum);
        const float mod = slots.empty() ? -1.0f : d.p.modView[(size_t) idx].load (std::memory_order_relaxed);
        if (std::fabs (range - lastRange) > 0.002f || std::fabs (mod - lastMod) > 0.002f)
        {
            lastRange = range; lastMod = mod;
            s.getProperties().set ("range", range); s.getProperties().set ("mod", mod); s.repaint();
        }
    }
    bool isInterestedInDragSource (const SourceDetails& e) override { return e.description.toString().startsWith ("tkmod:"); }
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails& e) override
    {
        dropHover = false; repaint();
        d.p.connect (e.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue(), idx);
    }
    void paintOverChildren (Graphics& g) override
    {
        if (dropHover) { g.setColour (d.acc); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 2.0f); }
    }
    juce::Slider s;
private:
    Ed& d; int idx; String name, shown, override_; float lastMod = -2.0f, lastRange = -2.0f; bool dropHover = false;
    std::unique_ptr<juce::SliderParameterAttachment> att;
    std::vector<int> slots; std::vector<std::unique_ptr<ModDot>> dots;
};

/** Knopfleiste fuer Auswahl-Parameter, zeigt alle Optionen gleichzeitig. */
class Strip : public Ticking
{
public:
    Strip (Ed& ed, int index, int cols = 0) : d (ed), ref { ed.p.param (index) }, columns (cols)
    {
        auto* c = dynamic_cast<juce::AudioParameterChoice*> (ref.p);
        items = c->choices;
    }
    int cell (juce::Point<int> pt) const
    {
        const int cols = columns > 0 ? columns : items.size(), rows = (items.size() + cols - 1) / cols;
        const int cx = juce::jlimit (0, cols - 1, pt.x * cols / juce::jmax (1, getWidth()));
        const int cy = juce::jlimit (0, rows - 1, pt.y * rows / juce::jmax (1, getHeight()));
        return juce::jmin (items.size() - 1, cy * cols + cx);
    }
    void paint (Graphics& g) override
    {
        const int cols = columns > 0 ? columns : items.size(), rows = (items.size() + cols - 1) / cols;
        const float w = (float) getWidth() / (float) cols, h = (float) getHeight() / (float) rows;
        const int cur = (int) ref.real();
        for (int i = 0; i < items.size(); ++i)
        {
            Rectangle<float> r ((float) (i % cols) * w, (float) (i / cols) * h, w, h); r.reduce (1.5f, 1.5f);
            g.setColour (i == cur ? d.acc : theme::well); g.fillRoundedRectangle (r, 4.0f);
            g.setColour (i == cur ? theme::well : theme::ink); g.setFont (theme::font (12.5f, i == cur));
            g.drawFittedText (items[i], r.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.75f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        ref.begin(); ref.setReal ((float) cell (e.getPosition())); ref.end(); repaint();
    }
    void tick() override { const int v = (int) ref.real(); if (v != last) { last = v; repaint(); } }
private:
    Ed& d; PRef ref; int columns, last = -1; juce::StringArray items;
};

/** Auswahl mit vielen Eintraegen: Klick oeffnet die Liste, Pfeile und Mausrad schalten durch. */
class ChoiceBtn : public Ticking
{
public:
    ChoiceBtn (Ed& ed, int index, std::function<String (int)> groupOf = {}) : d (ed), ref { ed.p.param (index) }, group (std::move (groupOf))
    {
        items = dynamic_cast<juce::AudioParameterChoice*> (ref.p)->choices;
    }
    void step (int by) { const int n = juce::jlimit (0, items.size() - 1, (int) ref.real() + by); ref.begin(); ref.setReal ((float) n); ref.end(); repaint(); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::well); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::dim); g.setFont (theme::font (13.0f, true));
        g.drawText ("<", r.removeFromLeft (16.0f), juce::Justification::centred, false);
        g.drawText (">", r.removeFromRight (16.0f), juce::Justification::centred, false);
        g.setColour (d.acc); g.setFont (theme::font (12.5f, true));
        g.drawFittedText (items[(int) ref.real()], r.toNearestInt(), juce::Justification::centred, 1, 0.75f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x < 17) { step (-1); return; }
        if (e.x > getWidth() - 17) { step (1); return; }
        const int cur = (int) ref.real();
        juce::PopupMenu menu;
        if (group)
        {
            String open; juce::PopupMenu sub;
            for (int i = 0; i < items.size(); ++i)
            {
                if (group (i) != open) { if (open.isNotEmpty()) menu.addSubMenu (open, sub); sub = {}; open = group (i); }
                sub.addItem (i + 1, items[i], true, i == cur);
            }
            menu.addSubMenu (open, sub);
        }
        else for (int i = 0; i < items.size(); ++i) menu.addItem (i + 1, items[i], true, i == cur);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [sp = SafePointer<ChoiceBtn> (this)] (int r) { if (sp != nullptr && r > 0) { sp->ref.begin(); sp->ref.setReal ((float) (r - 1)); sp->ref.end(); sp->repaint(); } });
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { if (w.deltaY != 0) step (w.deltaY > 0 ? -1 : 1); }
    void tick() override { const int v = (int) ref.real(); if (v != last) { last = v; repaint(); } }
private:
    Ed& d; PRef ref; std::function<String (int)> group; juce::StringArray items; int last = -1;
};

/** Laedt einen fertigen Charakter in die Ebene. */
class PatchBtn : public Ticking
{
public:
    PatchBtn (Ed& ed, int layerIndex) : d (ed), l (layerIndex) {}
    const std::vector<Patch>& list() const { return d.mode == KICK && l == 0 ? topPatches() : bassPatches(); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (d.acc); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::well); g.setFont (theme::font (12.5f, true));
        g.drawFittedText (U ("Charakter laden"), r.toNearestInt(), juce::Justification::centred, 1, 0.75f);
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu menu; const auto& p = list();
        for (int i = 0; i < (int) p.size(); ++i) menu.addItem (i + 1, U (p[(size_t) i].name));
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [sp = SafePointer<PatchBtn> (this)] (int r)
        {
            if (sp == nullptr || r <= 0) return;
            for (const auto& e : patchValues (sp->list()[(size_t) r - 1]))
            {
                PRef x { sp->d.p.param (modeParam (sp->d.mode, Y (sp->l, e.first))) };
                x.begin(); x.setReal (e.second); x.end();
            }
        });
    }
private:
    Ed& d; int l;
};

/** Anzeigefenster: wiederholt nur, wenn sich ein beobachteter Wert geaendert hat. */
class Disp : public Ticking
{
public:
    Disp (Ed& ed, std::vector<int> watch) : d (ed), watched (std::move (watch)) {}
    void tick() override
    {
        float sum = 0; for (int i = 0; i < (int) watched.size(); ++i) sum += d.p.param (watched[(size_t) i])->getValue() * (float) (i + 1);
        if (sum != lastSum) { lastSum = sum; repaint(); }
    }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (theme::well); g.fillRoundedRectangle (b, 6.0f);
        { Graphics::ScopedSaveState ss (g); g.reduceClipRegion (getLocalBounds().reduced (1)); draw (g, b.reduced (6.0f)); }
    }
    virtual void draw (Graphics&, Rectangle<float>) = 0;
protected:
    float val (int mp) const { return d.p.rawValue (modeParam (d.mode, mp)); }
    PRef ref (int mp) const { return { d.p.param (modeParam (d.mode, mp)) }; }
    void stroke (Graphics& g, const Path& p, float t = 2.0f) const
    {
        g.setColour (d.acc); g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void fillUnder (Graphics& g, Path p, Rectangle<float> b) const
    {
        p.lineTo (b.getRight(), b.getBottom()); p.lineTo (b.getX(), b.getBottom()); p.closeSubPath();
        g.setColour (d.acc.withAlpha (0.16f)); g.fillPath (p);
    }
    void dot (Graphics& g, float x, float y) const
    {
        g.setColour (theme::ink); g.fillEllipse (x - 4.5f, y - 4.5f, 9.0f, 9.0f);
        g.setColour (d.acc); g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
    }
    Ed& d;
private:
    std::vector<int> watched; float lastSum = -1;
};

/** Zweiachsiges Ziehen: x und y steuern je einen Parameter. */
class XYDisp : public Disp
{
public:
    XYDisp (Ed& ed, std::vector<int> watch, int xParam, int yParam) : Disp (ed, std::move (watch)), xp (xParam), yp (yParam) {}
    void mouseDown (const juce::MouseEvent&) override
    {
        if (xp >= 0) { auto r = ref (xp); r.begin(); sx = r.norm(); }
        if (yp >= 0) { auto r = ref (yp); r.begin(); sy = r.norm(); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (xp >= 0) ref (xp).setNorm (sx + (float) e.getDistanceFromDragStartX() / (float) getWidth());
        if (yp >= 0) ref (yp).setNorm (sy - (float) e.getDistanceFromDragStartY() / (float) getHeight());
    }
    void mouseUp (const juce::MouseEvent&) override { if (xp >= 0) ref (xp).end(); if (yp >= 0) ref (yp).end(); }
protected:
    int xp, yp; float sx = 0, sy = 0;
};

class WaveDisp : public XYDisp
{
public:
    WaveDisp (Ed& ed, int layerIndex) : XYDisp (ed, { modeParam (ed.mode, Y (layerIndex, LY_WAVE)), modeParam (ed.mode, Y (layerIndex, LY_UNI)), modeParam (ed.mode, Y (layerIndex, LY_DETUNE)),
                                                      modeParam (ed.mode, Y (layerIndex, LY_SRC)), modeParam (ed.mode, Y (layerIndex, LY_TABLE)), modeParam (ed.mode, Y (layerIndex, LY_WARP)),
                                                      modeParam (ed.mode, Y (layerIndex, LY_WARPAMT)) }, Y (layerIndex, LY_WAVE), Y (layerIndex, LY_WARPAMT)), layer (layerIndex) {}
    const SampleData* sample() const { return d.p.samples[sampleSlot (d.mode, layer)].load(); }
    bool isSample() const { return val (Y (layer, LY_SRC)) > 0.5f; }
    void draw (Graphics& g, Rectangle<float> b) override
    {
        if (isSample())
        {
            const auto* s = sample();
            if (s == nullptr)
            {
                g.setColour (theme::dim); g.setFont (theme::font (13.0f));
                g.drawText (U ("Sample hierher ziehen oder klicken"), b, juce::Justification::centred, false);
                return;
            }
            const int cols = (int) b.getWidth(), n = s->length();
            g.setColour (d.acc);
            for (int x = 0; x < cols; ++x)
            {
                float top = 0;
                const int from = (int) ((juce::int64) x * n / cols), to = juce::jmax (from + 1, (int) ((juce::int64) (x + 1) * n / cols));
                for (int i = from; i < to; i += juce::jmax (1, (to - from) / 24)) top = juce::jmax (top, std::fabs (s->l[(size_t) i]));
                g.fillRect (b.getX() + (float) x, b.getCentreY() - top * b.getHeight() * 0.46f, 1.0f, top * b.getHeight() * 0.92f);
            }
            g.setColour (theme::ink); g.setFont (theme::font (11.5f));
            g.drawText (U (s->name), b.removeFromTop (13.0f), juce::Justification::centredLeft, true);
            return;
        }
        const float pos = val (Y (layer, LY_WAVE)), amt = val (Y (layer, LY_WARPAMT)); Path p; const int N = 300;
        const int table = (int) val (Y (layer, LY_TABLE)), warp = (int) val (Y (layer, LY_WARP));
        const auto& bank = WavetableBank::get();
        juce::Random r (5); float last = 0;
        for (int i = 0; i <= N; ++i)
        {
            const float ph = std::fmod ((float) i / (float) N * 2.0f, 1.0f), rp = warpPhase (ph, warp, amt, last);
            last = table == WT_NOISE_INDEX ? r.nextFloat() * 2.0f - 1.0f : bank.ready (table) ? bank.shape (table, pos, rp) : std::sin (6.2831853f * rp);
            const float y = b.getCentreY() - last * b.getHeight() * 0.42f;
            const float x = b.getX() + b.getWidth() * (float) i / (float) N;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        const int uni = (int) std::round (val (Y (layer, LY_UNI)));
        for (int u = 1; u < uni; ++u)
        {
            const float o = (float) (u - uni / 2) * juce::jmin (5.0f, val (Y (layer, LY_DETUNE)) * 0.25f);
            g.setColour (d.acc.withAlpha (0.25f));
            g.strokePath (p, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (o, 0));
        }
        stroke (g, p);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (isSample()) { if (sample() == nullptr) d.e.chooseSample (layer); return; }
        XYDisp::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! isSample()) XYDisp::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! isSample()) XYDisp::mouseUp (e); }
    void tick() override
    {
        Disp::tick();
        const int v = d.p.sampleVersion.load();
        if (v != seen) { seen = v; repaint(); }
    }
private:
    int layer, seen = -1;
};

class PitchDisp : public XYDisp
{
public:
    PitchDisp (Ed& ed, int l) : XYDisp (ed, { modeParam (ed.mode, Y (l, LY_PAMT)), modeParam (ed.mode, Y (l, LY_PTIME)), modeParam (ed.mode, Y (l, LY_PCURVE)) }, Y (l, LY_PTIME), Y (l, LY_PAMT)), layer (l) {}
    void draw (Graphics& g, Rectangle<float> b) override
    {
        const float nx = ref (Y (layer, LY_PTIME)).norm(), ny = ref (Y (layer, LY_PAMT)).norm(), c = 1.0f + val (Y (layer, LY_PCURVE)) * 9.0f;
        const float zero = b.getY() + b.getHeight() * (1.0f - params()[(size_t) modeParam (0, Y (0, LY_PAMT))].toNorm (0));
        g.setColour (theme::line); g.drawHorizontalLine ((int) zero, b.getX(), b.getRight());
        const float top = b.getBottom() - ny * b.getHeight(), endX = b.getX() + nx * b.getWidth();
        Path p; p.startNewSubPath (b.getX(), top);
        for (int i = 1; i <= 80; ++i)
        {
            const float t = (float) i / 80.0f;
            p.lineTo (b.getX() + t * (endX - b.getX()), zero + (top - zero) * std::pow (1.0f - t, c));
        }
        p.lineTo (b.getRight(), zero);
        stroke (g, p); dot (g, endX, zero);
    }
private:
    int layer;
};

class AmpDisp : public XYDisp
{
public:
    AmpDisp (Ed& ed, int l) : XYDisp (ed, { modeParam (ed.mode, Y (l, LY_ATT)), modeParam (ed.mode, Y (l, LY_LEN)), modeParam (ed.mode, Y (l, LY_SUS)), modeParam (ed.mode, Y (l, LY_REL)),
                                            modeParam (ed.mode, Y (l, LY_CURVE)), modeParam (ed.mode, Y (l, LY_HOLD)) }, Y (l, LY_LEN), Y (l, LY_SUS)), layer (l) {}
    void draw (Graphics& g, Rectangle<float> b) override
    {
        const bool hold = val (Y (layer, LY_HOLD)) > 0.5f;
        const float sus = hold ? val (Y (layer, LY_SUS)) : 0.0f, c = 0.6f + val (Y (layer, LY_CURVE)) * 5.4f;
        const float a = b.getX() + ref (Y (layer, LY_ATT)).norm() * b.getWidth() * 0.2f;
        const float e = a + ref (Y (layer, LY_LEN)).norm() * (b.getRight() - a);
        const auto Yv = [&] (float v) { return b.getBottom() - v * b.getHeight(); };
        Path p; p.startNewSubPath (b.getX(), Yv (0)); p.lineTo (a, Yv (1));
        for (int i = 1; i <= 60; ++i) { const float t = (float) i / 60.0f; p.lineTo (a + (e - a) * t, Yv (sus + (1 - sus) * std::pow (1 - t, c))); }
        if (hold && sus > 0.0f) p.lineTo (b.getRight(), Yv (sus));
        fillUnder (g, p, b); stroke (g, p); dot (g, e, Yv (sus));
    }
private:
    int layer;
};

static float svfMag (int type, float f, float fc, float res)
{
    const float k = 2.0f - 1.96f * res, w = f / fc, a = 1 - w * w, den = std::sqrt (a * a + k * k * w * w);
    switch (type) { case 0: return 1 / den; case 1: return w * w / den; case 2: return w / den; default: return std::fabs (a) / den; }
}
static Colour heat (float v, Colour acc)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    return v < 0.6f ? theme::well.interpolatedWith (acc, v / 0.6f) : acc.interpolatedWith (theme::ink, (v - 0.6f) / 0.4f);
}
static String hzText (float hz)
{
    const int midi = (int) std::round (69.0f + 12.0f * std::log2 (juce::jmax (hz, 1.0f) / 440.0f));
    return (hz >= 1000.0f ? String (hz / 1000.0f, 2) + " kHz" : String (hz, 1) + " Hz") + "   " + (midi >= 0 ? noteName (midi) : String());
}
static float logX (float hz, float l = 20.0f, float h = 20000.0f) { return std::log (hz / l) / std::log (h / l); }
static float xHz (float x, float l = 20.0f, float h = 20000.0f) { return l * std::pow (h / l, x); }

class FilterDisp : public XYDisp
{
public:
    FilterDisp (Ed& ed, int l) : XYDisp (ed, { modeParam (ed.mode, Y (l, LY_CUT)), modeParam (ed.mode, Y (l, LY_RES)), modeParam (ed.mode, Y (l, LY_FTYPE)), modeParam (ed.mode, Y (l, LY_FAMT)) }, Y (l, LY_CUT), Y (l, LY_RES)), layer (l) {}
    void draw (Graphics& g, Rectangle<float> b) override
    {
        const auto curve = [&] (float fc)
        {
            Path p; const int type = (int) val (Y (layer, LY_FTYPE)); const float res = val (Y (layer, LY_RES));
            const auto mag = [&] (float hz)
            {
                switch (type)
                {
                    case 0: return svfMag (0, hz, fc, res);
                    case 1: return std::pow (svfMag (0, hz, fc, res), 2.0f);
                    case 2: return svfMag (1, hz, fc, res);
                    case 3: return std::pow (svfMag (1, hz, fc, res), 2.0f);
                    case 4: return svfMag (2, hz, fc, res);
                    case 5: return svfMag (3, hz, fc, res);
                    case 6: return 0.08f + std::fabs (std::cos (3.14159265f * hz / fc));
                    case 7: return 0.08f + std::fabs (std::sin (3.14159265f * hz / fc));
                    case 8: { float m = 0; for (int k = 0; k < 3; ++k) m = juce::jmax (m, svfMag (2, hz, formantHz (fc, k), 0.7f + res * 0.28f) * (k == 0 ? 1.0f : 0.7f)); return m; }
                    default: return 1.0f;
                }
            };
            const int steps = type == 6 || type == 7 ? 400 : 120;
            for (int i = 0; i <= steps; ++i)
            {
                const float x = (float) i / (float) steps, db = 20.0f * std::log10 (mag (xHz (x)) + 1e-4f);
                const float y = b.getY() + b.getHeight() * (0.3f - db / 60.0f);
                if (i == 0) p.startNewSubPath (b.getX() + x * b.getWidth(), y); else p.lineTo (b.getX() + x * b.getWidth(), juce::jlimit (b.getY(), b.getBottom() + 2, y));
            }
            return p;
        };
        const float fc = val (Y (layer, LY_CUT));
        if (std::fabs (val (Y (layer, LY_FAMT))) > 0.01f)
        {
            g.setColour (theme::dim.withAlpha (0.6f));
            g.strokePath (curve (juce::jlimit (20.0f, 20000.0f, fc * std::pow (2.0f, val (Y (layer, LY_FAMT))))), juce::PathStrokeType (1.0f));
        }
        auto p = curve (fc); fillUnder (g, p, b); stroke (g, p);
    }
private:
    int layer;
};

class DriveDisp : public XYDisp
{
public:
    DriveDisp (Ed& ed, int l) : XYDisp (ed, { modeParam (ed.mode, Y (l, LY_DRV)), modeParam (ed.mode, Y (l, LY_DRVTYPE)), modeParam (ed.mode, Y (l, LY_DRV2)), modeParam (ed.mode, Y (l, LY_DRV2TYPE)) }, -1, Y (l, LY_DRV)), layer (l) {}
    void draw (Graphics& g, Rectangle<float> b) override
    {
        const float gain = std::pow (10.0f, val (Y (layer, LY_DRV)) * 2.0f); const int t = (int) val (Y (layer, LY_DRVTYPE)), t2 = (int) val (Y (layer, LY_DRV2TYPE));
        const float g2 = val (Y (layer, LY_DRV2)), gain2 = std::pow (10.0f, g2 * 1.8f);
        g.setColour (theme::line); g.drawHorizontalLine ((int) b.getCentreY(), b.getX(), b.getRight());
        g.drawVerticalLine ((int) b.getCentreX(), b.getY(), b.getBottom());
        Path p;
        for (int i = 0; i <= 200; ++i)
        {
            const float x = (float) i / 200.0f * 2 - 1, y0 = shape (x * gain, t), y = g2 > 0.0001f ? shape (y0 * gain2, t2) : y0;
            const float px = b.getCentreX() + x * b.getWidth() * 0.5f, py = b.getCentreY() - y * b.getHeight() * 0.46f;
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        stroke (g, p);
    }
private:
    int layer;
};

class ChordDisp : public Disp
{
public:
    ChordDisp (Ed& ed) : Disp (ed, { modeParam (ed.mode, M_CHORD) }) {}
    void draw (Graphics& g, Rectangle<float> b) override
    {
        int iv[5]; const int n = chordIntervals ((int) val (M_CHORD), iv);
        static const int whites[7] = { 0, 2, 4, 5, 7, 9, 11 }, blacks[5] = { 1, 3, 6, 8, 10 }, bpos[5] = { 0, 1, 3, 4, 5 };
        const float w = b.getWidth() / 14.0f;
        const auto on = [&] (int s) { for (int i = 0; i < n; ++i) if (iv[i] == s) return true; return false; };
        for (int o = 0; o < 2; ++o) for (int i = 0; i < 7; ++i)
        {
            Rectangle<float> r (b.getX() + (float) (o * 7 + i) * w, b.getY(), w - 1, b.getHeight());
            g.setColour (on (o * 12 + whites[i]) ? d.acc : theme::plateHi); g.fillRoundedRectangle (r, 2.0f);
        }
        for (int o = 0; o < 2; ++o) for (int i = 0; i < 5; ++i)
        {
            Rectangle<float> r (b.getX() + ((float) (o * 7 + bpos[i]) + 0.68f) * w, b.getY(), w * 0.62f, b.getHeight() * 0.6f);
            g.setColour (on (o * 12 + blacks[i]) ? d.acc : theme::well); g.fillRoundedRectangle (r, 2.0f);
        }
    }
};

// ---------------- Vorschau des Klangs --------------------------------------
struct TekkforgeEditor::Preview : public juce::Thread
{
    explicit Preview (TekkforgeProcessor& pr) : juce::Thread ("Tekkforge Vorschau"), p (pr)
    {
        engine = std::make_unique<Engine>();
        engine->allocate (44100.0f); engine->setSampleRate (44100.0f); engine->curves = &p.curves; engine->samples = p.samples;
        startThread (juce::Thread::Priority::low);
    }
    ~Preview() override { stopThread (2000); }
    void request (const float* v, int note)
    {
        { std::lock_guard<std::mutex> l (m); std::copy (v, v + NUM_PARAMS, want.begin()); wantNote = note; pending = true; }
        notify();
    }
    void run() override
    {
        std::vector<float> L, R;
        juce::dsp::FFT fft (13); std::vector<float> fftData (1 << 14);
        while (! threadShouldExit())
        {
            wait (-1);
            std::array<float, NUM_PARAMS> v; int note;
            { std::lock_guard<std::mutex> l (m); if (! pending) continue; v = want; note = wantNote; pending = false; }
            const int mode = (int) v[G_MODE]; const float* q = v.data() + modeParam (mode, 0);
            float body = 0.02f, wanted = 0.05f;
            for (int l = 0; l < NUM_LAYERS; ++l)
            {
                const float* y = q + Y (l, 0);
                if (y[LY_MUTE] > 0.5f) continue;
                const float len = (y[LY_ATT] + y[LY_LEN]) * 0.001f;
                body = juce::jmax (body, len);
                wanted = juce::jmax (wanted, len * 1.05f + y[LY_REL] * 0.001f + 0.02f);
            }
            for (int l = 0; l < NUM_LAYERS; ++l)
                if (const auto* smp = p.samples[sampleSlot (mode, l)].load(); smp != nullptr && q[Y (l, LY_SRC)] > 0.5f && q[Y (l, LY_FREE)] > 0.5f)
                    wanted = juce::jmax (wanted, (float) smp->length() / smp->rate);
            const float secs = juce::jlimit (0.08f, 3.0f, wanted);
            const int n = (int) (secs * 44100.0f); L.assign ((size_t) n, 0); R.assign ((size_t) n, 0);
            engine->reset(); engine->noteOn (note, 1.0f, v.data());
            const int off = (int) (body * 44100.0f); bool sent = false;
            for (int i = 0; i < n; i += 256)
            {
                if (! sent && i >= off) { engine->noteOff (note, v.data()); sent = true; }
                engine->process (L.data() + i, R.data() + i, juce::jmin (256, n - i), v.data(), 170.0, -1.0);
            }
            std::vector<float> lo (600), hi (600);
            for (int i = 0; i < n; ++i)
            {
                const auto c = (size_t) (i * 600 / n);
                lo[c] = juce::jmin (lo[c], L[(size_t) i]); hi[c] = juce::jmax (hi[c], L[(size_t) i]);
            }
            std::fill (fftData.begin(), fftData.end(), 0.0f);
            const int fftN = 1 << 13, use = juce::jmin (fftN, n);
            for (int i = 0; i < use; ++i)
                fftData[(size_t) i] = (L[(size_t) i] + R[(size_t) i]) * 0.5f * (0.5f + 0.5f * std::cos (kPi * (float) i / (float) use));
            fft.performFrequencyOnlyForwardTransform (fftData.data());
            std::vector<float> spec (fftData.begin(), fftData.begin() + fftN / 2);
            std::lock_guard<std::mutex> l (m);
            waveLo = std::move (lo); waveHi = std::move (hi); staticSpec = std::move (spec); duration = secs; ++version;
        }
    }
    TekkforgeProcessor& p;
    std::unique_ptr<Engine> engine;
    std::mutex m;
    std::array<float, NUM_PARAMS> want {}; int wantNote = 29; bool pending = false;
    std::vector<float> waveLo, waveHi, staticSpec; float duration = 0.3f; std::atomic<int> version { 0 };
};

// Live-Spektrum mit den Zonen des aktuellen Modus
class Spectrum : public Ticking
{
public:
    Spectrum (Ed& ed, std::function<void (std::vector<float>&)> staticSource)
        : d (ed), fft (12), source (std::move (staticSource)), data (1 << 13), smooth (256, -90.0f), now (256, -90.0f),
          sonogram (juce::Image::RGB, 320, 160, true)
    {
        juce::Graphics g (sonogram); g.fillAll (theme::well);
    }
    void tick() override
    {
        const int w = d.p.scopeWrite.load (std::memory_order_acquire), N = 1 << 12;
        for (int i = 0; i < N; ++i)
        {
            const int k = (w - N + i + TekkforgeProcessor::SCOPE) % TekkforgeProcessor::SCOPE;
            data[(size_t) i] = d.p.scope[(size_t) k].load (std::memory_order_relaxed) * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) N));
        }
        std::fill (data.begin() + N, data.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (data.data());
        const float sr = (float) d.p.currentSampleRate();
        for (int i = 0; i < 256; ++i)
        {
            const float hz = xHz ((float) i / 255.0f), bin = hz / sr * (float) N;
            const int a = juce::jlimit (1, N / 2 - 2, (int) bin);
            const float db = 20.0f * std::log10 (juce::jmax (data[(size_t) a], data[(size_t) a + 1]) * 4.0f / (float) N + 1e-5f);
            now[(size_t) i] = db;
            smooth[(size_t) i] = juce::jmax (db, smooth[(size_t) i] - 2.5f);
        }
        // Spektrogramm: Bild eine Spalte nach links, neue Spalte rechts
        const int iw = sonogram.getWidth(), ih = sonogram.getHeight();
        sonogram.moveImageSection (0, 0, 1, 0, iw - 1, ih);
        for (int y = 0; y < ih; ++y)
        {
            const int idx = (int) ((1.0f - (float) y / (float) (ih - 1)) * 255.0f);
            sonogram.setPixelAt (iw - 1, y, heat (std::pow (juce::jlimit (0.0f, 1.0f, (now[(size_t) idx] + 66.0f) / 66.0f), 1.6f), d.acc));
        }
        source (stat);
        repaint();
    }
    Rectangle<float> plot() const { auto b = getLocalBounds().toFloat().reduced (6.0f); b.removeFromBottom (14.0f); return b; }
    Rectangle<int> tabs() const { return { getWidth() - 236, 8, 228, 20 }; }
    void setHover (juce::Point<int> p) { hover = p; repaint(); }
    void clearHover() { hover = { -1, -1 }; repaint(); }
    void paint (Graphics& g) override
    {
        auto o = getLocalBounds().toFloat();
        g.setColour (theme::well); g.fillRoundedRectangle (o, 6.0f);
        auto b = plot();
        const auto X = [&] (float hz) { return b.getX() + logX (hz) * b.getWidth(); };
        const auto Y = [&] (float db) { return juce::jlimit (b.getY(), b.getBottom(), b.getY() + b.getHeight() * (-db / 72.0f)); };
        const int mode = d.e.specMode;

        if (mode == 2)
        {
            g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
            g.drawImage (sonogram, b);
            g.setColour (theme::ink.withAlpha (0.8f)); g.setFont (theme::font (11.5f));
            for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 5000.0f })
            {
                const float y = b.getBottom() - logX (hz) * b.getHeight();
                g.setColour (theme::well.withAlpha (0.75f)); g.fillRoundedRectangle (b.getX() + 2, y - 7, 32, 14, 3.0f);
                g.setColour (theme::ink);
                g.drawText (hz >= 1000 ? String ((int) (hz / 1000)) + "k" : String ((int) hz), Rectangle<float> (b.getX() + 5, y - 7, 28, 14), juce::Justification::centredLeft, false);
            }
        }
        else
        {
            const auto& gd = guideFor (d.mode);
            for (size_t i = 0; i < gd.zones.size(); ++i)
            {
                const auto& z = gd.zones[i]; const bool keepOut = z.lo < 25.0f;
                Rectangle<float> r (X (z.lo), b.getY(), X (z.hi) - X (z.lo), b.getHeight());
                g.setColour ((keepOut ? theme::hot : d.acc).withAlpha (keepOut ? 0.14f : 0.09f + 0.05f * (float) (i % 2))); g.fillRect (r);
                g.setColour (keepOut ? theme::hot : theme::ink); g.setFont (theme::font (11.5f));
                g.drawText (U (z.name), r.withHeight (15.0f).reduced (2, 0), juce::Justification::centredLeft, true);
            }
            if (! stat.empty() && mode == 0)
            {
                float maxV = 1e-6f; for (auto v : stat) maxV = juce::jmax (maxV, v);
                Path p; const float n = (float) stat.size() * 2.0f;
                for (int i = 0; i < 300; ++i)
                {
                    const float x = (float) i / 299.0f, hz = xHz (x); const int bin = juce::jlimit (1, (int) stat.size() - 1, (int) (hz / 44100.0f * n));
                    const float y = Y (20.0f * std::log10 (stat[(size_t) bin] / maxV + 1e-5f) - 6.0f);
                    if (i == 0) p.startNewSubPath (b.getX(), y); else p.lineTo (b.getX() + x * b.getWidth(), y);
                }
                Path f (p); f.lineTo (b.getRight(), b.getBottom()); f.lineTo (b.getX(), b.getBottom()); f.closeSubPath();
                g.setColour (d.acc.withAlpha (0.28f)); g.fillPath (f);
                g.setColour (d.acc.withAlpha (0.8f)); g.strokePath (p, juce::PathStrokeType (1.4f));
            }
            if (mode == 1)
            {
                constexpr int bars = 56;
                const float w = b.getWidth() / (float) bars;
                for (int i = 0; i < bars; ++i)
                {
                    float top = -90.0f;
                    for (int k = i * 256 / bars; k < (i + 1) * 256 / bars; ++k) top = juce::jmax (top, smooth[(size_t) k]);
                    const float y = Y (top);
                    g.setColour (heat ((top + 72.0f) / 72.0f, d.acc));
                    g.fillRect (b.getX() + (float) i * w + 1.0f, y, w - 2.0f, b.getBottom() - y);
                }
            }
            else
            {
                Path live; bool any = false;
                for (int i = 0; i < 256; ++i)
                {
                    const float x = b.getX() + (float) i / 255.0f * b.getWidth(), y = Y (smooth[(size_t) i]);
                    if (smooth[(size_t) i] > -70.0f) any = true;
                    if (i == 0) live.startNewSubPath (x, y); else live.lineTo (x, y);
                }
                if (any) { g.setColour (theme::ink); g.strokePath (live, juce::PathStrokeType (1.6f)); }
            }
            g.setColour (theme::dim); g.setFont (theme::font (11.5f));
            for (float hz : { 30.0f, 60.0f, 120.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
                g.drawText (hz >= 1000 ? String ((int) (hz / 1000)) + "k" : String ((int) hz), Rectangle<float> (X (hz) - 18, b.getBottom() + 1, 36, 13), juce::Justification::centred, false);
        }

        static const char* names[] = { "Kurve", "Balken", "Spektrogramm" };
        static const int widths[] = { 56, 62, 110 };
        auto t = tabs();
        for (int i = 0; i < 3; ++i)
        {
            auto r = t.removeFromLeft (widths[i]).reduced (1, 0).toFloat();
            g.setColour (i == mode ? d.acc : theme::plateHi); g.fillRoundedRectangle (r, 4.0f);
            g.setColour (i == mode ? theme::well : theme::ink); g.setFont (theme::font (12.0f, i == mode));
            g.drawText (names[i], r, juce::Justification::centred, false);
        }
        if (hover.x >= 0)
        {
            const float hz = mode == 2 ? xHz (juce::jlimit (0.0f, 1.0f, (b.getBottom() - (float) hover.y) / b.getHeight()))
                                       : xHz (juce::jlimit (0.0f, 1.0f, ((float) hover.x - b.getX()) / b.getWidth()));
            g.setColour (theme::ink.withAlpha (0.5f));
            if (mode == 2) g.fillRect (b.getX(), (float) hover.y, b.getWidth(), 1.0f); else g.fillRect ((float) hover.x, b.getY(), 1.0f, b.getHeight());
            Rectangle<float> tag (juce::jmin ((float) hover.x + 8.0f, b.getRight() - 124.0f), juce::jmax (b.getY() + 24.0f, (float) hover.y - 24.0f), 120.0f, 20.0f);
            g.setColour (theme::well); g.fillRoundedRectangle (tag, 4.0f);
            g.setColour (theme::ink); g.setFont (theme::font (12.5f, true));
            g.drawText (hzText (hz), tag, juce::Justification::centred, false);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { setHover (e.getPosition()); }
    void mouseExit (const juce::MouseEvent&) override { clearHover(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! tabs().contains (e.getPosition())) return;
        const int x = e.x - tabs().getX();
        d.e.specMode = x < 56 ? 0 : x < 118 ? 1 : 2;
        repaint();
    }
private:
    Ed& d; juce::dsp::FFT fft; std::function<void (std::vector<float>&)> source;
    std::vector<float> data, smooth, now, stat;
    juce::Image sonogram; juce::Point<int> hover { -1, -1 };
};

class WaveShapeView : public Ticking
{
public:
    WaveShapeView (Ed& ed, std::function<int (std::vector<float>&, std::vector<float>&, float&)> src) : d (ed), source (std::move (src)) {}
    void tick() override { int v = source (lo, hi, len); if (v != seen) { seen = v; repaint(); } }
    void paint (Graphics& g) override
    {
        auto o = getLocalBounds().toFloat();
        g.setColour (theme::well); g.fillRoundedRectangle (o, 6.0f);
        auto b = o.reduced (8.0f);
        if (lo.empty()) return;
        g.setColour (theme::line); g.drawHorizontalLine ((int) b.getCentreY(), b.getX(), b.getRight());
        const float top = b.getCentreY(), h = b.getHeight() * 0.48f;
        const int N = (int) lo.size(); Path p;
        p.startNewSubPath (b.getX(), top);
        for (int i = 0; i < N; ++i) p.lineTo (b.getX() + b.getWidth() * (float) i / (float) N, top - juce::jmin (hi[(size_t) i], 1.0f) * h);
        for (int i = N - 1; i >= 0; --i) p.lineTo (b.getX() + b.getWidth() * (float) i / (float) N, top - juce::jmax (lo[(size_t) i], -1.0f) * h);
        p.closeSubPath();
        g.setGradientFill (juce::ColourGradient (theme::ink, b.getX(), 0, d.acc, b.getX() + b.getWidth() * 0.35f, 0, false));
        g.fillPath (p);
        g.setColour (theme::dim); g.setFont (theme::font (12.0f));
        g.drawText (String (juce::roundToInt (len * 1000.0f)) + " ms", b.removeFromBottom (14.0f), juce::Justification::bottomRight, false);
    }
private:
    Ed& d; std::function<int (std::vector<float>&, std::vector<float>&, float&)> source;
    std::vector<float> lo, hi; float len = 0; int seen = -1;
};

// Baugruppe mit Titel; Inhalte werden per lay() platziert
class Panel : public Ticking
{
public:
    Panel (Ed& ed, String t) : d (ed), title (std::move (t)) {}
    void paint (Graphics& g) override
    {
        g.setColour (theme::plateHi.withAlpha (0.5f)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        g.setColour (theme::ink); g.setFont (theme::font (14.0f, true));
        g.drawText (title, 10, 6, getWidth() - 20, 18, juce::Justification::centredLeft, false);
    }
    template <class T> T* own (std::unique_ptr<T> c) { auto* r = c.get(); addAndMakeVisible (r); keep.push_back (std::move (c)); return r; }
    Knob* knob (int mp, const char* name) { auto* k = own (std::make_unique<Knob> (d, modeParam (d.mode, mp), U (name))); knobs.push_back (k); return k; }
    void tick() override { Ticking::tick(); tickChoices(); }
    Knob* knobAbs (int index, const char* name) { auto* k = own (std::make_unique<Knob> (d, index, U (name))); knobs.push_back (k); return k; }
    void resized() override
    {
        auto b = getLocalBounds().reduced (8, 6);
        auto head = b.removeFromTop (22);
        if (headStrip != nullptr) headStrip->setBounds (head.removeFromRight (headWidth).reduced (0, 1));
        for (auto& h : headItems) { h.first->setBounds (head.removeFromRight (h.second).reduced (1, 1)); }
        b.removeFromTop (3);
        if (! knobs.empty())
        {
            auto row = b.removeFromBottom (78);
            const int w = row.getWidth() / (int) knobs.size();
            for (auto* k : knobs) k->setBounds (row.removeFromLeft (w));
        }
        if (fill != nullptr) fill->setBounds (b.reduced (0, 2));
    }
    juce::Component* fill = nullptr; Strip* headStrip = nullptr; int headWidth = 120, oscIndex = -1;
    std::vector<std::pair<juce::Component*, int>> headItems;
    ChoiceBtn* choice (int mp, int width, std::function<String (int)> grp = {})
    {
        auto* c = own (std::make_unique<ChoiceBtn> (d, modeParam (d.mode, mp), std::move (grp))); headItems.push_back ({ c, width }); choices.push_back (c); return c;
    }
    void tickChoices() { for (auto* c : choices) c->tick(); }
private:
    Ed& d; String title; std::vector<std::unique_ptr<juce::Component>> keep; std::vector<Knob*> knobs; std::vector<ChoiceBtn*> choices;
};

class CurveEdit : public Ticking
{
public:
    CurveEdit (Ed& ed, int laneIndex) : d (ed), lane (laneIndex) {}
    void tick() override
    {
        const float p = d.p.engine.lfoPos[lane].load (std::memory_order_relaxed);
        if (p != lastPos) { lastPos = p; repaint(); }
    }
    void paint (Graphics& g) override
    {
        auto o = getLocalBounds().toFloat();
        g.setColour (theme::well); g.fillRoundedRectangle (o, 6.0f);
        auto b = o.reduced (6.0f);
        g.setColour (theme::line);
        for (int i = 1; i < 16; ++i) { const float x = b.getX() + b.getWidth() * (float) i / 16.0f; g.drawVerticalLine ((int) x, b.getY(), b.getBottom()); }
        const auto& c = d.p.curves[(size_t) lane];
        Path p;
        for (int i = 0; i < LFO_POINTS; ++i)
        {
            const float x = b.getX() + b.getWidth() * (float) i / (float) (LFO_POINTS - 1), y = b.getBottom() - c[(size_t) i].load() * b.getHeight();
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        Path f (p); f.lineTo (b.getRight(), b.getBottom()); f.lineTo (b.getX(), b.getBottom()); f.closeSubPath();
        g.setColour (d.acc.withAlpha (0.18f)); g.fillPath (f);
        g.setColour (d.acc); g.strokePath (p, juce::PathStrokeType (2.0f));
        g.setColour (theme::ink);
        g.drawVerticalLine ((int) (b.getX() + b.getWidth() * lastPos), b.getY(), b.getBottom());
    }
    void paintAt (const juce::MouseEvent& e)
    {
        auto b = getLocalBounds().toFloat().reduced (6.0f);
        const int i = juce::jlimit (0, LFO_POINTS - 1, (int) std::round (((float) e.x - b.getX()) / b.getWidth() * (float) (LFO_POINTS - 1)));
        const float y = juce::jlimit (0.0f, 1.0f, (b.getBottom() - (float) e.y) / b.getHeight());
        const int a = prev < 0 ? i : prev;
        const int lo = juce::jmin (a, i), hi = juce::jmax (a, i);
        for (int k = lo; k <= hi; ++k)
        {
            const float t = hi == lo ? 1.0f : (float) (k - a) / (float) (i - a);
            d.p.curves[(size_t) lane][(size_t) k].store (prevY + (y - prevY) * t);
        }
        prev = i; prevY = y; repaint();
    }
    void mouseDown (const juce::MouseEvent& e) override { prev = -1; paintAt (e); }
    void mouseDrag (const juce::MouseEvent& e) override { paintAt (e); }
private:
    Ed& d; int lane, prev = -1; float prevY = 0, lastPos = -1;
};

// ---------------- Env und LFO wie in Vital --------------------------------
/** Reiter einer Quelle: Klick waehlt sie aus, Ziehen auf einen Regler verbindet sie. */
class SourceChip : public Ticking
{
public:
    SourceChip (Ed& ed, int sourceIndex) : d (ed), src (sourceIndex) {}
    bool selected() const { return src < NUM_LFOS ? d.e.lfoTab == src : d.e.envTab == src - NUM_LFOS; }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const bool sel = selected();
        g.setColour (sel ? d.acc : theme::well); g.fillRoundedRectangle (b, 5.0f);
        const float v = juce::jlimit (0.0f, 1.0f, val);
        g.setColour ((sel ? theme::well : d.acc).withAlpha (0.35f)); g.fillRoundedRectangle (b.withWidth (b.getWidth() * v), 5.0f);
        g.setColour (sel ? theme::well : theme::ink); g.setFont (theme::font (13.0f, true));
        g.drawText (U (sourceName (src)), b, juce::Justification::centred, false);
    }
    void tick() override
    {
        const float v = d.p.engine.sourceView[src].load (std::memory_order_relaxed);
        if (std::fabs (v - val) > 0.01f) { val = v; repaint(); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() < 4) return;
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! c->isDragAndDropActive())
                c->startDragging ("tkmod:" + String (src), this);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        if (src < NUM_LFOS) d.e.lfoTab = src; else d.e.envTab = src - NUM_LFOS;
        d.e.requestRebuild();
    }
    juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::DraggingHandCursor; }
private:
    Ed& d; int src; float val = 0;
};

/** Env-Anzeige mit ziehbaren Punkten: Attack, Decay und Sustain, Release. */
class EnvEdit : public Disp
{
public:
    EnvEdit (Ed& ed, int index) : Disp (ed, { envParam (index, EV_ATT), envParam (index, EV_DEC), envParam (index, EV_SUS), envParam (index, EV_REL), envParam (index, EV_CURVE) }), env (index) {}
    PRef at (int k) const { return { d.p.param (envParam (env, k)) }; }
    juce::Point<float> handle (int i, Rectangle<float> b) const
    {
        const float a = at (EV_ATT).norm() * 0.3f, de = a + at (EV_DEC).norm() * 0.3f, su = at (EV_SUS).norm(), r = de + 0.1f + at (EV_REL).norm() * 0.3f;
        const float x = i == 0 ? a : i == 1 ? de : r, y = i == 0 ? 1.0f : i == 1 ? su : 0.0f;
        return { b.getX() + x * b.getWidth(), b.getBottom() - y * b.getHeight() };
    }
    void draw (Graphics& g, Rectangle<float> b) override
    {
        const float c = 0.6f + d.p.rawValue (envParam (env, EV_CURVE)) * 5.4f, su = at (EV_SUS).norm();
        const auto a = handle (0, b), de = handle (1, b), r = handle (2, b);
        Path p; p.startNewSubPath (b.getX(), b.getBottom()); p.lineTo (a);
        for (int i = 1; i <= 50; ++i) { const float t = (float) i / 50.0f; p.lineTo (a.x + (de.x - a.x) * t, b.getBottom() - (su + (1 - su) * std::pow (1 - t, c)) * b.getHeight()); }
        p.lineTo (de.x + b.getWidth() * 0.1f, de.y);
        for (int i = 1; i <= 30; ++i) { const float t = (float) i / 30.0f; const float x0 = de.x + b.getWidth() * 0.1f; p.lineTo (x0 + (r.x - x0) * t, b.getBottom() - su * std::pow (1 - t, 3.0f) * b.getHeight()); }
        fillUnder (g, p, b); stroke (g, p);
        for (int i = 0; i < 3; ++i) { const auto c2 = handle (i, b); dot (g, c2.x, c2.y); }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto b = getLocalBounds().toFloat().reduced (6.0f);
        float best = 1.0e9f; grabbed = 0;
        for (int i = 0; i < 3; ++i) if (const float dist = handle (i, b).getDistanceFrom (e.position); dist < best) { best = dist; grabbed = i; }
        static const int xp[3] = { EV_ATT, EV_DEC, EV_REL };
        auto x = at (xp[grabbed]); x.begin(); sx = x.norm();
        auto y = at (EV_SUS); y.begin(); sy = y.norm();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        static const int xp[3] = { EV_ATT, EV_DEC, EV_REL };
        at (xp[grabbed]).setNorm (sx + 3.3f * (float) e.getDistanceFromDragStartX() / (float) getWidth());
        if (grabbed == 1) at (EV_SUS).setNorm (sy - (float) e.getDistanceFromDragStartY() / (float) getHeight());
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        static const int xp[3] = { EV_ATT, EV_DEC, EV_REL };
        at (xp[grabbed]).end(); at (EV_SUS).end();
    }
private:
    int env, grabbed = 0; float sx = 0, sy = 0;
};

class EnvPanel : public Ticking
{
public:
    explicit EnvPanel (Ed& ed) : d (ed), edit (ed, ed.e.envTab)
    {
        addAndMakeVisible (edit);
        for (int i = 0; i < NUM_ENVS; ++i) { chips.push_back (std::make_unique<SourceChip> (d, NUM_LFOS + i)); addAndMakeVisible (*chips.back()); }
        const std::pair<int, const char*> ks[] = { { EV_ATT, "Attack" }, { EV_DEC, "Decay" }, { EV_SUS, "Sustain" }, { EV_REL, "Release" }, { EV_CURVE, "Kurve" } };
        for (auto& k : ks) { knobs.push_back (std::make_unique<Knob> (d, envParam (d.e.envTab, k.first), U (k.second))); addAndMakeVisible (*knobs.back()); }
    }
    void paint (Graphics& g) override { g.setColour (theme::plateHi.withAlpha (0.5f)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f); }
    void resized() override
    {
        auto b = getLocalBounds().reduced (8);
        auto tabs = b.removeFromTop (28); const int w = tabs.getWidth() / NUM_ENVS;
        for (auto& c : chips) c->setBounds (tabs.removeFromLeft (w).withTrimmedRight (4));
        auto row = b.removeFromBottom (78); const int kw = row.getWidth() / (int) knobs.size();
        for (auto& k : knobs) k->setBounds (row.removeFromLeft (kw));
        b.reduce (0, 4); edit.setBounds (b);
    }
    void tick() override { edit.tick(); for (auto& c : chips) c->tick(); for (auto& k : knobs) k->tick(); }
private:
    Ed& d; EnvEdit edit;
    std::vector<std::unique_ptr<SourceChip>> chips;
    std::vector<std::unique_ptr<Knob>> knobs;
};

class LfoPanel : public Ticking
{
public:
    explicit LfoPanel (Ed& ed) : d (ed), lfo (ed.e.lfoTab), curve (ed, lfo), sync (ed, lfoParam (lfo, LF_SYNC)), trig (ed, lfoParam (lfo, LF_TRIG)),
                                 div (ed, lfoParam (lfo, LF_DIV), 4), rate (ed, lfoParam (lfo, LF_HZ), "Tempo")
    {
        for (juce::Component* c : std::initializer_list<juce::Component*> { &curve, &sync, &trig, &div, &rate }) addAndMakeVisible (c);
        for (int i = 0; i < NUM_LFOS; ++i) { chips.push_back (std::make_unique<SourceChip> (d, i)); addAndMakeVisible (*chips.back()); }
        static const char* shapes[] = { "Runter", "Hoch", "Dreieck", "Sinus", "Gate", "Zufall", "Leer" };
        for (int i = 0; i < 7; ++i)
        {
            auto b = std::make_unique<juce::TextButton> (U (shapes[i]));
            b->setColour (juce::TextButton::buttonColourId, theme::well); b->setColour (juce::TextButton::textColourOffId, theme::ink);
            b->setColour (juce::ComboBox::outlineColourId, theme::line);
            b->onClick = [this, i] { d.p.shapeLane (lfo, i); curve.repaint(); };
            addAndMakeVisible (*b); shapeBtns.push_back (std::move (b));
        }
    }
    void paint (Graphics& g) override { g.setColour (theme::plateHi.withAlpha (0.5f)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f); }
    void resized() override
    {
        auto b = getLocalBounds().reduced (8);
        auto tabs = b.removeFromTop (28); const int w = tabs.getWidth() / NUM_LFOS;
        for (auto& c : chips) c->setBounds (tabs.removeFromLeft (w).withTrimmedRight (4));
        b.removeFromTop (6);
        auto side = b.removeFromRight (152); b.removeFromRight (6);
        sync.setBounds (side.removeFromTop (24)); side.removeFromTop (3);
        trig.setBounds (side.removeFromTop (24)); side.removeFromTop (3);
        rate.setBounds (side.removeFromBottom (76)); side.removeFromBottom (3);
        div.setBounds (side);
        auto row = b.removeFromBottom (24); const int sw = row.getWidth() / 7;
        for (auto& s : shapeBtns) s->setBounds (row.removeFromLeft (sw).reduced (1, 0));
        b.removeFromBottom (4); curve.setBounds (b);
    }
    void tick() override
    {
        curve.tick(); sync.tick(); trig.tick(); div.tick(); rate.tick();
        for (auto& c : chips) c->tick();
        const bool synced = d.p.rawValue (lfoParam (lfo, LF_SYNC)) > 0.5f;
        div.setAlpha (synced ? 1.0f : 0.35f); rate.setAlpha (synced ? 0.35f : 1.0f);
    }
private:
    Ed& d; int lfo; CurveEdit curve; Strip sync, trig, div; Knob rate;
    std::vector<std::unique_ptr<SourceChip>> chips;
    std::vector<std::unique_ptr<juce::TextButton>> shapeBtns;
};

/** Eine Ebene als Zeile: alles, was den Klang dieser Ebene formt. */
class ToggleBtn : public Ticking
{
public:
    ToggleBtn (Ed& ed, int index, String label, Colour on) : d (ed), ref { ed.p.param (index) }, text (std::move (label)), onColour (on) {}
    void paint (Graphics& g) override
    {
        const bool on = ref.norm() > 0.5f;
        g.setColour (on ? onColour : theme::well); g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.setColour (on ? theme::well : theme::ink); g.setFont (theme::font (13.0f, true));
        g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
    }
    void mouseDown (const juce::MouseEvent&) override { ref.begin(); ref.setNorm (ref.norm() > 0.5f ? 0.0f : 1.0f); ref.end(); repaint(); }
    void tick() override { const float v = ref.norm(); if (v != last) { last = v; repaint(); } }
private:
    Ed& d; PRef ref; String text; Colour onColour; float last = -1;
};

/** Eine Ebene in der Mischleiste: Klick waehlt sie zum Bearbeiten aus. */
class LayerStrip : public Ticking
{
public:
    LayerStrip (Ed& ed, int layerIndex) : d (ed), l (layerIndex),
        mute (ed, modeParam (ed.mode, Y (l, LY_MUTE)), "M", theme::hot), solo (ed, modeParam (ed.mode, Y (l, LY_SOLO)), "S", ed.acc),
        level (ed, modeParam (ed.mode, Y (l, LY_LVL)), U ("Pegel"))
    {
        addAndMakeVisible (mute); addAndMakeVisible (solo); addAndMakeVisible (level);
        fxBtn.setButtonText ("FX");
        fxBtn.setColour (juce::TextButton::buttonColourId, theme::well); fxBtn.setColour (juce::TextButton::textColourOffId, theme::ink);
        fxBtn.setColour (juce::ComboBox::outlineColourId, theme::line);
        fxBtn.onClick = [this] { d.e.fxScope = l + 1; d.e.selectedSlot = layerSlot (l, 0); d.e.showPage (2); };
        eqBtn.setButtonText ("EQ");
        eqBtn.setColour (juce::TextButton::buttonColourId, theme::well); eqBtn.setColour (juce::TextButton::textColourOffId, theme::ink);
        eqBtn.setColour (juce::ComboBox::outlineColourId, theme::line);
        eqBtn.onClick = [this] { d.e.eqScope = l + 1; d.e.showPage (6); };
        addAndMakeVisible (fxBtn); addAndMakeVisible (eqBtn);
    }
    bool selected() const { return d.e.layerTab == l; }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (selected() ? d.acc.withAlpha (0.22f) : theme::plateHi.withAlpha (0.5f)); g.fillRoundedRectangle (b, 8.0f);
        if (selected()) { g.setColour (d.acc); g.drawRoundedRectangle (b.reduced (1.0f), 8.0f, 2.0f); }
        const auto v = [&] (int p) { return d.p.rawValue (modeParam (d.mode, Y (l, p))); };
        g.setColour (theme::ink); g.setFont (theme::font (16.0f, true));
        g.drawText (U (layerName (d.mode, l)), 12, 8, 150, 22, juce::Justification::centredLeft, false);
        const auto* smp = d.p.samples[sampleSlot (d.mode, l)].load();
        const String what = v (LY_SRC) > 0.5f ? (smp != nullptr ? U (smp->name) : U ("Sample fehlt")) : U (waveInfos()[juce::jlimit (0, NUM_TABLES - 1, (int) v (LY_TABLE))].name);
        g.setColour (theme::dim); g.setFont (theme::font (12.5f));
        g.drawText (what, 12, 32, 170, 18, juce::Justification::centredLeft, true);
    }
    void resized() override
    {
        auto b = getLocalBounds().reduced (6);
        level.setBounds (b.removeFromRight (70)); b.removeFromRight (4);
        auto r = b.removeFromRight (150).withSizeKeepingCentre (150, 26);
        mute.setBounds (r.removeFromLeft (30)); r.removeFromLeft (2); solo.setBounds (r.removeFromLeft (30)); r.removeFromLeft (4);
        fxBtn.setBounds (r.removeFromLeft (40)); r.removeFromLeft (2); eqBtn.setBounds (r);
    }
    void mouseDown (const juce::MouseEvent&) override { if (! selected()) { d.e.layerTab = l; d.e.requestRebuild(); } }
    void tick() override { mute.tick(); solo.tick(); level.tick(); repaintIfChanged(); }
    void repaintIfChanged()
    {
        const int t = (int) d.p.rawValue (modeParam (d.mode, Y (l, LY_TABLE))) * 2 + (int) d.p.rawValue (modeParam (d.mode, Y (l, LY_SRC)));
        if (t != shown) { shown = t; repaint(); }
    }
private:
    Ed& d; int l, shown = -1; ToggleBtn mute, solo; Knob level; juce::TextButton fxBtn, eqBtn;
};

/** Alle Werkzeuge der gewaehlten Ebene auf einmal: Quelle bis Ausgang in Signalrichtung. */
class LayerEditor : public Ticking
{
public:
    LayerEditor (Ed& ed, int layerIndex) : d (ed), l (layerIndex)
    {
        const auto add = [&] (const char* t) { panels.push_back (std::make_unique<Panel> (d, U (t))); addAndMakeVisible (*panels.back()); return panels.back().get(); };
        const auto strip = [&] (Panel* p, int param, int w) { p->headStrip = p->own (std::make_unique<Strip> (d, modeParam (d.mode, Y (l, param)))); p->headWidth = w; };

        auto* src = add ("Quelle"); src->oscIndex = l; src->fill = src->own (std::make_unique<WaveDisp> (d, l)); strip (src, LY_SRC, 130);
        src->choice (Y (l, LY_WARP), 120);
        src->choice (Y (l, LY_TABLE), 160, [] (int i) { return U (waveInfos()[i].group); });
        waveKnobs[0] = src->knob (Y (l, LY_WAVE), "Position"); waveKnobs[1] = src->knob (Y (l, LY_WARPAMT), "Warp");
        src->knob (Y (l, LY_TUNE), "Tune"); src->knob (Y (l, LY_FINE), "Fein");
        waveKnobs[2] = src->knob (Y (l, LY_UNI), "Stimmen"); waveKnobs[3] = src->knob (Y (l, LY_DETUNE), "Verst."); waveKnobs[4] = src->knob (Y (l, LY_WIDTH), "Breite");

        auto* amp = add ("Hüllkurve"); amp->fill = amp->own (std::make_unique<AmpDisp> (d, l)); strip (amp, LY_HOLD, 150);
        amp->knob (Y (l, LY_LEN), "Länge"); amp->knob (Y (l, LY_ATT), "Attack"); amp->knob (Y (l, LY_CURVE), "Kurve");
        susKnob = amp->knob (Y (l, LY_SUS), "Sustain"); amp->knob (Y (l, LY_REL), "Release");

        auto* pit = add ("Pitch"); pit->fill = pit->own (std::make_unique<PitchDisp> (d, l));
        pit->knob (Y (l, LY_PAMT), "Menge"); pit->knob (Y (l, LY_PTIME), "Zeit"); pit->knob (Y (l, LY_PCURVE), "Kurve"); pit->knob (Y (l, LY_GLIDE), "Tail Pitch");

        auto* fil = add ("Filter"); fil->fill = fil->own (std::make_unique<FilterDisp> (d, l)); fil->choice (Y (l, LY_FTYPE), 110);
        filterKnobs[0] = fil->knob (Y (l, LY_CUT), "Cutoff"); filterKnobs[1] = fil->knob (Y (l, LY_RES), "Reso");
        filterKnobs[2] = fil->knob (Y (l, LY_FAMT), "Env"); filterKnobs[3] = fil->knob (Y (l, LY_FDEC), "Zeit");

        auto* drv = add ("Drive"); drv->fill = drv->own (std::make_unique<DriveDisp> (d, l));
        drv->choice (Y (l, LY_DRV2TYPE), 96); drv->choice (Y (l, LY_DRVTYPE), 96);
        drv->knob (Y (l, LY_PREAMT), "Boost"); boostKnob = drv->knob (Y (l, LY_PREF), "Boost Hz");
        drv->knob (Y (l, LY_DRV), "Stufe 1"); toneKnob = drv->knob (Y (l, LY_DRVTONE), "Ton"); drv->knob (Y (l, LY_DRV2), "Stufe 2");

        auto* bas = add ("Bass"); strip (bas, LY_SUBOCT, 66);
        bas->headItems.push_back ({ bas->own (std::make_unique<PatchBtn> (d, l)), 112 });
        bas->choice (Y (l, LY_KEY), 100);
        bas->fill = bas->own (std::make_unique<KeyHint> (d, l));
        bas->knob (Y (l, LY_SUB), "Sub"); bas->knob (Y (l, LY_H2), "Oberton 2"); bas->knob (Y (l, LY_H3), "Oberton 3"); bas->knob (Y (l, LY_H5), "Oberton 5");
        bas->knob (Y (l, LY_MONO), "Mono bis");

        auto* mix = add ("Ausgang"); mix->headStrip = mix->own (std::make_unique<Strip> (d, modeParam (d.mode, Y (l, LY_PHASE)))); mix->headWidth = 100;
        mix->knob (Y (l, LY_LOWCUT), "Lowcut"); mix->knob (Y (l, LY_PAN), "Pan");
    }
    /** Zeigt, welche Frequenz die letzte Taste auf dieser Ebene wirklich spielt. */
    struct KeyHint : public Ticking
    {
        KeyHint (Ed& ed, int layerIndex) : d (ed), l (layerIndex) {}
        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (theme::well); g.fillRoundedRectangle (b, 6.0f);
            const int key = (int) d.p.rawValue (modeParam (d.mode, Y (l, LY_KEY)));
            const int n = d.p.lastNote.load();
            float hz = 440.0f * std::pow (2.0f, ((float) (n >= 0 ? n : 60) - 69.0f) / 12.0f);
            if (key == 1) { while (hz >= 80.0f) hz *= 0.5f; while (hz < 40.0f) hz *= 2.0f; }
            if (key == 2) hz = 440.0f * std::pow (2.0f, (d.p.rawValue (modeParam (d.mode, Y (l, LY_ROOT))) - 69.0f) / 12.0f);
            const char* t = key == 1 ? "Bass-Lage: Jede Taste landet zwischen 40 und 80 Hz" : key == 2 ? "Fest: Spielt immer dieselbe Tonhöhe" : "Normal: Spielt die echte Tonhöhe der Taste";
            g.setColour (theme::ink); g.setFont (theme::font (16.0f, true));
            g.drawText (hzText (hz), b.withTrimmedBottom (b.getHeight() * 0.42f), juce::Justification::centred, false);
            g.setColour (theme::dim); g.setFont (theme::font (12.5f));
            g.drawFittedText (U (t), b.withTrimmedTop (b.getHeight() * 0.58f).toNearestInt().reduced (6, 0), juce::Justification::centred, 2, 0.8f);
        }
        void tick() override { repaint(); }
        Ed& d; int l;
    };
    void resized() override
    {
        auto b = getLocalBounds();
        auto row1 = b.removeFromTop (b.getHeight() / 2); b.removeFromTop (4); row1.removeFromBottom (0);
        const auto place = [] (Panel& p, juce::Rectangle<int>& r, int w) { p.setBounds (w > 0 ? r.removeFromLeft (w).withTrimmedRight (4) : r); };
        place (*panels[0], row1, 560); place (*panels[1], row1, 380); place (*panels[2], row1, 0);
        place (*panels[3], b, 300); place (*panels[4], b, 390); place (*panels[5], b, 400); place (*panels[6], b, 0);
    }
    void tick() override
    {
        for (auto& p : panels) p->tick();
        for (auto& p : panels) for (auto* c : p->getChildren()) if (auto* k = dynamic_cast<KeyHint*> (c)) k->tick();
        const auto v = [&] (int p) { return d.p.rawValue (modeParam (d.mode, Y (l, p))); };
        const bool off = v (LY_MUTE) > 0.5f;
        for (auto& p : panels) p->setAlpha (off ? 0.45f : 1.0f);
        const bool sample = v (LY_SRC) > 0.5f;
        for (auto* k : waveKnobs) k->setAlpha (sample ? 0.3f : 1.0f);
        waveKnobs[1]->setAlpha (sample || (int) v (LY_WARP) == 0 ? 0.3f : 1.0f);
        const bool fOff = (int) v (LY_FTYPE) == 9;
        for (auto* k : filterKnobs) k->setAlpha (fOff ? 0.3f : 1.0f);
        toneKnob->setAlpha (v (LY_DRV) <= 0.0f ? 0.3f : 1.0f);
        boostKnob->setAlpha (v (LY_PREAMT) <= 0.0f ? 0.3f : 1.0f);
        susKnob->setAlpha (v (LY_HOLD) > 0.5f ? 1.0f : 0.3f);
    }
private:
    Ed& d; int l;
    std::vector<std::unique_ptr<Panel>> panels;
    Knob* waveKnobs[5] {}; Knob* filterKnobs[4] {}; Knob* toneKnob = nullptr; Knob* susKnob = nullptr; Knob* boostKnob = nullptr;
};

// ---------------- Seite: Synth ---------------------------------------------
class SynthPage : public Ticking
{
public:
    SynthPage (Ed& ed, std::function<int (std::vector<float>&, std::vector<float>&, float&)> src, std::function<void (std::vector<float>&)> spec)
        : d (ed), wave (d, src), spectrum (d, spec), env (d), lfo (d)
    {
        for (int l = 0; l < NUM_LAYERS; ++l) { rows.push_back (std::make_unique<LayerStrip> (d, l)); addAndMakeVisible (*rows.back()); }
        layer = std::make_unique<LayerEditor> (d, juce::jlimit (0, NUM_LAYERS - 1, d.e.layerTab)); addAndMakeVisible (*layer);
        addAndMakeVisible (env); addAndMakeVisible (lfo); addAndMakeVisible (spectrum);
        if (modeIsMono (d.mode)) addAndMakeVisible (wave);
        else { chord = std::make_unique<ChordBox> (d); addAndMakeVisible (*chord); }
    }
    struct ChordBox : public Ticking
    {
        explicit ChordBox (Ed& ed) : disp (ed), strip (ed, modeParam (ed.mode, M_CHORD), 5) { addAndMakeVisible (disp); addAndMakeVisible (strip); }
        void resized() override { auto b = getLocalBounds(); strip.setBounds (b.removeFromBottom (44)); b.removeFromBottom (3); disp.setBounds (b); }
        void tick() override { disp.tick(); strip.tick(); }
        ChordDisp disp; Strip strip;
    };
    void resized() override
    {
        auto b = getLocalBounds();
        auto bottom = b.removeFromBottom (268); b.removeFromBottom (6);
        auto mix = b.removeFromTop (70); b.removeFromTop (4);
        const int w = mix.getWidth() / NUM_LAYERS;
        for (auto& r : rows) r->setBounds (mix.removeFromLeft (w).withTrimmedRight (4));
        layer->setBounds (b);
        env.setBounds (bottom.removeFromLeft (380)); bottom.removeFromLeft (6);
        lfo.setBounds (bottom.removeFromLeft (500)); bottom.removeFromLeft (6);
        auto top = bottom.removeFromTop (bottom.getHeight() / 2 - 3); bottom.removeFromTop (6);
        wave.setBounds (top); if (chord != nullptr) chord->setBounds (top);
        spectrum.setBounds (bottom);
    }
    void tick() override
    {
        for (auto& r : rows) r->tick();
        layer->tick(); env.tick(); lfo.tick(); spectrum.tick();
        if (chord != nullptr) chord->tick(); else wave.tick();
    }
private:
    Ed& d; WaveShapeView wave; Spectrum spectrum; EnvPanel env; LfoPanel lfo;
    std::vector<std::unique_ptr<LayerStrip>> rows; std::unique_ptr<LayerEditor> layer; std::unique_ptr<ChordBox> chord;
};

// ---------------- Seite: FX ------------------------------------------------
class FxVisual : public Ticking
{
public:
    FxVisual (Ed& ed, int slotIndex) : d (ed), slot (slotIndex) {}
    /** Liegt die Anzeige ueber dem Spektrum, entfaellt der Hintergrund und Baender lassen sich ziehen. */
    void attachTo (Spectrum* s) { link = s; setOpaque (false); }
    bool hitTest (int x, int y) override { return link == nullptr || ! link->tabs().contains (x, y); }
    Rectangle<float> area() const
    {
        if (link != nullptr) return link->plot();
        return getLocalBounds().toFloat().reduced (10.0f);
    }
    juce::Point<float> handle (int band) const
    {
        const auto b = area();
        if (type() == FX_FILTER) return { b.getX() + logX (v (1)) * b.getWidth(), b.getY() + (1.0f - v (2)) * b.getHeight() };
        const int fi = band * 2 + 1, gi = band * 2;
        return { b.getX() + logX (v (fi)) * b.getWidth(), b.getBottom() - (0.5f + v (gi) / 60.0f) * b.getHeight() };
    }
    int handles() const { return type() == FX_EQ ? 3 : type() == FX_FILTER ? 1 : 0; }
    void setReal (int k, float value)
    {
        d.p.param (slotParam (slot, S_P1 + k))->setValueNotifyingHost (fxNorm (fxInfo (type())[k], value));
    }
    void mouseMove (const juce::MouseEvent& e) override { if (link != nullptr) link->setHover (e.getPosition()); }
    void mouseExit (const juce::MouseEvent&) override { if (link != nullptr) link->clearHover(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabbed = -1; float best = 18.0f;
        for (int i = 0; i < handles(); ++i)
            if (const float dist = handle (i).getDistanceFrom (e.position); dist < best) { best = dist; grabbed = i; }
        if (grabbed < 0) return;
        for (int k = 0; k < FX_PARAMS; ++k) d.p.param (slotParam (slot, S_P1 + k))->beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (grabbed < 0) return;
        const auto b = area();
        const float nx = juce::jlimit (0.0f, 1.0f, (e.position.x - b.getX()) / b.getWidth());
        const float ny = juce::jlimit (0.0f, 1.0f, (b.getBottom() - e.position.y) / b.getHeight());
        if (type() == FX_FILTER) { setReal (1, xHz (nx)); setReal (2, ny); }
        else { setReal (grabbed * 2 + 1, xHz (nx)); setReal (grabbed * 2, (ny - 0.5f) * 60.0f); }
        if (link != nullptr) link->setHover (e.getPosition());
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (grabbed < 0) return;
        for (int k = 0; k < FX_PARAMS; ++k) d.p.param (slotParam (slot, S_P1 + k))->endChangeGesture();
        grabbed = -1;
    }
    float v (int k) const { return fxReal (fxInfo (type())[k], d.p.rawValue (slotParam (slot, S_P1 + k))); }
    int type() const { return (int) d.p.rawValue (slotParam (slot, S_TYPE)); }
    void tick() override
    {
        float sum = (float) type();
        for (int k = 0; k < FX_PARAMS; ++k) sum += d.p.rawValue (slotParam (slot, S_P1 + k)) * (float) (k + 2);
        const bool moving = type() == FX_CHORUS || type() == FX_FLANGER || type() == FX_PHASER || type() == FX_FILTER;
        if (moving) phase += 0.033f * v (type() == FX_FILTER ? 4 : 0);
        if (sum != last || moving) { last = sum; repaint(); }
    }
    void paint (Graphics& g) override
    {
        auto o = getLocalBounds().toFloat();
        if (link == nullptr) { g.setColour (theme::well); g.fillRoundedRectangle (o, 6.0f); }
        auto b = area(); const int t = type();
        Path p;
        const auto curve = [&] (std::function<float (float)> fn)   // fn: x 0..1 -> y 0..1
        {
            for (int i = 0; i <= 240; ++i)
            {
                const float x = (float) i / 240.0f, y = b.getBottom() - juce::jlimit (-0.02f, 1.02f, fn (x)) * b.getHeight();
                if (i == 0) p.startNewSubPath (b.getX() + x * b.getWidth(), y); else p.lineTo (b.getX() + x * b.getWidth(), y);
            }
        };
        if (link == nullptr) { g.setColour (theme::line); g.drawHorizontalLine ((int) b.getCentreY(), b.getX(), b.getRight()); }
        switch (t)
        {
            case FX_DIST:       { const float gn = dbToGain (v (0)); curve ([&] (float x) { return 0.5f + 0.46f * shape ((x * 2 - 1) * gn, (int) v (1), v (3)); }); break; }
            case FX_DESTRUCTOR: { const float gn = dbToGain (v (0)), fo = v (1); curve ([&] (float x) { const float i = (x * 2 - 1) * gn, s = softClip (i); return 0.5f + 0.46f * (s + fo * (std::sin (i * 2.2f) - s)); }); break; }
            case FX_CLIP:       { const float gn = dbToGain (v (0)), ce = dbToGain (v (1)), s = v (2); curve ([&] (float x) { const float i = (x * 2 - 1) * gn / ce; return 0.5f + 0.46f * ce * (juce::jlimit (-1.0f, 1.0f, i) * (1 - s) + softClip (i) * s); }); break; }
            case FX_CRUSH:      { const float st = std::pow (2.0f, v (0) - 1); curve ([&] (float x) { return 0.5f + 0.46f * std::round ((x * 2 - 1) * st) / st; }); break; }
            case FX_FILTER:     { const float fc = v (1) * std::pow (2.0f, std::sin (phase * 6.283f) * v (5) * 3.0f);
                                  curve ([&] (float x) { return 0.72f + 20.0f * std::log10 (svfMag ((int) v (0), xHz (x), fc, v (2)) + 1e-4f) / 60.0f; }); break; }
            case FX_EQ:         { Biquad a, m, h; const float sr = 44100; a.set (Biquad::LOWSHELF, v (1), v (0), 0.7f, sr); m.set (Biquad::PEAK, v (3), v (2), 1, sr); h.set (Biquad::HIGHSHELF, v (5), v (4), 0.7f, sr);
                                  const auto mag = [&] (const Biquad& q, float f) { const float w = 6.2831853f * f / sr, c1 = std::cos (w), c2 = std::cos (2 * w);
                                      const float n = q.b0 * q.b0 + q.b1 * q.b1 + q.b2 * q.b2 + 2 * (q.b0 * q.b1 + q.b1 * q.b2) * c1 + 2 * q.b0 * q.b2 * c2;
                                      const float e = 1 + q.a1 * q.a1 + q.a2 * q.a2 + 2 * (q.a1 + q.a1 * q.a2) * c1 + 2 * q.a2 * c2; return 10.0f * std::log10 (n / e); };
                                  curve ([&] (float x) { const float f = xHz (x); return 0.5f + (mag (a, f) + mag (m, f) + mag (h, f)) / 60.0f; }); break; }
            case FX_CHORUS: case FX_FLANGER: case FX_PHASER:
                                { const float dp = juce::jlimit (0.05f, 1.0f, v (1)); curve ([&] (float x) { return 0.5f + 0.42f * dp * std::sin (6.283f * (x * 2 + phase)); });
                                  Path q (p); g.setColour (d.acc.withAlpha (0.3f)); g.strokePath (q, juce::PathStrokeType (1.5f), juce::AffineTransform::translation (b.getWidth() * 0.05f, 0)); break; }
            case FX_DELAY:      { float lvl = 1; const float fb = v (1); const int n = juce::jlimit (2, 14, (int) (1800.0f / juce::jmax (40.0f, v (0)))); g.setColour (d.acc);
                                  for (int i = 0; i < 14 && i < n + 8; ++i) { const float x = b.getX() + b.getWidth() * (float) i / (float) n; if (x > b.getRight()) break; g.fillRoundedRectangle (x, b.getBottom() - lvl * b.getHeight(), 6.0f, lvl * b.getHeight(), 2.0f); lvl *= fb; } break; }
            case FX_REVERB:     { const float tau = 0.08f + v (0) * 0.5f; curve ([&] (float x) { return std::exp (-x / tau) * 0.95f; }); break; }
            case FX_SIDECHAIN:  { const float a = v (2) / 600.0f, l = v (3) / 600.0f, de = v (1);
                                  curve ([&] (float x) { const float t = std::fmod (x * 2.0f, 1.0f) * 0.5f; float e = t < a ? t / a : t < a + l ? 1 - (t - a) / l : 0; e = e * e * (3 - 2 * e); return 0.95f * (1 - de * e); }); break; }
            case FX_COMP:       { curve ([&] (float x) { const float in = -60.0f * (1 - x), ov = in - v (0), o2 = ov > 0 ? v (0) + ov / v (1) : in; return 1.0f + o2 / 60.0f; }); break; }
            default:
                g.setColour (theme::dim); g.setFont (theme::font (16.0f));
                if (type() == FX_OFF) g.drawText (U ("Wähle oben einen Effekt für diesen Slot."), b, juce::Justification::centred, false);
                else g.drawFittedText (U (fxHint (type())), b.toNearestInt().reduced (20), juce::Justification::centred, 3, 0.9f);
                return;
        }
        if (! p.isEmpty()) { g.setColour (d.acc); g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved)); }
        for (int i = 0; i < handles(); ++i)
        {
            const auto c = handle (i);
            g.setColour (theme::ink); g.fillEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f);
            g.setColour (d.acc); g.fillEllipse (c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
        }
    }
private:
    Ed& d; int slot; float last = -1, phase = 0; Spectrum* link = nullptr; int grabbed = -1;
};

class FxRow : public Ticking
{
public:
    FxRow (Ed& ed, int slotIndex, int firstSlot, int countSlots, std::function<void (int)> sel, std::function<void (int, int)> mv) : d (ed), slot (slotIndex), first (firstSlot), count (countSlots), select (std::move (sel)), move (std::move (mv)) {}
    void paint (Graphics& g) override
    {
        const int t = (int) d.p.rawValue (slotParam (slot, S_TYPE)); const bool on = d.p.rawValue (slotParam (slot, S_ON)) > 0.5f, sel = d.e.selectedSlot == slot;
        auto b = getLocalBounds().toFloat();
        g.setColour (sel ? d.acc.withAlpha (0.22f) : theme::plateHi.withAlpha (0.5f)); g.fillRoundedRectangle (b, 6.0f);
        if (sel) { g.setColour (d.acc); g.fillRoundedRectangle (b.withWidth (4.0f), 2.0f); }
        g.setColour (theme::dim); g.setFont (theme::font (13.0f)); g.drawText (String (slot - first + 1), 12, 0, 18, getHeight(), juce::Justification::centredLeft, false);
        g.setColour (t != 0 && on ? theme::ink : theme::dim); g.setFont (theme::font (15.0f, t != 0));
        g.drawText (U (fxNames()[(size_t) t]), 34, 0, 150, getHeight(), juce::Justification::centredLeft, false);
        auto led = powerArea().toFloat().withSizeKeepingCentre (16, 16);
        g.setColour (on ? d.acc : theme::well); g.fillEllipse (led);
        const float mix = d.p.rawValue (slotParam (slot, S_MIX));
        Rectangle<float> bar (34.0f, (float) getHeight() - 9.0f, (float) getWidth() - 80.0f, 3.0f);
        g.setColour (theme::well); g.fillRect (bar); if (t != 0) { g.setColour (d.acc); g.fillRect (bar.withWidth (bar.getWidth() * mix)); }
    }
    Rectangle<int> powerArea() const { return getLocalBounds().removeFromRight (40); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (powerArea().contains (e.getPosition()))
        {
            PRef r { d.p.param (slotParam (slot, S_ON)) };
            r.begin(); r.setNorm (r.real() > 0.5f ? 0.0f : 1.0f); r.end(); repaint();
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (powerArea().contains (e.getMouseDownPosition())) return;
        if (! e.mouseWasDraggedSinceMouseDown()) { select (slot); return; }
        const int to = juce::jlimit (first, first + count - 1, slot + (int) std::floor ((float) e.y / (float) getHeight()));
        if (to != slot) move (slot, to);
    }
    void tick() override
    {
        float sum = 0; for (int k : { S_ON, S_TYPE, S_MIX }) sum = sum * 7 + d.p.rawValue (slotParam (slot, k));
        if (sum != last) { last = sum; repaint(); }
    }
private:
    Ed& d; int slot, first, count; float last = -1; std::function<void (int)> select; std::function<void (int, int)> move;
};

/** Effektauswahl als Raster: Beim Wechsel starten die Regler mit sinnvollen Werten. */
class FxTypeGrid : public Ticking
{
public:
    FxTypeGrid (Ed& ed, int slotIndex, std::function<void()> changed) : d (ed), slot (slotIndex), onChange (std::move (changed)) {}
    void paint (Graphics& g) override
    {
        const int cur = (int) d.p.rawValue (slotParam (slot, S_TYPE));
        const float w = (float) getWidth() / 8.0f, h = (float) getHeight() / 3.0f;
        for (int i = 0; i < FX_TYPES; ++i)
        {
            Rectangle<float> r ((float) (i % 8) * w, (float) (i / 8) * h, w, h); r.reduce (2, 2);
            g.setColour (i == cur ? d.acc : theme::well); g.fillRoundedRectangle (r, 5.0f);
            g.setColour (i == cur ? theme::well : theme::ink); g.setFont (theme::font (13.5f, i == cur));
            g.drawText (U (fxNames()[(size_t) i]), r, juce::Justification::centred, false);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, FX_TYPES - 1, e.y * 3 / getHeight() * 8 + e.x * 8 / getWidth());
        PRef t { d.p.param (slotParam (slot, S_TYPE)) }; t.begin(); t.setReal ((float) i); t.end();
        const FxP* info = fxInfo (i);
        for (int k = 0; k < FX_PARAMS; ++k) { PRef r { d.p.param (slotParam (slot, S_P1 + k)) }; r.begin(); r.setReal (fxNorm (info[k], info[k].def)); r.end(); }
        PRef m { d.p.param (slotParam (slot, S_MIX)) }; m.begin(); m.setReal (fxDefaultMix (i)); m.end();
        onChange();
    }
private:
    Ed& d; int slot; std::function<void()> onChange;
};

class FxPage : public Ticking
{
public:
    explicit FxPage (Ed& ed) : d (ed)
    {
        const int scope = d.e.fxScope;
        const int firstSlot = scope == 0 ? 0 : layerSlot (scope - 1, 0), countSlots = scope == 0 ? NUM_MASTER_SLOTS : SLOTS_PER_LAYER;
        if (d.e.selectedSlot < firstSlot || d.e.selectedSlot >= firstSlot + countSlots) d.e.selectedSlot = firstSlot;
        for (int i = 0; i <= NUM_LAYERS; ++i)
        {
            auto b = std::make_unique<juce::TextButton> (i == 0 ? String ("Master") : U (layerName (d.mode, i - 1)));
            b->setColour (juce::TextButton::buttonColourId, i == scope ? d.acc : theme::well);
            b->setColour (juce::TextButton::textColourOffId, i == scope ? theme::well : theme::ink);
            b->setColour (juce::ComboBox::outlineColourId, theme::line);
            b->onClick = [this, i] { d.e.fxScope = i; d.e.requestRebuild(); };
            addAndMakeVisible (*b); scopeTabs.push_back (std::move (b));
        }
        for (int s = firstSlot; s < firstSlot + countSlots; ++s)
        {
            rows.push_back (std::make_unique<FxRow> (d, s, firstSlot, countSlots, [this] (int x) { d.e.selectedSlot = x; d.e.requestRebuild(); },
                                                     [this] (int a, int b) { swap (a, b); }));
            addAndMakeVisible (*rows.back());
        }
        const int s = d.e.selectedSlot;
        grid = std::make_unique<FxTypeGrid> (d, s, [this] { d.e.requestRebuild(); });
        visual = std::make_unique<FxVisual> (d, s);
        addAndMakeVisible (*grid);
        const int t = (int) d.p.rawValue (slotParam (s, S_TYPE));
        if (t == FX_EQ || t == FX_FILTER)
        {
            analyzer = std::make_unique<Spectrum> (d, [] (std::vector<float>&) {});
            addAndMakeVisible (*analyzer);
            visual->attachTo (analyzer.get());
        }
        addAndMakeVisible (*visual);
        if (t != 0)
        {
            for (int k = 0; k < FX_PARAMS; ++k)
            {
                const auto* info = fxInfo (t);
                if (info[k].label[0] == 0) continue;
                knobs.push_back (std::make_unique<Knob> (d, slotParam (s, S_P1 + k), U (info[k].label)));
                addAndMakeVisible (*knobs.back());
            }
            knobs.push_back (std::make_unique<Knob> (d, slotParam (s, S_MIX), "Mix"));
            addAndMakeVisible (*knobs.back());
        }
    }
    void swap (int a, int b)
    {
        for (int k = 0; k < S_COUNT; ++k)
        {
            PRef x { d.p.param (slotParam (a, k)) }, y { d.p.param (slotParam (b, k)) };
            const float vx = x.norm(), vy = y.norm();
            x.begin(); y.begin(); x.setNorm (vy); y.setNorm (vx); x.end(); y.end();
        }
        d.e.selectedSlot = b;
        d.e.requestRebuild();
    }
    void resized() override
    {
        auto b = getLocalBounds();
        auto left = b.removeFromLeft (290); b.removeFromLeft (10);
        auto tabs = left.removeFromTop (34); const int tw = tabs.getWidth() / (int) scopeTabs.size();
        for (auto& t : scopeTabs) t->setBounds (tabs.removeFromLeft (tw).withTrimmedRight (3));
        left.removeFromTop (6);
        const int h = juce::jmin (84, left.getHeight() / NUM_MASTER_SLOTS);
        for (auto& r : rows) r->setBounds (left.removeFromTop (h).reduced (0, 3));
        grid->setBounds (b.removeFromTop (126)); b.removeFromTop (8);
        auto row = b.removeFromBottom (92);
        if (! knobs.empty()) { const int w = row.getWidth() / 7; for (auto& k : knobs) k->setBounds (row.removeFromLeft (w)); }
        b.removeFromBottom (6);
        if (analyzer != nullptr) analyzer->setBounds (b);
        visual->setBounds (b);
    }
    void paint (Graphics&) override {}
private:
    Ed& d;
    std::vector<std::unique_ptr<FxRow>> rows;
    std::vector<std::unique_ptr<juce::TextButton>> scopeTabs;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::unique_ptr<FxTypeGrid> grid; std::unique_ptr<Spectrum> analyzer; std::unique_ptr<FxVisual> visual;
};

// ---------------- Seite: EQ ------------------------------------------------
static const char* const eqKindNames[8] = { "Glocke", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Bandpass", "Tilt" };

/** Kurve und Punkte ueber dem Analyzer. Ziehen: Frequenz und Gain, Mausrad: Q, Doppelklick: Band an oder aus. */
class EqGraph : public Ticking
{
public:
    EqGraph (Ed& ed, Spectrum& s, int eqIndex) : d (ed), spec (s), eq (eqIndex) { setOpaque (false); }
    bool hitTest (int x, int y) override { return ! spec.tabs().contains (x, y); }
    PRef ref (int band, int p) const { return { d.p.param (eqParam (eq, band, p)) }; }
    void read (int band, float* out) const { for (int k = 0; k < E_COUNT; ++k) out[k] = d.p.rawValue (eqParam (eq, band, k)); }
    float range() const { return d.e.eqRange == 0 ? 15.0f : 30.0f; }
    Rectangle<float> area() const { return spec.plot(); }
    float yOf (float db) const { const auto b = area(); return juce::jlimit (b.getY(), b.getBottom(), b.getCentreY() - db / range() * b.getHeight() * 0.48f); }
    static bool flat (int kind) { return kind == EQ_LOWCUT || kind == EQ_HIGHCUT || kind == EQ_NOTCH || kind == EQ_BANDPASS; }
    juce::Point<float> node (int band) const
    {
        float q[E_COUNT]; read (band, q); const auto b = area();
        return { b.getX() + logX (q[E_FREQ]) * b.getWidth(), yOf (flat ((int) q[E_TYPE]) ? 0.0f : q[E_GAIN]) };
    }
    int nodeAt (juce::Point<int> p) const
    {
        int best = -1; float dist = 14.0f;
        for (int i = 0; i < EQ_BANDS; ++i)
        {
            if (d.p.rawValue (eqParam (eq, i, E_ON)) < 0.5f) continue;
            const float r = node (i).getDistanceFrom (p.toFloat());
            if (r < dist) { dist = r; best = i; }
        }
        return best;
    }
    void paint (Graphics& g) override
    {
        const auto b = area(); const float sr = juce::jmax (22050.0f, (float) d.p.currentSampleRate());
        g.setColour (theme::ink.withAlpha (0.35f)); g.fillRect (b.getX(), yOf (0.0f), b.getWidth(), 1.0f);
        g.setFont (theme::font (11.5f));
        for (float db : { -0.66f, -0.33f, 0.33f, 0.66f })
        {
            const float v = db * range();
            g.setColour (theme::line.withAlpha (0.5f)); g.fillRect (b.getX(), yOf (v), b.getWidth(), 1.0f);
            g.setColour (theme::dim); g.drawText (String ((int) std::round (v)), Rectangle<float> (b.getRight() - 30, yOf (v) - 13, 28, 13), juce::Justification::centredRight, false);
        }
        float q[EQ_BANDS][E_COUNT]; for (int i = 0; i < EQ_BANDS; ++i) read (i, q[i]);
        const auto curve = [&] (int only)
        {
            Path p;
            for (int i = 0; i <= 320; ++i)
            {
                const float x = (float) i / 320.0f, hz = xHz (x); float db = 0;
                for (int k = 0; k < EQ_BANDS; ++k)
                    if (q[k][E_ON] > 0.5f && (only < 0 || only == k)) db += bandDb (q[k], juce::jmin (hz, sr * 0.49f), sr);
                const float px = b.getX() + x * b.getWidth();
                if (i == 0) p.startNewSubPath (px, yOf (db)); else p.lineTo (px, yOf (db));
            }
            return p;
        };
        const int sel = d.e.eqBand;
        if (q[sel][E_ON] > 0.5f)
        {
            auto p = curve (sel), f = p;
            f.lineTo (b.getRight(), yOf (0)); f.lineTo (b.getX(), yOf (0)); f.closeSubPath();
            g.setColour (d.acc.withAlpha (0.18f)); g.fillPath (f);
            g.setColour (d.acc.withAlpha (0.55f)); g.strokePath (p, juce::PathStrokeType (1.2f));
        }
        g.setColour (d.acc); g.strokePath (curve (-1), juce::PathStrokeType (2.4f));
        for (int i = 0; i < EQ_BANDS; ++i)
        {
            if (q[i][E_ON] < 0.5f) continue;
            const auto c = node (i);
            if (std::fabs (q[i][E_DYN]) > 0.01f)
            {   // Dynamik: Strich zeigt, wie weit das Band fahren kann
                g.setColour (theme::hot); g.fillRect (c.x - 1.5f, juce::jmin (c.y, yOf (q[i][E_GAIN] + q[i][E_DYN])), 3.0f, std::fabs (yOf (q[i][E_GAIN] + q[i][E_DYN]) - c.y));
            }
            const bool on = i == sel;
            g.setColour (on ? d.acc : theme::ink); g.fillEllipse (c.x - 9.0f, c.y - 9.0f, 18.0f, 18.0f);
            g.setColour (theme::well); g.setFont (theme::font (12.5f, true));
            g.drawText (String (i + 1), Rectangle<float> (c.x - 9.0f, c.y - 9.0f, 18.0f, 18.0f), juce::Justification::centred, false);
        }
        const auto* s = q[sel];
        if (s[E_ON] > 0.5f)
        {
            g.setColour (theme::ink); g.setFont (theme::font (13.0f, true));
            g.drawText (String (sel + 1) + "  " + U (eqKindNames[(int) s[E_TYPE]]) + "  " + hzText (s[E_FREQ]) + "  " + String (s[E_GAIN], 1) + " dB  Q " + String (s[E_Q], 2),
                        Rectangle<float> (b.getX() + 8, b.getBottom() - 22, 420, 18), juce::Justification::centredLeft, false);
        }
    }
    void select (int band) { if (band != d.e.eqBand) { d.e.eqBand = band; bandChanged = true; } }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int n = nodeAt (e.getPosition());
        spec.clearHover();
        if (n < 0) return;
        select (n);
        if (e.mods.isPopupMenu()) { menu (n); return; }
        grabbed = n; auto f = ref (n, E_FREQ), a = ref (n, E_GAIN); f.begin(); a.begin();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (grabbed < 0) return;
        const auto b = area();
        ref (grabbed, E_FREQ).setReal (juce::jlimit (20.0f, 20000.0f, xHz (juce::jlimit (0.0f, 1.0f, ((float) e.x - b.getX()) / b.getWidth()))));
        ref (grabbed, E_GAIN).setReal (juce::jlimit (-30.0f, 30.0f, (b.getCentreY() - (float) e.y) / (b.getHeight() * 0.48f) * range()));
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (grabbed >= 0) { auto f = ref (grabbed, E_FREQ), a = ref (grabbed, E_GAIN); f.end(); a.end(); grabbed = -1; }
        if (bandChanged) { bandChanged = false; d.e.requestRebuild(); }
    }
    void mouseMove (const juce::MouseEvent& e) override { spec.setHover (e.getPosition()); }
    void mouseExit (const juce::MouseEvent&) override { spec.clearHover(); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int n = nodeAt (e.getPosition()), t = n >= 0 ? n : d.e.eqBand;
        auto q = ref (t, E_Q); q.begin(); q.setReal (juce::jlimit (0.1f, 18.0f, q.real() * std::exp (w.deltaY * 1.2f))); q.end();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int n = nodeAt (e.getPosition());
        if (n >= 0) { auto x = ref (n, E_ON); x.begin(); x.setReal (0); x.end(); return; }
        for (int i = 0; i < EQ_BANDS; ++i)
            if (d.p.rawValue (eqParam (eq, i, E_ON)) < 0.5f)
            {
                const auto b = area(); const float hz = xHz (juce::jlimit (0.0f, 1.0f, ((float) e.x - b.getX()) / b.getWidth()));
                const auto put = [&] (int p, float v) { auto x = ref (i, p); x.begin(); x.setReal (v); x.end(); };
                put (E_TYPE, hz < 45.0f ? EQ_LOWCUT : hz > 13000.0f ? EQ_HIGHCUT : EQ_BELL); put (E_FREQ, hz); put (E_GAIN, 0); put (E_Q, 1); put (E_ON, 1);
                d.e.eqBand = i; d.e.requestRebuild();
                return;
            }
    }
    void menu (int band)
    {
        juce::PopupMenu m;
        const int kind = (int) ref (band, E_TYPE).real();
        for (int i = 0; i < 8; ++i) m.addItem (1 + i, U (eqKindNames[i]), true, i == kind);
        m.addSeparator(); m.addItem (20, U ("Band ausschalten"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [sp = SafePointer<EqGraph> (this), band] (int r)
        {
            if (sp == nullptr || r <= 0) return;
            if (r == 20) { auto x = sp->ref (band, E_ON); x.begin(); x.setReal (0); x.end(); }
            else { auto x = sp->ref (band, E_TYPE); x.begin(); x.setReal ((float) (r - 1)); x.end(); }
            sp->d.e.requestRebuild();
        });
    }
private:
    Ed& d; Spectrum& spec; int eq, grabbed = -1; bool bandChanged = false;
};

/** Acht Baender nebeneinander: Punkt schaltet, Flaeche waehlt. */
class EqBar : public Ticking
{
public:
    EqBar (Ed& ed, int eqIndex) : d (ed), eq (eqIndex) {}
    void paint (Graphics& g) override
    {
        const float w = (float) getWidth() / (float) EQ_BANDS;
        for (int i = 0; i < EQ_BANDS; ++i)
        {
            Rectangle<float> r ((float) i * w, 0, w, (float) getHeight()); r.reduce (2, 1);
            const bool sel = i == d.e.eqBand, on = d.p.rawValue (eqParam (eq, i, E_ON)) > 0.5f;
            g.setColour (sel ? d.acc : theme::well); g.fillRoundedRectangle (r, 5.0f);
            g.setColour (on ? (sel ? theme::well : d.acc) : theme::line); g.fillEllipse (r.getX() + 8, r.getCentreY() - 6, 12, 12);
            g.setColour (sel ? theme::well : theme::ink); g.setFont (theme::font (13.0f, sel));
            g.drawFittedText (String (i + 1) + "  " + U (eqKindNames[(int) d.p.rawValue (eqParam (eq, i, E_TYPE))]), r.toNearestInt().withTrimmedLeft (26), juce::Justification::centredLeft, 1, 0.8f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, EQ_BANDS - 1, e.x * EQ_BANDS / juce::jmax (1, getWidth()));
        const int inside = e.x - i * getWidth() / EQ_BANDS;
        if (inside < 26) { PRef x { d.p.param (eqParam (eq, i, E_ON)) }; x.begin(); x.setReal (x.real() > 0.5f ? 0.0f : 1.0f); x.end(); repaint(); }
        if (i != d.e.eqBand) { d.e.eqBand = i; d.e.requestRebuild(); }
    }
    void tick() override
    {
        float sum = 0; for (int i = 0; i < EQ_BANDS; ++i) sum += d.p.rawValue (eqParam (eq, i, E_ON)) * (float) (1 << i) + d.p.rawValue (eqParam (eq, i, E_TYPE)) * 300.0f * (float) (i + 1);
        if (sum != last) { last = sum; repaint(); }
    }
private:
    Ed& d; int eq; float last = -1;
};

class EqPage : public Ticking
{
public:
    explicit EqPage (Ed& ed) : d (ed), eq (juce::jlimit (0, NUM_EQS - 1, ed.e.eqScope)), spec (ed, [] (std::vector<float>&) {}), graph (ed, spec, eq), bar (ed, eq),
        form (ed, eqParam (eq, ed.e.eqBand, E_TYPE), 4), slope (ed, eqParam (eq, ed.e.eqBand, E_SLOPE), 1), stereo (ed, eqParam (eq, ed.e.eqBand, E_STEREO), 1)
    {
        const int band = d.e.eqBand;
        for (int i = 0; i <= NUM_LAYERS; ++i)
        {
            auto b = std::make_unique<juce::TextButton> (i == 0 ? String ("Master") : U (layerName (d.mode, i - 1)));
            b->setColour (juce::TextButton::buttonColourId, i == eq ? d.acc : theme::well);
            b->setColour (juce::TextButton::textColourOffId, i == eq ? theme::well : theme::ink);
            b->setColour (juce::ComboBox::outlineColourId, theme::line);
            b->onClick = [this, i] { d.e.eqScope = i; d.e.requestRebuild(); };
            addAndMakeVisible (*b); tabs.push_back (std::move (b));
        }
        static const char* const names[5] = { "Frequenz", "Gain", "Q", "Dynamik", "Schwelle" };
        static const int ids[5] = { E_FREQ, E_GAIN, E_Q, E_DYN, E_THR };
        for (int i = 0; i < 5; ++i) { knobs.push_back (std::make_unique<Knob> (d, eqParam (eq, band, ids[i]), U (names[i]))); addAndMakeVisible (*knobs.back()); }
        for (auto* c : { (juce::Component*) &spec, (juce::Component*) &graph, (juce::Component*) &bar, (juce::Component*) &form, (juce::Component*) &slope, (juce::Component*) &stereo, (juce::Component*) &rangeBtn, (juce::Component*) &startBtn }) addAndMakeVisible (c);
        const auto style = [this] (juce::TextButton& b, const char* t)
        {
            b.setButtonText (U (t)); b.setColour (juce::TextButton::buttonColourId, theme::well); b.setColour (juce::TextButton::textColourOffId, theme::ink);
            b.setColour (juce::ComboBox::outlineColourId, theme::line);
        };
        style (rangeBtn, d.e.eqRange == 0 ? "Bereich 15 dB" : "Bereich 30 dB"); style (startBtn, "Startpunkt laden");
        rangeBtn.onClick = [this] { d.e.eqRange = 1 - d.e.eqRange; d.e.requestRebuild(); };
        startBtn.onClick = [this] { showStarts(); };
    }
    struct Band { int kind; float hz, gain, q; int slope, stereo; };
    void apply (std::initializer_list<Band> bands)
    {
        int i = 0;
        for (int b = 0; b < EQ_BANDS; ++b) { PRef x { d.p.param (eqParam (eq, b, E_ON)) }; x.begin(); x.setReal (0); x.end(); }
        for (const auto& s : bands)
        {
            const auto put = [&] (int p, float v) { PRef x { d.p.param (eqParam (eq, i, p)) }; x.begin(); x.setReal (v); x.end(); };
            put (E_TYPE, (float) s.kind); put (E_FREQ, s.hz); put (E_GAIN, s.gain); put (E_Q, s.q); put (E_SLOPE, (float) s.slope); put (E_STEREO, (float) s.stereo); put (E_DYN, 0); put (E_ON, 1);
            ++i;
        }
        d.e.eqBand = 0; d.e.requestRebuild();
    }
    void showStarts()
    {
        juce::PopupMenu m;
        m.addItem (1, U ("Kick-Bass: Rumpeln weg, Mitten frei")); m.addItem (2, U ("Topkick: Bass raus, Klick betonen"));
        m.addItem (3, U ("Melo oder Stab: Platz für die Kick")); m.addItem (4, U ("Master: Unten mono, oben Luft"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&startBtn), [sp = SafePointer<EqPage> (this)] (int r)
        {
            if (sp == nullptr) return;
            if (r == 1) sp->apply ({ { EQ_LOWCUT, 28, 0, 1, 2, 0 }, { EQ_BELL, 280, -3, 1.2f, 1, 0 }, { EQ_BELL, 900, 2, 1, 1, 0 } });
            if (r == 2) sp->apply ({ { EQ_LOWCUT, 180, 0, 1, 1, 0 }, { EQ_BELL, 3200, 3, 1, 1, 0 } });
            if (r == 3) sp->apply ({ { EQ_LOWCUT, 160, 0, 1, 1, 0 }, { EQ_BELL, 350, -2, 1.2f, 1, 0 }, { EQ_HIGHSHELF, 8000, 2, 0.7f, 1, 0 } });
            if (r == 4) sp->apply ({ { EQ_LOWCUT, 25, 0, 1, 2, 0 }, { EQ_LOWCUT, 140, 0, 1, 1, ES_SIDE }, { EQ_HIGHSHELF, 11000, 1.5f, 0.7f, 1, 0 } });
        });
    }
    void resized() override
    {
        auto b = getLocalBounds();
        auto t = b.removeFromTop (34);
        startBtn.setBounds (t.removeFromRight (170)); t.removeFromRight (4); rangeBtn.setBounds (t.removeFromRight (130)); t.removeFromRight (10);
        for (size_t i = 0; i < tabs.size(); ++i) tabs[i]->setBounds (t.removeFromLeft (150).withTrimmedRight (3));
        b.removeFromTop (6);
        auto row = b.removeFromBottom (96); b.removeFromBottom (4);
        bar.setBounds (b.removeFromBottom (42)); b.removeFromBottom (4);
        spec.setBounds (b); graph.setBounds (b);
        form.setBounds (row.removeFromLeft (330).reduced (0, 8)); row.removeFromLeft (6);
        slope.setBounds (row.removeFromLeft (80)); row.removeFromLeft (6);
        stereo.setBounds (row.removeFromLeft (80)); row.removeFromLeft (10);
        const int w = row.getWidth() / (int) knobs.size();
        for (auto& k : knobs) k->setBounds (row.removeFromLeft (w));
    }
    void tick() override
    {
        spec.tick(); graph.repaint(); bar.tick(); form.tick(); slope.tick(); stereo.tick();
        for (auto& k : knobs) k->tick();
    }
private:
    Ed& d; int eq; Spectrum spec; EqGraph graph; EqBar bar; Strip form, slope, stereo;
    juce::TextButton rangeBtn, startBtn;
    std::vector<std::unique_ptr<juce::TextButton>> tabs; std::vector<std::unique_ptr<Knob>> knobs;
};

// ---------------- Seite: Sample --------------------------------------------
class SampleWave : public Ticking
{
public:
    explicit SampleWave (Ed& ed) : d (ed), osc (ed.e.sampleOsc), start { ed.p.param (modeParam (ed.mode, Y (ed.e.sampleOsc, LY_START))) }
    {
        if (const auto* s = current()) report = analyzeSample (*s);
    }
    const SampleData* current() const { return d.p.samples[sampleSlot (d.mode, osc)].load(); }
    Rectangle<int> tabs() const { return { 16, 14, 230, 22 }; }
    void buildSonogram (const SampleData& s, int w, int h)
    {
        sono = juce::Image (juce::Image::RGB, w, h, true);
        constexpr int order = 12, N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) N * 2);
        const int n = s.length();
        float top = 1.0e-6f;
        std::vector<std::vector<float>> cols ((size_t) w);
        for (int x = 0; x < w; ++x)
        {
            const int at = (int) ((juce::int64) x * n / w) - N / 2;
            for (int i = 0; i < N; ++i)
            {
                const int k = at + i;
                const float v = k >= 0 && k < n ? s.l[(size_t) k] : 0.0f;
                buf[(size_t) i] = v * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) N));
            }
            std::fill (buf.begin() + N, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data());
            cols[(size_t) x].assign (buf.begin(), buf.begin() + N / 2);
            for (auto v : cols[(size_t) x]) top = juce::jmax (top, v);
        }
        for (int x = 0; x < w; ++x)
            for (int y = 0; y < h; ++y)
            {
                const float hz = xHz (1.0f - (float) y / (float) (h - 1));
                const int bin = juce::jlimit (1, N / 2 - 1, (int) (hz / s.rate * (float) N));
                sono.setPixelAt (x, y, heat (std::pow (juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (cols[(size_t) x][(size_t) bin] / top + 1.0e-5f) + 60.0f) / 60.0f), 1.6f), d.acc));
            }
    }
    void paint (Graphics& g) override
    {
        auto o = getLocalBounds().toFloat();
        g.setColour (theme::well); g.fillRoundedRectangle (o, 6.0f);
        auto b = o.reduced (10.0f);
        const auto* s = current();
        if (s == nullptr || s->length() < 2)
        {
            g.setColour (theme::ink); g.setFont (theme::font (19.0f, true));
            g.drawText (U ("Audiodatei hierher ziehen oder klicken"), b.withTrimmedBottom (26), juce::Justification::centred, false);
            g.setColour (theme::dim); g.setFont (theme::font (14.0f));
            g.drawText (U ("WAV, AIFF, FLAC oder OGG, das Sample gilt für den aktuellen Modus"), b.withTrimmedTop (34), juce::Justification::centred, false);
            return;
        }
        const int cols = (int) b.getWidth(), n = s->length();
        if (d.e.sampleView == 1)
        {
            if (sono.isNull() || sono.getWidth() != cols) buildSonogram (*s, cols, (int) b.getHeight());
            g.drawImage (sono, b);
            g.setColour (theme::ink.withAlpha (0.85f)); g.setFont (theme::font (12.0f));
            for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            {
                const float y = b.getBottom() - logX (hz) * b.getHeight();
                g.setColour (theme::well.withAlpha (0.75f)); g.fillRoundedRectangle (b.getX() + 2, y - 8, 56, 16, 3.0f);
                g.setColour (theme::ink);
                g.drawText (hz >= 1000 ? String ((int) (hz / 1000)) + " kHz" : String ((int) hz) + " Hz", Rectangle<float> (b.getX() + 6, y - 8, 52, 16), juce::Justification::centredLeft, false);
            }
            if (report.tonal)
            {
                const float y = b.getBottom() - logX (report.hz) * b.getHeight();
                g.setColour (theme::ink); g.fillRect (b.getX(), y, b.getWidth(), 1.0f);
            }
        }
        g.setColour (theme::line);
        if (d.e.sampleView == 0) g.drawHorizontalLine ((int) b.getCentreY(), b.getX(), b.getRight());
        g.setColour (d.acc);
        for (int x = 0; x < cols && d.e.sampleView == 0; ++x)
        {
            float lo = 0, hi = 0;
            const int a = (int) ((juce::int64) x * n / cols), e = juce::jmax (a + 1, (int) ((juce::int64) (x + 1) * n / cols));
            const int step = juce::jmax (1, (e - a) / 40);
            for (int i = a; i < e; i += step) { lo = juce::jmin (lo, s->l[(size_t) i]); hi = juce::jmax (hi, s->l[(size_t) i]); }
            g.drawVerticalLine ((int) b.getX() + x, b.getCentreY() - hi * b.getHeight() * 0.48f, b.getCentreY() - lo * b.getHeight() * 0.48f + 1.0f);
        }
        const float sx = b.getX() + start.norm() * b.getWidth();
        g.setColour (theme::well.withAlpha (0.65f)); g.fillRect (b.withRight (sx));
        g.setColour (theme::ink); g.fillRect (sx - 1.0f, b.getY(), 2.0f, b.getHeight());
        g.setFont (theme::font (13.0f));
        const String tone = report.tonal ? U ("   Grundton ") + hzText (report.hz) : String();
        g.drawText (U (s->name) + "   " + String ((float) n / s->rate, 2) + " s" + tone, b.removeFromTop (16.0f), juce::Justification::topRight, true);

        static const char* names[] = { "Wellenform", "Spektrogramm" };
        auto t = tabs();
        for (int i = 0; i < 2; ++i)
        {
            auto r = t.removeFromLeft (t.getWidth() / (2 - i)).reduced (1, 0).toFloat();
            g.setColour (i == d.e.sampleView ? d.acc : theme::plateHi); g.fillRoundedRectangle (r, 4.0f);
            g.setColour (i == d.e.sampleView ? theme::well : theme::ink); g.setFont (theme::font (12.5f, i == d.e.sampleView));
            g.drawText (names[i], r, juce::Justification::centred, false);
        }
        if (hover.x >= 0 && d.e.sampleView == 1)
        {
            auto pl = getLocalBounds().toFloat().reduced (10.0f);
            const float hz = xHz (juce::jlimit (0.0f, 1.0f, (pl.getBottom() - (float) hover.y) / pl.getHeight()));
            g.setColour (theme::ink.withAlpha (0.5f)); g.fillRect (pl.getX(), (float) hover.y, pl.getWidth(), 1.0f);
            Rectangle<float> tag (juce::jmin ((float) hover.x + 10.0f, pl.getRight() - 128.0f), juce::jmax (pl.getY() + 28.0f, (float) hover.y - 26.0f), 124.0f, 22.0f);
            g.setColour (theme::well); g.fillRoundedRectangle (tag, 4.0f);
            g.setColour (theme::ink); g.setFont (theme::font (13.0f, true));
            g.drawText (hzText (hz), tag, juce::Justification::centred, false);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { hover = e.getPosition(); if (d.e.sampleView == 1) repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = { -1, -1 }; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto* s = current();
        if (s == nullptr) { d.e.chooseSample (osc); return; }
        if (tabs().contains (e.getPosition()))
        {
            d.e.sampleView = e.x < tabs().getCentreX() ? 0 : 1;
            repaint(); return;
        }
        dragging = d.e.sampleView == 0;
        if (dragging) { start.begin(); mouseDrag (e); }
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging) start.setNorm ((float) (e.x - 10) / (float) (getWidth() - 20)); }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { start.end(); dragging = false; } }
    void tick() override { const float v = start.norm(); if (v != last) { last = v; repaint(); } }
private:
    Ed& d; int osc; PRef start; float last = -1; SampleReport report; juce::Image sono;
    juce::Point<int> hover { -1, -1 }; bool dragging = false;
};

static void styleButton (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId, theme::well); b.setColour (juce::TextButton::textColourOffId, theme::ink);
    b.setColour (juce::ComboBox::outlineColourId, theme::line);
}

class SamplePage : public Ticking
{
public:
    explicit SamplePage (Ed& ed) : d (ed), wave (ed), osc (ed.e.sampleOsc), pitch (ed, modeParam (ed.mode, Y (ed.e.sampleOsc, LY_PITCH))), loop (ed, modeParam (ed.mode, Y (ed.e.sampleOsc, LY_LOOP))), len (ed, modeParam (ed.mode, Y (ed.e.sampleOsc, LY_FREE)))
    {
        addAndMakeVisible (wave); addAndMakeVisible (pitch); addAndMakeVisible (loop); addAndMakeVisible (len);
        for (auto [p, n] : std::initializer_list<std::pair<int, const char*>> { { Y (osc, LY_LVL), "Pegel" }, { Y (osc, LY_TUNE), "Tune" }, { Y (osc, LY_FINE), "Fein" }, { Y (osc, LY_ROOT), "Grundton" }, { Y (osc, LY_START), "Start" } })
        {
            knobs.push_back (std::make_unique<Knob> (d, modeParam (d.mode, p), U (n)));
            addAndMakeVisible (*knobs.back());
        }
        styleButton (load); styleButton (clear);
        load.setButtonText (U ("Sample laden")); clear.setButtonText (U ("Sample entfernen"));
        load.onClick = [this] { d.e.chooseSample (osc); };
        clear.onClick = [this] { d.p.clearSample (d.mode, osc); d.e.status = {}; };
        for (int o = 0; o < NUM_LAYERS; ++o)
        {
            auto b = std::make_unique<juce::TextButton> (U (layerName (d.mode, o)));
            styleButton (*b);
            if (o == osc) { b->setColour (juce::TextButton::buttonColourId, d.acc); b->setColour (juce::TextButton::textColourOffId, theme::well); }
            b->onClick = [this, o] { d.e.sampleOsc = o; d.e.status = {}; d.e.requestRebuild(); };
            addAndMakeVisible (*b); oscTabs.push_back (std::move (b));
        }
        addAndMakeVisible (load); addAndMakeVisible (clear);
    }
    void resized() override
    {
        auto b = getLocalBounds();
        auto tabs = b.removeFromTop (32); b.removeFromTop (6);
        for (auto& t : oscTabs) t->setBounds (tabs.removeFromLeft (150).withTrimmedRight (4));
        auto bottom = b.removeFromBottom (196); b.removeFromBottom (8);
        wave.setBounds (b);
        auto buttons = bottom.removeFromRight (190); buttons.removeFromTop (28);
        load.setBounds (buttons.removeFromTop (34)); buttons.removeFromTop (6); clear.setBounds (buttons.removeFromTop (34));
        bottom.removeFromRight (10);
        auto left = bottom.removeFromLeft (400);
        for (auto* row : { &pitch, &loop, &len })
            row->setBounds (left.removeFromTop (60).withTrimmedTop (26).withWidth (360));
        bottom.removeFromTop (28);
        const int w = bottom.getWidth() / (int) knobs.size();
        for (auto& k : knobs) k->setBounds (bottom.removeFromLeft (w).withHeight (90));
    }
    void paint (Graphics& g) override
    {
        const int y0 = getHeight() - 196;
        g.setColour (theme::ink); g.setFont (theme::font (14.0f, true));
        g.drawText (U ("Tonhöhe folgt"), 0, y0 + 4, 300, 20, juce::Justification::centredLeft, false);
        g.drawText (U ("Loop"), 0, y0 + 64, 300, 20, juce::Justification::centredLeft, false);
        g.drawText (U ("Länge bestimmt"), 0, y0 + 124, 300, 20, juce::Justification::centredLeft, false);
        g.setColour (theme::dim); g.setFont (theme::font (13.0f));
        const String hint = d.e.status.isNotEmpty() ? d.e.status : U ("Jede Ebene hat ihr eigenes Sample. Es läuft durch Filter, Drive und FX und wird im Projekt und im Preset mitgespeichert.");
        g.drawText (hint, 410, y0 + 150, getWidth() - 620, 40, juce::Justification::centredLeft, true);
    }
private:
    Ed& d; SampleWave wave; int osc; Strip pitch, loop, len;
    std::vector<std::unique_ptr<Knob>> knobs; juce::TextButton load, clear;
    std::vector<std::unique_ptr<juce::TextButton>> oscTabs;
};

// ---------------- Seite: Mod -----------------------------------------------
class ModRow : public Ticking
{
public:
    ModRow (Ed& ed, int slotIndex) : d (ed), slot (slotIndex)
    {
        amount.setSliderStyle (juce::Slider::LinearHorizontal);
        amount.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        amount.setColour (juce::Slider::trackColourId, d.acc); amount.setColour (juce::Slider::thumbColourId, theme::ink);
        amount.setColour (juce::Slider::backgroundColourId, theme::well);
        addAndMakeVisible (amount);
        att = std::make_unique<juce::SliderParameterAttachment> (*d.p.param (modSlot (slot, MD_AMOUNT)), amount, nullptr);
        amount.setDoubleClickReturnValue (true, 0.0);
        styleButton (remove); remove.setButtonText ("x");
        remove.onClick = [this] { d.p.disconnect (slot); };
        addAndMakeVisible (remove);
    }
    void resized() override
    {
        auto b = getLocalBounds().reduced (6, 2);
        remove.setBounds (b.removeFromRight (26)); b.removeFromRight (6);
        amount.setBounds (b.removeFromRight (150));
    }
    void paint (Graphics& g) override
    {
        g.setColour (theme::plateHi.withAlpha (0.5f)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.setColour (d.acc); g.setFont (theme::font (13.0f, true));
        g.drawText (U (sourceName ((int) d.p.rawValue (modSlot (slot, MD_SOURCE)))), 8, 0, 56, getHeight(), juce::Justification::centredLeft, false);
        g.setColour (theme::ink); g.setFont (theme::font (13.0f));
        g.drawText (d.p.targetName ((int) d.p.rawValue (modSlot (slot, MD_TARGET))), 66, 0, getWidth() - 256, getHeight(), juce::Justification::centredLeft, true);
    }
private:
    Ed& d; int slot; juce::Slider amount; juce::TextButton remove;
    std::unique_ptr<juce::SliderParameterAttachment> att;
};

class ModPage : public Ticking
{
public:
    explicit ModPage (Ed& ed) : d (ed)
    {
        for (int m = 0; m < NUM_MODS; ++m)
            if ((int) d.p.rawValue (modSlot (m, MD_TARGET)) >= 0)
            {
                rows.push_back (std::make_unique<ModRow> (d, m));
                addAndMakeVisible (*rows.back());
            }
        hash = signature();
    }
    int signature() const { int h = 0; for (int m = 0; m < NUM_MODS; ++m) h = h * 31 + (int) d.p.rawValue (modSlot (m, MD_TARGET)) + 2; return h; }
    void tick() override { const int h = signature(); if (h != hash) { hash = h; d.e.requestRebuild(); } }
    void resized() override
    {
        auto list = getLocalBounds().withTrimmedTop (32);
        const int rowW = (list.getWidth() - 8) / 2;
        for (int i = 0; i < (int) rows.size(); ++i)
            rows[(size_t) i]->setBounds (list.getX() + (i / 16) * (rowW + 8), list.getY() + (i % 16) * 40, rowW, 34);
    }
    void paint (Graphics& g) override
    {
        g.setColour (theme::ink); g.setFont (theme::font (16.0f, true));
        g.drawText (U ("Matrix"), 0, 2, 300, 22, juce::Justification::centredLeft, false);
        if (! rows.empty()) return;
        g.setColour (theme::dim); g.setFont (theme::font (16.0f));
        g.drawText (U ("Noch keine Verbindung. Zieh auf der Seite Synth den Reiter eines LFOs oder einer Env auf einen Regler."), getLocalBounds(), juce::Justification::centred, false);
    }
private:
    Ed& d; std::vector<std::unique_ptr<ModRow>> rows; int hash = 0;
};

// ---------------- Seite: Presets und Designs ---------------------------------
class PresetsPage : public Ticking, private juce::ListBoxModel
{
public:
    explicit PresetsPage (Ed& ed) : d (ed)
    {
        files = d.p.listPresets();
        list.setModel (this); list.setRowHeight (34);
        list.setColour (juce::ListBox::backgroundColourId, theme::well);
        list.setOutlineThickness (0);
        addAndMakeVisible (list);
        name.setText (d.p.currentPreset, false);
        name.setTextToShowWhenEmpty (U ("Name des Presets"), theme::dim);
        name.setColour (juce::TextEditor::backgroundColourId, theme::well); name.setColour (juce::TextEditor::textColourId, theme::ink);
        name.setColour (juce::TextEditor::outlineColourId, theme::line); name.setColour (juce::TextEditor::focusedOutlineColourId, d.acc);
        name.setFont (theme::font (15.0f));
        addAndMakeVisible (name);
        const auto add = [this] (juce::TextButton& b, const char* t, std::function<void()> fn)
        { styleButton (b); b.setButtonText (U (t)); b.onClick = std::move (fn); addAndMakeVisible (b); };
        add (save, "In Tekkforge speichern", [this]
        {
            const auto n = juce::File::createLegalFileName (name.getText().trim());
            if (n.isEmpty()) { d.e.status = U ("Gib dem Preset zuerst einen Namen."); repaint(); return; }
            d.e.status = d.p.savePresetFile (d.p.presetFolder().getChildFile (n + ".tkf")) ? U ("Gespeichert: ") + n : U ("Speichern ist fehlgeschlagen.");
            d.e.requestRebuild();
        });
        add (exportB, "Als Datei exportieren", [this] { d.e.chooseExport(); });
        add (importB, "Datei importieren", [this] { d.e.chooseImport(); });
        add (reset, "Alles zurücksetzen", [this] { d.p.resetToDefaults(); d.e.status = U ("Alle Modi sind zurückgesetzt."); d.e.requestRebuild(); });
        add (render, "Sound fertig rendern", [this] { d.e.askRenderSound(); });
        add (remove, "Preset löschen", [this]
        {
            const int r = list.getSelectedRow();
            if (r < 0 || r >= files.size()) return;
            d.e.status = files[r].moveToTrash() ? U ("In den Papierkorb gelegt: ") + files[r].getFileNameWithoutExtension() : U ("Löschen ist fehlgeschlagen.");
            d.e.requestRebuild();
        });
    }
    ~PresetsPage() override { list.setModel (nullptr); }
    int getNumRows() override { return files.size(); }
    void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
    {
        if (row >= files.size()) return;
        if (selected) { g.setColour (d.acc.withAlpha (0.25f)); g.fillRect (0, 0, w, h); }
        g.setColour (theme::ink); g.setFont (theme::font (15.0f));
        g.drawText (files[row].getFileNameWithoutExtension(), 12, 0, w - 24, h, juce::Justification::centredLeft, true);
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { loadRow (row); }
    void selectedRowsChanged (int row) override { if (row >= 0 && row < files.size() && ! loading) loadRow (row); }
    void loadRow (int row)
    {
        loading = true;
        d.e.status = d.p.loadPresetFile (files[row]) ? U ("Geladen: ") + files[row].getFileNameWithoutExtension() : U ("Dieses Preset lässt sich nicht lesen.");
        d.e.requestRebuild();
    }
    Rectangle<int> themeArea() const { auto b = getLocalBounds(); b.removeFromLeft (b.getWidth() / 2 + 8); b.removeFromTop (262); return b; }
    Rectangle<int> swatch (int i) const
    {
        auto a = themeArea(); const int w = a.getWidth() / 2, h = a.getHeight() / 4;
        return Rectangle<int> (a.getX() + (i % 2) * w, a.getY() + (i / 2) * h, w, h).reduced (4);
    }
    void resized() override
    {
        auto b = getLocalBounds();
        auto left = b.removeFromLeft (b.getWidth() / 2 - 8); b.removeFromLeft (16);
        left.removeFromTop (28);
        list.setBounds (left);
        b.removeFromTop (28);
        name.setBounds (b.removeFromTop (36)); b.removeFromTop (8);
        auto r = b.removeFromTop (36); const int w = r.getWidth() / 2;
        save.setBounds (r.removeFromLeft (w).reduced (0, 0).withTrimmedRight (4)); remove.setBounds (r.withTrimmedLeft (4));
        b.removeFromTop (8); r = b.removeFromTop (36);
        exportB.setBounds (r.removeFromLeft (w).withTrimmedRight (4)); importB.setBounds (r.withTrimmedLeft (4));
        b.removeFromTop (8); r = b.removeFromTop (36);
        reset.setBounds (r.removeFromLeft (w).withTrimmedRight (4)); render.setBounds (r.withTrimmedLeft (4));
    }
    void paint (Graphics& g) override
    {
        g.setColour (theme::ink); g.setFont (theme::font (16.0f, true));
        g.drawText (U ("Deine Presets"), 0, 2, 300, 22, juce::Justification::centredLeft, false);
        auto ra = getLocalBounds(); ra.removeFromLeft (ra.getWidth() / 2 + 8);
        g.drawText (U ("Speichern"), ra.getX(), 2, 300, 22, juce::Justification::centredLeft, false);
        g.drawText (U ("Farbdesign"), ra.getX(), 236, 300, 22, juce::Justification::centredLeft, false);
        if (d.e.status.isNotEmpty())
        {
            g.setColour (theme::dim); g.setFont (theme::font (13.0f));
            g.drawText (d.e.status, ra.getX(), 200, ra.getWidth(), 32, juce::Justification::centredLeft, true);
        }
        if (files.isEmpty())
        {
            g.setColour (theme::dim); g.setFont (theme::font (15.0f));
            g.drawText (U ("Noch keine Presets. Speichere rechts dein erstes."), list.getBounds().toFloat(), juce::Justification::centred, false);
        }
        for (int i = 0; i < NUM_THEMES; ++i)
        {
            const auto& t = themeDef (i); auto r = swatch (i).toFloat();
            g.setColour (Colour (t.plate)); g.fillRoundedRectangle (r, 6.0f);
            if (i == theme::current) { g.setColour (Colour (t.ink)); g.drawRoundedRectangle (r.reduced (1), 6.0f, 2.0f); }
            auto in = r.reduced (10, 8);
            g.setColour (Colour (t.ink)); g.setFont (theme::font (14.0f, true));
            g.drawText (U (t.name), in.removeFromTop (20), juce::Justification::centredLeft, false);
            in.removeFromTop (4);
            const float w = in.getWidth() / 5.0f;
            for (int a = 0; a < 5; ++a) { g.setColour (Colour (t.accent[a])); g.fillRoundedRectangle (in.getX() + (float) a * w, in.getY(), w - 3, in.getHeight(), 3.0f); }
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < NUM_THEMES; ++i)
            if (swatch (i).contains (e.getPosition())) { theme::apply (i); theme::saveChoice(); return; }
    }
private:
    Ed& d; juce::ListBox list { "presets" }; juce::TextEditor name;
    juce::TextButton save, exportB, importB, reset, remove, render;
    juce::Array<juce::File> files; bool loading = false;
};

// ---------------- Seite: Guide ---------------------------------------------
class GuidePage : public Ticking
{
public:
    GuidePage (Ed& ed, std::function<void (std::vector<float>&)> spec) : d (ed), spectrum (ed, spec)
    {
        addAndMakeVisible (spectrum);
        recipes = recipesFor (d.mode);
        d.e.recipe = juce::jlimit (0, (int) recipes.size() - 1, d.e.recipe);
        const auto tab = [this] (juce::TextButton& b, const String& t, std::function<void()> fn)
        { styleButton (b); b.setButtonText (t); b.onClick = std::move (fn); addAndMakeVisible (b); };
        tab (viewFreq, U ("Frequenzen"), [this] { d.e.guideView = 0; d.e.requestRebuild(); });
        tab (viewHow, U ("Anleitungen"), [this] { d.e.guideView = 1; d.e.requestRebuild(); });
        (d.e.guideView == 0 ? viewFreq : viewHow).setColour (juce::TextButton::buttonColourId, d.acc);
        (d.e.guideView == 0 ? viewFreq : viewHow).setColour (juce::TextButton::textColourOffId, theme::well);
        if (d.e.guideView == 1)
        {
            for (int i = 0; i < (int) recipes.size(); ++i)
            {
                auto b = std::make_unique<juce::TextButton>();
                tab (*b, U (recipes[(size_t) i].title), [this, i] { d.e.recipe = i; d.e.requestRebuild(); });
                if (i == d.e.recipe) { b->setColour (juce::TextButton::buttonColourId, d.acc); b->setColour (juce::TextButton::textColourOffId, theme::well); }
                recipeTabs.push_back (std::move (b));
            }
            int useOsc = -1;
            for (int o = NUM_LAYERS - 1; o >= 0; --o) if (d.p.samples[sampleSlot (d.mode, o)].load() != nullptr) useOsc = o;
            if (useOsc >= 0)
            {
                hasSample = true; report = analyzeSample (*d.p.samples[sampleSlot (d.mode, useOsc)].load()); advice = suggestSettings (d.mode, report, useOsc);
                tab (apply, U ("Vorschläge übernehmen"), [this]
                {
                    for (const auto& e : advice.items)
                    {
                        PRef r { d.p.param (e.index) };
                        r.begin(); r.setReal (e.value); r.end();
                    }
                    d.e.status = U ("Vorschläge übernommen. Spiel jetzt die Note ") + noteName (advice.playNote) + ".";
                    d.e.requestRebuild();
                });
            }
        }
    }
    void resized() override
    {
        auto b = getLocalBounds();
        spectrum.setBounds (b.removeFromTop (190));
        auto head = b.withTrimmedTop (8).removeFromTop (30);
        viewHow.setBounds (head.removeFromRight (110)); head.removeFromRight (4); viewFreq.setBounds (head.removeFromRight (110));
        const int right = getWidth() / 2 + 12;
        auto tabsRow = Rectangle<int> (right, 240, getWidth() - right, 30);
        if (! recipeTabs.empty())
        {
            const int w = tabsRow.getWidth() / (int) recipeTabs.size();
            for (auto& t : recipeTabs) t->setBounds (tabsRow.removeFromLeft (w).withTrimmedRight (4));
        }
        if (hasSample) apply.setBounds (0, getHeight() - 36, getWidth() / 2 - 24, 34);
    }
    void paint (Graphics& g) override
    {
        const auto& gd = guideFor (d.mode);
        auto b = getLocalBounds().withTrimmedTop (198);
        auto head = b.removeFromTop (32);
        g.setColour (theme::ink); g.setFont (theme::font (24.0f, true));
        g.drawText (U (modeName (d.mode)), head, juce::Justification::centredLeft, false);
        b.removeFromTop (10);
        if (d.e.guideView == 1) { paintHow (g, b); return; }

        g.setColour (theme::dim); g.setFont (theme::font (16.0f));
        g.drawFittedText (U (gd.job), b.removeFromTop (28), juce::Justification::centredLeft, 2);
        b.removeFromTop (8);
        auto left = b.removeFromLeft (b.getWidth() / 2 - 12); b.removeFromLeft (24);

        g.setColour (theme::ink); g.setFont (theme::font (16.0f, true));
        g.drawText (U ("Frequenzbereiche"), left.removeFromTop (24), juce::Justification::centredLeft, false);
        for (auto& z : gd.zones)
        {
            auto row = left.removeFromTop (46);
            const bool keepOut = z.lo < 25.0f;
            g.setColour (keepOut ? theme::hot : d.acc); g.fillRoundedRectangle (row.removeFromLeft (5).reduced (0, 4).toFloat(), 2.0f);
            row.removeFromLeft (10);
            auto top = row.removeFromTop (22);
            g.setColour (theme::ink); g.setFont (theme::font (15.0f, true));
            g.drawText (U (z.name), top.removeFromLeft (140), juce::Justification::centredLeft, false);
            g.setColour (keepOut ? theme::hot : d.acc);
            const auto fmt = [] (float hz) { return hz >= 1000 ? String (hz / 1000.0f, 1).replace (".", ",") + " kHz" : String ((int) hz) + " Hz"; };
            g.drawText (fmt (z.lo) + " bis " + fmt (z.hi), top, juce::Justification::centredLeft, false);
            g.setColour (theme::dim); g.setFont (theme::font (14.0f));
            g.drawText (U (z.what), row.removeFromTop (22), juce::Justification::centredLeft, true);
        }
        g.setColour (theme::ink); g.setFont (theme::font (16.0f, true));
        g.drawText (U ("Darauf achten"), b.removeFromTop (24), juce::Justification::centredLeft, false);
        for (auto* c : gd.check)
        {
            auto row = b.removeFromTop (40);
            g.setColour (d.acc); g.fillEllipse (row.getX() + 1.0f, row.getY() + 8.0f, 6.0f, 6.0f);
            g.setColour (theme::ink); g.setFont (theme::font (14.5f));
            g.drawFittedText (U (c), row.withTrimmedLeft (16), juce::Justification::topLeft, 2);
        }
    }
    void paintHow (Graphics& g, Rectangle<int> b)
    {
        auto left = b.removeFromLeft (b.getWidth() / 2 - 12); b.removeFromLeft (24);
        // rechts: Schritte
        b.removeFromTop (36);
        const auto& steps = recipes[(size_t) d.e.recipe].steps;
        const int stepH = juce::jmin (54, b.getHeight() / juce::jmax (1, (int) steps.size()));
        for (size_t i = 0; i < steps.size(); ++i)
        {
            auto row = b.removeFromTop (stepH);
            auto num = row.removeFromLeft (28).removeFromTop (24).toFloat();
            g.setColour (d.acc); g.fillEllipse (num.withSizeKeepingCentre (22, 22));
            g.setColour (theme::well); g.setFont (theme::font (13.0f, true));
            g.drawText (String ((int) i + 1), num, juce::Justification::centred, false);
            g.setColour (theme::ink); g.setFont (theme::font (14.5f));
            g.drawFittedText (U (steps[i]), row.withTrimmedLeft (6).withTrimmedRight (4), juce::Justification::topLeft, 2);
        }
        // links: Messung und Vorschlaege
        g.setColour (theme::ink); g.setFont (theme::font (16.0f, true));
        g.drawText (U ("Dein Sample"), left.removeFromTop (24), juce::Justification::centredLeft, false);
        if (! hasSample)
        {
            g.setColour (theme::dim); g.setFont (theme::font (15.0f));
            g.drawFittedText (U ("Lade ein Sample, dann misst Tekkforge es und schlägt dir hier Einstellungen für diesen Modus vor."),
                              left.removeFromTop (60), juce::Justification::topLeft, 3);
            return;
        }
        const auto line = [&] (const String& k, const String& v)
        {
            auto r = left.removeFromTop (20);
            g.setColour (theme::dim); g.setFont (theme::font (13.5f)); g.drawText (k, r.removeFromLeft (120), juce::Justification::centredLeft, false);
            g.setColour (theme::ink); g.drawText (v, r, juce::Justification::centredLeft, true);
        };
        line (U ("Grundton"), report.tonal ? hzText (report.hz) + "   " + String (report.cents >= 0 ? "+" : "") + String ((int) std::round (report.cents)) + " ct" : U ("kein klarer Ton gefunden"));
        line (U ("Länge"), String (report.seconds, 2) + " s");
        line (U ("Anschlag"), String (report.attackMs, 1) + " ms");
        line (U ("Bass bis 120 Hz"), String (juce::roundToInt ((report.band[0] + report.band[1]) * 100.0f)) + " % der Energie");
        line (U ("Spiel die Note"), noteName (advice.playNote));
        left.removeFromTop (8);
        g.setColour (theme::ink); g.setFont (theme::font (15.0f, true));
        g.drawText (U ("Warum diese Vorschläge"), left.removeFromTop (22), juce::Justification::centredLeft, false);
        left.removeFromBottom (44);
        for (const auto& e : advice.items)
        {
            if (e.why.empty() || left.getHeight() < 36) continue;
            auto row = left.removeFromTop (36);
            g.setColour (d.acc); g.fillEllipse (row.getX() + 1.0f, row.getY() + 7.0f, 5.0f, 5.0f);
            g.setColour (theme::dim); g.setFont (theme::font (13.5f));
            g.drawFittedText (U (e.why), row.withTrimmedLeft (14), juce::Justification::topLeft, 2);
        }
        if (d.e.status.isNotEmpty())
        {
            g.setColour (d.acc); g.setFont (theme::font (13.0f));
            g.drawText (d.e.status, 0, getHeight() - 56, getWidth() / 2 - 24, 18, juce::Justification::centredLeft, true);
        }
    }
private:
    Ed& d; Spectrum spectrum;
    juce::TextButton viewFreq, viewHow, apply;
    std::vector<Recipe> recipes; std::vector<std::unique_ptr<juce::TextButton>> recipeTabs;
    bool hasSample = false; SampleReport report; SuggestionSet advice;
};

// ---------------- Kopfzeile ------------------------------------------------
class TopBar : public Ticking
{
public:
    static constexpr int pageOrder[7] = { 0, 1, 2, 6, 3, 4, 5 };
    TopBar (TekkforgeEditor& editor, Ed& ed) : e (editor), d (ed), quality (d, G_QUALITY), master (d, G_MASTER, "Master")
    {
        addAndMakeVisible (quality); addAndMakeVisible (master);
    }
    Rectangle<int> modeArea() const { return { 136, 8, 300, 50 }; }
    Rectangle<int> pageArea() const { return { 444, 8, 390, 50 }; }
    Rectangle<int> auditionArea() const { return { 840, 16, 86, 34 }; }
    void resized() override
    {
        quality.setBounds (932, 16, 96, 30);
        master.setBounds (getWidth() - 150, 0, 64, 66);
    }
    void paint (Graphics& g) override
    {
        g.setColour (theme::ink); g.setFont (theme::font (23.0f, true));
        g.drawText ("Tekkforge", 14, 8, 122, 50, juce::Justification::centredLeft, false);

        auto ma = modeArea(); const int w = ma.getWidth() / NUM_MODES;
        for (int m = 0; m < NUM_MODES; ++m)
        {
            auto r = ma.removeFromLeft (w).reduced (2, 0).toFloat();
            const Colour c = theme::accent (m); const bool sel = m == d.mode;
            g.setColour (sel ? c : theme::well); g.fillRoundedRectangle (r, 6.0f);
            g.setColour (sel ? theme::well : c); g.setFont (theme::font (14.0f, true));
            g.drawText (U (modeName (m)), r, juce::Justification::centred, false);
        }
        static const char* pages[] = { "Synth", "Sample", "FX", "EQ", "Matrix", "Guide", "Presets" };
        auto pa = pageArea(); const int pw = pa.getWidth() / 7;
        for (int i = 0; i < 7; ++i)
        {
            auto r = pa.removeFromLeft (pw).reduced (2, 8).toFloat();
            const bool sel = pageOrder[i] == e.getPage();
            g.setColour (sel ? theme::ink : theme::plateHi); g.fillRoundedRectangle (r, 5.0f);
            g.setColour (sel ? theme::well : theme::ink); g.setFont (theme::font (13.0f, sel));
            g.drawFittedText (pages[i], r.toNearestInt().reduced (1, 0), juce::Justification::centred, 1, 0.8f);
        }
        {
            auto r = auditionArea().toFloat();
            g.setColour (e.auditionOn ? d.acc : theme::well); g.fillRoundedRectangle (r, 5.0f);
            g.setColour (e.auditionOn ? theme::well : theme::ink); g.setFont (theme::font (13.0f, e.auditionOn));
            g.drawText (U ("Vorhören"), r, juce::Justification::centred, false);
        }
        // Note und Frequenz fuer sauberes Stimmen
        const int note = d.p.lastNote.load();
        auto info = Rectangle<int> (1034, 8, getWidth() - 1034 - 152, 50);
        if (note >= 0)
        {
            const float tune = d.p.rawValue (modeParam (d.mode, M_TUNE)) + d.p.rawValue (modeParam (d.mode, M_FINE)) * 0.01f;
            float hz = 440.0f * std::pow (2.0f, ((float) note + tune - 69.0f) / 12.0f);
            if (d.mode == KICK && (int) d.p.rawValue (modeParam (KICK, Y (1, LY_KEY))) == 1) { while (hz >= 80.0f) hz *= 0.5f; while (hz < 40.0f) hz *= 2.0f; }   // so klingt der Bass wirklich
            g.setColour (theme::ink); g.setFont (theme::font (15.0f, true));
            g.drawText (noteName (note), info.removeFromTop (25), juce::Justification::bottomRight, false);
            g.setColour (theme::dim); g.setFont (theme::font (12.5f));
            g.drawText (String (hz, 1) + " Hz", info, juce::Justification::topRight, false);
        }
        // Pegel
        auto mb = Rectangle<float> ((float) getWidth() - 78.0f, 8.0f, 66.0f, 50.0f);
        for (int c = 0; c < 2; ++c)
        {
            auto lane = mb.removeFromTop (22.0f).reduced (0, 2); mb.removeFromTop (6.0f);
            const float pk = c == 0 ? peakL : peakR, db = 20.0f * std::log10 (pk + 1e-5f), n = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
            g.setColour (theme::well); g.fillRoundedRectangle (lane, 3.0f);
            g.setColour (pk >= 0.99f ? theme::hot : d.acc); g.fillRoundedRectangle (lane.withWidth (lane.getWidth() * n), 3.0f);
        }
    }
    void mouseDown (const juce::MouseEvent& ev) override
    {
        if (modeArea().contains (ev.getPosition()))
        {
            const int m = juce::jlimit (0, NUM_MODES - 1, (ev.x - modeArea().getX()) * NUM_MODES / modeArea().getWidth());
            PRef r { d.p.param (G_MODE) }; r.begin(); r.setReal ((float) m); r.end();
        }
        else if (pageArea().contains (ev.getPosition()))
            e.showPage (pageOrder[juce::jlimit (0, 6, (ev.x - pageArea().getX()) * 7 / pageArea().getWidth())]);
        else if (auditionArea().contains (ev.getPosition())) { e.auditionOn = ! e.auditionOn; repaint(); }
    }
    void tick() override
    {
        Ticking::tick();
        peakL = d.p.meterL.load(); peakR = d.p.meterR.load();
        const int n = d.p.lastNote.load();
        if (peakL != shownL || peakR != shownR || n != shownNote) { shownL = peakL; shownR = peakR; shownNote = n; repaint(); }
    }
private:
    TekkforgeEditor& e; Ed& d; Strip quality; Knob master;
    float peakL = 0, peakR = 0, shownL = -1, shownR = -1; int shownNote = -2;
};

// ---------------- Hauptfenster ---------------------------------------------
TekkforgeEditor::TekkforgeEditor (TekkforgeProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    theme::loadChoice();
    look = std::make_unique<TkLook>();
    setLookAndFeel (look.get());
    juce::LookAndFeel::setDefaultLookAndFeel (look.get());
    preview = std::make_unique<Preview> (p);
    keys = std::make_unique<juce::MidiKeyboardComponent> (p.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard);
    keys->setAvailableRange (24, 96);
    keys->setKeyWidth (29.0f);
    addAndMakeVisible (*keys);
    update = std::make_unique<tk::UpdateBanner> ("brucknerjulian55-netizen/tekkforge", JucePlugin_VersionString);
    update->fill = [this] { return theme::accent ((int) proc.rawValue (G_MODE)); };
    update->text = [] { return theme::well; };
    addChildComponent (*update);
    setSize (1280, 960);
    rebuild();
    startTimerHz (30);
}
TekkforgeEditor::~TekkforgeEditor()
{
    stopTimer();
    top.reset(); page.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void TekkforgeEditor::rebuild()
{
    const int mode = (int) proc.rawValue (G_MODE);
    lastMode = mode;
    page.reset(); top.reset();
    lastTheme = theme::current; lastSample = proc.sampleVersion.load(); lastPreviewNote = -1;
    keys->setColour (juce::MidiKeyboardComponent::whiteNoteColourId, theme::plateHi);
    keys->setColour (juce::MidiKeyboardComponent::blackNoteColourId, theme::well.getBrightness() > 0.5f ? theme::ink : theme::well);   // helles Design: dunkle Tasten
    keys->setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, theme::plate);
    keys->setColour (juce::MidiKeyboardComponent::textLabelColourId, theme::dim);
    keys->setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::accent (mode));
    keys->setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, theme::accent (mode).withAlpha (0.3f));
    ctx.reset (new Ed { *this, proc, mode, theme::accent (mode) });
    look->accent = ctx->acc;
    auto& d = *ctx;

    top = std::make_unique<TopBar> (*this, d);
    addAndMakeVisible (*top);

    auto* pv = preview.get();
    auto waveSrc = [pv] (std::vector<float>& lo, std::vector<float>& hi, float& len)
    {
        std::lock_guard<std::mutex> l (pv->m);
        lo = pv->waveLo; hi = pv->waveHi; len = pv->duration;
        return pv->version.load();
    };
    auto specSrc = [pv] (std::vector<float>& s) { std::lock_guard<std::mutex> l (pv->m); s = pv->staticSpec; };

    switch (pageIndex)
    {
        case 1:  page = std::make_unique<SamplePage> (d); break;
        case 2:  page = std::make_unique<FxPage> (d); break;
        case 3:  page = std::make_unique<ModPage> (d); break;
        case 4:  page = std::make_unique<GuidePage> (d, specSrc); break;
        case 5:  page = std::make_unique<PresetsPage> (d); break;
        case 6:  page = std::make_unique<EqPage> (d); break;
        default: page = std::make_unique<SynthPage> (d, waveSrc, specSrc); break;
    }
    addAndMakeVisible (*page);
    update->toFront (false);
    resized();
    repaint();
}

void TekkforgeEditor::requestRebuild() { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<TekkforgeEditor> (this)] { if (sp != nullptr) sp->rebuild(); }); }
void TekkforgeEditor::showPage (int p) { pageIndex = p; requestRebuild(); }

void TekkforgeEditor::paint (Graphics& g)
{
    g.fillAll (theme::plate);
}

void TekkforgeEditor::resized()
{
    auto b = getLocalBounds();
    if (top != nullptr) top->setBounds (b.removeFromTop (66));
    keys->setBounds (b.removeFromBottom (72).reduced (10, 0).withTrimmedBottom (8));
    if (update != nullptr) update->setBounds (932, 47, 96, 17);
    if (page != nullptr) page->setBounds (b.reduced (10, 4));
}

void TekkforgeEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (files.isEmpty()) return;
    // Wird die Datei auf einem Oszillator abgelegt, landet sie genau dort
    int osc = sampleOsc;
    for (auto* c = getComponentAt (x, y); c != nullptr; c = c->getParentComponent())
        if (auto* p = dynamic_cast<Panel*> (c); p != nullptr && p->oscIndex >= 0) { osc = p->oscIndex; break; }
    loadSampleFile (juce::File (files[0]), osc);
}

void TekkforgeEditor::loadSampleFile (const juce::File& file, int osc)
{
    const int mode = (int) proc.rawValue (G_MODE);
    String msg;
    if (proc.loadSample (mode, osc, file, msg))
    {
        const auto set = [this, mode] (int p, float real)
        {
            auto* x = proc.param (modeParam (mode, p));
            x->beginChangeGesture(); x->setValueNotifyingHost (x->convertTo0to1 (real)); x->endChangeGesture();
        };
        set (Y (osc, LY_SRC), 1.0f);
        set (Y (osc, LY_MUTE), 0.0f);
        if (proc.rawValue (modeParam (mode, Y (osc, LY_LVL))) <= 0.0f) set (Y (osc, LY_LVL), 1.0f);
        status = msg.isNotEmpty() ? msg : U ("Sample geladen. Es läuft auf dieser Ebene, die anderen bleiben aktiv.");
    }
    else status = msg;
    sampleOsc = osc;
    requestRebuild();
}

void TekkforgeEditor::chooseSample (int osc)
{
    chooser = std::make_unique<juce::FileChooser> (U ("Sample laden"), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [sp = SafePointer<TekkforgeEditor> (this), osc] (const juce::FileChooser& c)
                          { if (sp != nullptr && c.getResult().existsAsFile()) sp->loadSampleFile (c.getResult(), osc); });
}

void TekkforgeEditor::askRenderSound()
{
    const int mode = (int) proc.rawValue (G_MODE), last = proc.lastNote.load();
    const int note = last >= 0 ? last : (modeIsMono (mode) ? 29 : 60);
    auto* w = new juce::AlertWindow (U ("Sound fertig rendern"),
        U ("Der aktuelle Sound wird mit allen Effekten auf Note ") + noteName (note) + U (" in ein Sample gerechnet. "
           "Danach steht das ganze Plugin wieder auf Standard und nur dieses Sample bleibt im Modus ") + U (modeName (mode)) + U (".\n\n"
           "Einstellungen und die anderen Modi sind dann weg. Möchtest du vorher ein Preset speichern?"),
        juce::MessageBoxIconType::QuestionIcon, this);
    w->addTextEditor ("name", proc.currentPreset.isNotEmpty() ? proc.currentPreset : U (modeName (mode)) + " " + noteName (note), U ("Presetname"));
    w->addButton (U ("Speichern und rendern"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (U ("Ohne Speichern rendern"), 2);
    w->addButton (U ("Abbrechen"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([sp = SafePointer<TekkforgeEditor> (this), w, mode, note] (int choice)
    {
        std::unique_ptr<juce::AlertWindow> owner (w);
        if (sp == nullptr || choice == 0) return;
        auto& pr = sp->proc;
        String msg;
        auto data = pr.renderSound (note, msg);    // zuerst rendern: scheitert es, bleibt alles unveraendert
        if (data == nullptr) { sp->status = msg; sp->requestRebuild(); return; }
        if (choice == 1)
        {
            const auto base = juce::File::createLegalFileName (w->getTextEditorContents ("name").trim());
            if (base.isEmpty()) { sp->status = U ("Gib dem Preset einen Namen. Es wurde nichts geändert."); sp->requestRebuild(); return; }
            const auto file = pr.presetFolder().getChildFile (base + ".tkf").getNonexistentSibling (false);
            if (! pr.savePresetFile (file)) { sp->status = U ("Speichern ist fehlgeschlagen. Es wurde nichts geändert."); sp->requestRebuild(); return; }
        }
        const auto seconds = (float) data->length() / data->rate;
        pr.applyRendered (std::move (data), mode, note);
        sp->sampleOsc = 0;
        sp->status = U ("Fertig gerendert: ") + String (seconds, 2) + U (" s. Das Plugin steht auf Standard.");
        sp->showPage (1);
    }), false);
}

void TekkforgeEditor::chooseExport()
{
    chooser = std::make_unique<juce::FileChooser> (U ("Preset exportieren"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.tkf");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [sp = SafePointer<TekkforgeEditor> (this)] (const juce::FileChooser& c)
    {
        if (sp == nullptr || c.getResult() == juce::File()) return;
        const auto file = c.getResult().withFileExtension ("tkf");
        sp->status = sp->proc.savePresetFile (file) ? U ("Exportiert: ") + file.getFileName() : U ("Export ist fehlgeschlagen.");
        sp->requestRebuild();
    });
}

void TekkforgeEditor::chooseImport()
{
    chooser = std::make_unique<juce::FileChooser> (U ("Preset importieren"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.tkf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [sp = SafePointer<TekkforgeEditor> (this)] (const juce::FileChooser& c)
    {
        if (sp == nullptr || ! c.getResult().existsAsFile()) return;
        // Kopie in die interne Liste, damit das Preset dort dauerhaft auftaucht
        const auto target = sp->proc.presetFolder().getChildFile (c.getResult().getFileName());
        if (target != c.getResult()) c.getResult().copyFileTo (target);
        sp->status = sp->proc.loadPresetFile (target) ? U ("Importiert: ") + target.getFileNameWithoutExtension() : U ("Diese Datei ist kein Tekkforge-Preset.");
        sp->requestRebuild();
    });
}

void TekkforgeEditor::timerCallback()
{
    const int mode = (int) proc.rawValue (G_MODE);
    if (mode != lastMode || theme::current != lastTheme || proc.sampleVersion.load() != lastSample) { rebuild(); return; }

    float now[NUM_PARAMS]; proc.snapshot (now);
    bool changed = false;
    bool soundChanged = false;
    for (int i = 0; i < NUM_PARAMS; ++i)
        if (now[i] != lastValues[i])
        {
            changed = true; lastValues[i] = now[i];
            if ((i >= MODE_BASE && i < LFO_BASE) || i >= EQ_BASE) soundChanged = true;
        }
    // Vorhoeren: kurz nach dem Drehen die Note anspielen, damit man jede Aenderung hoert
    const auto nowMs = juce::Time::getMillisecondCounter();
    if (soundChanged && lastPreviewNote >= 0) { auditionDue = true; auditionAt = nowMs + 120; }
    if (auditionDue && nowMs >= auditionAt) { auditionDue = false; if (auditionOn) proc.auditionNow.store (true); }
    const int defaultNote = modeIsMono (mode) ? 29 : 60;
    const int ln = proc.lastNote.load(), note = ln >= 0 ? ln : defaultNote;
    if (changed || note != lastPreviewNote) { lastPreviewNote = note; preview->request (now, note); }

    if (auto* t = dynamic_cast<Ticking*> (page.get())) t->tick();
    if (top != nullptr) top->tick();
}
