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
    inline Colour accent (int mode) { return accents[juce::jlimit (0, 4, mode)]; }
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
    Colour accent = theme::accent (4);

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

        const float mod = (float) s.getProperties().getWithDefault ("mod", -1.0f);
        if (mod >= 0.0f) arc (ang, a0 + mod * (a1 - a0), r + 0.5f, 2.0f, theme::ink);
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

class Knob : public Ticking, public juce::DragAndDropTarget
{
public:
    Knob (Ed& ed, int index, String title) : d (ed), idx (index), name (std::move (title))
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRotaryParameters (juce::degreesToRadians (-135.0f) + juce::MathConstants<float>::twoPi,
                               juce::degreesToRadians (135.0f) + juce::MathConstants<float>::twoPi, true);
        s.setMouseDragSensitivity (180);
        addAndMakeVisible (s);
        auto* p = d.p.param (idx);
        att = std::make_unique<juce::SliderParameterAttachment> (*p, s, nullptr);
        s.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        s.addMouseListener (this, false);
    }
    ~Knob() override { s.removeMouseListener (this); }
    void resized() override { auto b = getLocalBounds().removeFromTop (getHeight() - 30); const int z = juce::jmin (46, b.getHeight()); s.setBounds (b.withSizeKeepingCentre (z, z)); }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().removeFromBottom (30);
        g.setColour (theme::dim); g.setFont (theme::font (12.0f));
        g.drawFittedText (name, b.removeFromTop (14), juce::Justification::centred, 1, 0.7f);
        g.setColour (theme::ink); g.setFont (theme::font (12.0f));
        g.drawFittedText (shown, b, juce::Justification::centred, 1, 0.7f);
    }
    void tick() override
    {
        auto* p = d.p.param (idx);
        const String v = p->getCurrentValueAsText().fromFirstOccurrenceOf (name + " ", false, false).isNotEmpty()
                           ? p->getCurrentValueAsText().fromFirstOccurrenceOf (name + " ", false, false) : p->getCurrentValueAsText();
        if (v != shown) { shown = v; repaint(); }
        float mod = -1.0f;
        for (int l = 0; l < NUM_LANES; ++l)
            if (d.p.rawValue (laneParam (l, L_ON)) > 0.5f && (int) d.p.rawValue (laneParam (l, L_TARGET)) == idx)
                mod = d.p.modView[(size_t) idx].load (std::memory_order_relaxed);
        for (int m = 0; m < NUM_MODS; ++m)
            if ((int) d.p.rawValue (modSlot (m, MD_TARGET)) == idx)
                mod = d.p.modView[(size_t) idx].load (std::memory_order_relaxed);
        if (std::fabs (mod - lastMod) > 0.002f) { lastMod = mod; s.getProperties().set ("mod", mod); s.repaint(); }
    }
    bool isInterestedInDragSource (const SourceDetails& s) override { return s.description.toString().startsWith ("tkmod:"); }
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails& s) override
    {
        dropHover = false; repaint();
        d.p.connect (s.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue(), idx);
    }
    void paintOverChildren (Graphics& g) override
    {
        if (dropHover) { g.setColour (d.acc); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 2.0f); }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            for (int i = 0; i < NUM_MODS; ++i)
                if ((int) d.p.rawValue (modSlot (i, MD_TARGET)) == idx)
                    m.addItem (i + 1, U (sourceName ((int) d.p.rawValue (modSlot (i, MD_SOURCE)))) + U (" entfernen"));
            if (m.getNumItems() > 0)
                m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r) { if (r > 0) d.p.disconnect (r - 1); });
            return;
        }
        if (d.e.learnLane >= 0)
        {
            auto* t = d.p.param (laneParam (d.e.learnLane, L_TARGET));
            t->setValueNotifyingHost (t->convertTo0to1 ((float) idx));
            d.e.learnLane = -1;
            d.e.showPage (4);
        }
    }
    juce::Slider s;
private:
    Ed& d; int idx; String name, shown; float lastMod = -2.0f; bool dropHover = false;
    std::unique_ptr<juce::SliderParameterAttachment> att;
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
    WaveDisp (Ed& ed, int oscIndex) : XYDisp (ed, { modeParam (ed.mode, O (oscIndex, O_WAVE)), modeParam (ed.mode, O (oscIndex, O_UNI)), modeParam (ed.mode, O (oscIndex, O_DETUNE)),
                                                    modeParam (ed.mode, O (oscIndex, O_SRC)) }, O (oscIndex, O_WAVE), -1), osc (oscIndex) {}
    const SampleData* sample() const { return d.p.samples[sampleSlot (d.mode, osc)].load(); }
    bool isSample() const { return val (O (osc, O_SRC)) > 0.5f; }
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
        const float w = val (O (osc, O_WAVE)); Path p; const int N = 200;
        for (int i = 0; i <= N; ++i)
        {
            const float ph = std::fmod ((float) i / (float) N * 2.0f, 1.0f);
            const float y = b.getCentreY() - morphOsc (ph, 0.001f, w) * b.getHeight() * 0.42f;
            const float x = b.getX() + b.getWidth() * (float) i / (float) N;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        const int uni = (int) std::round (val (O (osc, O_UNI)));
        for (int u = 1; u < uni; ++u)
        {
            const float o = (float) (u - uni / 2) * juce::jmin (5.0f, val (O (osc, O_DETUNE)) * 0.25f);
            g.setColour (d.acc.withAlpha (0.25f));
            g.strokePath (p, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (o, 0));
        }
        stroke (g, p);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (isSample()) { if (sample() == nullptr) d.e.chooseSample (osc); return; }
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
    int osc, seen = -1;
};

class PitchDisp : public XYDisp
{
public:
    PitchDisp (Ed& ed) : XYDisp (ed, { modeParam (ed.mode, M_PAMT), modeParam (ed.mode, M_PTIME), modeParam (ed.mode, M_PCURVE) }, M_PTIME, M_PAMT) {}
    void draw (Graphics& g, Rectangle<float> b) override
    {
        const float nx = ref (M_PTIME).norm(), ny = ref (M_PAMT).norm(), c = 1.0f + val (M_PCURVE) * 9.0f;
        const float zero = b.getY() + b.getHeight() * (1.0f - params()[(size_t) modeParam (0, M_PAMT)].toNorm (0));
        g.setColour (theme::line); g.drawHorizontalLine ((int) zero, b.getX(), b.getRight());
        const float top = b.getBottom() - ny * b.getHeight(), endX = b.getX() + nx * b.getWidth();
        Path p; p.startNewSubPath (b.getX(), top);
        for (int i = 1; i <= 80; ++i)
        {
            const float t = (float) i / 80.0f;
            p.lineTo (b.getX() + t * (endX - b.getX()), zero + (top - zero) * std::pow (1.0f - t, c));
        }
        p.lineTo (b.getRight(), zero);
        stroke (g, p); dot (g, b.getX() + 2, top); dot (g, endX, zero);
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { auto r = ref (M_PCURVE); r.setNorm (r.norm() + w.deltaY * 0.2f); }
};

class AmpDisp : public Disp
{
public:
    AmpDisp (Ed& ed) : Disp (ed, { modeParam (ed.mode, M_ATT), modeParam (ed.mode, M_HOLD), modeParam (ed.mode, M_DEC), modeParam (ed.mode, M_SUS), modeParam (ed.mode, M_REL), modeParam (ed.mode, M_ACURVE), modeParam (ed.mode, M_GATE), modeParam (ed.mode, M_LENGTH) }) {}
    bool gate() const { return val (M_GATE) > 0.5f; }
    float susValue() const { const int g = (int) val (M_GATE); return g == 2 ? 1.0f : g == 1 ? val (M_SUS) : 0.0f; }
    float px (int i) const   // Jeder Abschnitt nutzt ein Viertel der Breite, verschoben um die Vorgaenger
    {
        static const int order[4] = { M_ATT, M_HOLD, M_DEC, M_REL };
        float x = 0; for (int k = 0; k <= i; ++k) x += ref (order[k]).norm() * 0.25f;
        return x;
    }
    void draw (Graphics& g, Rectangle<float> b) override
    {
        const float sus = susValue(), c = 0.6f + val (M_ACURVE) * 5.4f;
        const auto X = [&] (float n) { return b.getX() + n * b.getWidth(); };
        const auto Y = [&] (float v) { return b.getBottom() - v * b.getHeight(); };
        const float a = X (px (0)), h = X (px (1)), de = X (px (2)), r = X (px (3));
        Path p; p.startNewSubPath (b.getX(), Y (0)); p.lineTo (a, Y (1)); p.lineTo (h, Y (1));
        for (int i = 1; i <= 60; ++i) { const float t = (float) i / 60.0f; p.lineTo (h + (de - h) * t, Y (sus + (1 - sus) * std::pow (1 - t, c))); }
        if (gate()) p.lineTo (r, Y (0));
        fillUnder (g, p, b); stroke (g, p);
        dot (g, a, Y (1)); dot (g, h, Y (1)); dot (g, de, Y (sus)); if (gate()) dot (g, r, Y (0));
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto b = getLocalBounds().toFloat().reduced (6.0f);
        const float n = (float) (e.x - b.getX()) / b.getWidth();
        static const int order[4] = { M_ATT, M_HOLD, M_DEC, M_REL };
        best = 0; float dist = 9;
        for (int i = 0; i < 4; ++i) if (std::fabs (px (i) - n) < dist) { dist = std::fabs (px (i) - n); best = i; }
        grabbed = order[best];
        auto r = ref (grabbed); r.begin(); start = r.norm();
        auto s = ref (M_SUS); s.begin(); sStart = s.norm();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        ref (grabbed).setNorm (start + 4.0f * (float) e.getDistanceFromDragStartX() / (float) getWidth());
        if (best == 2) ref (M_SUS).setNorm (sStart - (float) e.getDistanceFromDragStartY() / (float) getHeight());
    }
    void mouseUp (const juce::MouseEvent&) override { ref (grabbed).end(); ref (M_SUS).end(); }
private:
    int grabbed = M_ATT, best = 0; float start = 0, sStart = 0;
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
    FilterDisp (Ed& ed) : XYDisp (ed, { modeParam (ed.mode, M_CUT), modeParam (ed.mode, M_RES), modeParam (ed.mode, M_FTYPE), modeParam (ed.mode, M_FENV) }, M_CUT, M_RES) {}
    void draw (Graphics& g, Rectangle<float> b) override
    {
        const auto curve = [&] (float fc)
        {
            Path p; const int type = (int) val (M_FTYPE);
            for (int i = 0; i <= 120; ++i)
            {
                const float x = (float) i / 120.0f, db = type == 3 ? 0.0f : 20.0f * std::log10 (svfMag (type, xHz (x), fc, val (M_RES)) + 1e-4f);
                const float y = b.getY() + b.getHeight() * (0.3f - db / 60.0f);
                if (i == 0) p.startNewSubPath (b.getX() + x * b.getWidth(), y); else p.lineTo (b.getX() + x * b.getWidth(), juce::jlimit (b.getY(), b.getBottom() + 2, y));
            }
            return p;
        };
        const float fc = val (M_CUT);
        if (std::fabs (val (M_FENV)) > 0.01f)
        {
            g.setColour (theme::dim.withAlpha (0.6f));
            g.strokePath (curve (juce::jlimit (20.0f, 20000.0f, fc * std::pow (2.0f, val (M_FENV)))), juce::PathStrokeType (1.0f));
        }
        auto p = curve (fc); fillUnder (g, p, b); stroke (g, p);
    }
};

class ClickDisp : public XYDisp
{
public:
    ClickDisp (Ed& ed) : XYDisp (ed, { modeParam (ed.mode, M_CLKLVL), modeParam (ed.mode, M_CLKLEN), modeParam (ed.mode, M_CLKNOISE), modeParam (ed.mode, M_CLKTONE) }, M_CLKLEN, M_CLKLVL) {}
    void draw (Graphics& g, Rectangle<float> b) override
    {
        juce::Random r (7); Path p; const int N = (int) b.getWidth();
        const float tau = val (M_CLKLEN), lvl = val (M_CLKLVL), nz = val (M_CLKNOISE), tone = val (M_CLKTONE);
        for (int i = 0; i < N; ++i)
        {
            const float ms = (float) i / (float) N * 40.0f, e = std::exp (-ms / tau) * lvl;
            const float s = (1 - nz) * std::sin (ms * 0.001f * tone * 6.283f * 0.18f) + nz * (r.nextFloat() * 2 - 1);
            const float y = b.getCentreY() - e * s * b.getHeight() * 0.48f;
            if (i == 0) p.startNewSubPath (b.getX(), y); else p.lineTo (b.getX() + (float) i, y);
        }
        stroke (g, p, 1.3f);
    }
};

class DriveDisp : public XYDisp
{
public:
    DriveDisp (Ed& ed) : XYDisp (ed, { modeParam (ed.mode, M_DRV), modeParam (ed.mode, M_DRVTYPE) }, -1, M_DRV) {}
    void draw (Graphics& g, Rectangle<float> b) override
    {
        const float gain = std::pow (10.0f, val (M_DRV) * 2.0f); const int t = (int) val (M_DRVTYPE);
        g.setColour (theme::line); g.drawHorizontalLine ((int) b.getCentreY(), b.getX(), b.getRight());
        g.drawVerticalLine ((int) b.getCentreX(), b.getY(), b.getBottom());
        Path p;
        for (int i = 0; i <= 200; ++i)
        {
            const float x = (float) i / 200.0f * 2 - 1, y = shape (x * gain, t);
            const float px = b.getCentreX() + x * b.getWidth() * 0.5f, py = b.getCentreY() - y * b.getHeight() * 0.46f;
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        stroke (g, p);
    }
};

class SubDisp : public XYDisp
{
public:
    SubDisp (Ed& ed) : XYDisp (ed, { modeParam (ed.mode, M_SUBLVL), modeParam (ed.mode, M_SUBOCT) }, -1, M_SUBLVL) {}
    void draw (Graphics& g, Rectangle<float> b) override
    {
        const float cycles = 2.0f * std::pow (0.5f, std::round (val (M_SUBOCT))), l = val (M_SUBLVL);
        Path p;
        for (int i = 0; i <= 120; ++i)
        {
            const float t = (float) i / 120.0f, y = b.getCentreY() - std::sin (t * cycles * 6.283f) * l * b.getHeight() * 0.46f;
            if (i == 0) p.startNewSubPath (b.getX(), y); else p.lineTo (b.getX() + t * b.getWidth(), y);
        }
        stroke (g, p, 2.5f);
    }
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
            const float body = (q[M_ATT] + q[M_LENGTH]) * 0.001f;
            float wanted = body * 1.05f + (q[M_GATE] > 0.5f ? q[M_REL] * 0.001f : 0.0f) + 0.02f;
            for (int o = 0; o < NUM_OSCS; ++o)
                if (const auto* smp = p.samples[sampleSlot (mode, o)].load(); smp != nullptr && q[O (o, O_SRC)] > 0.5f && q[O (o, O_FREE)] > 0.5f)
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
    Knob* knobAbs (int index, const char* name) { auto* k = own (std::make_unique<Knob> (d, index, U (name))); knobs.push_back (k); return k; }
    void resized() override
    {
        auto b = getLocalBounds().reduced (8, 6);
        auto head = b.removeFromTop (22);
        if (headStrip != nullptr) headStrip->setBounds (head.removeFromRight (headWidth).reduced (0, 1));
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
private:
    Ed& d; String title; std::vector<std::unique_ptr<juce::Component>> keep; std::vector<Knob*> knobs;
};

// ---------------- LFOs und Envs -------------------------------------------
class LfoView : public Ticking
{
public:
    LfoView (Ed& ed, int index) : d (ed), lfo (index) {}
    void tick() override { const float v = d.p.engine.sourceView[lfo].load (std::memory_order_relaxed); if (v != last) { last = v; repaint(); } }
    void paint (Graphics& g) override
    {
        auto o = getLocalBounds().toFloat();
        g.setColour (theme::well); g.fillRoundedRectangle (o, 6.0f);
        auto b = o.reduced (6.0f);
        const int s = (int) d.p.rawValue (lfoParam (lfo, LF_SHAPE));
        Path p;
        for (int i = 0; i <= 160; ++i)
        {
            const float t = (float) i / 160.0f, x = std::fmod (t * 2.0f, 1.0f); float y;
            switch (s) { case 0: y = std::sin (6.2831853f * x); break; case 1: y = x < 0.25f ? 4 * x : x < 0.75f ? 2 - 4 * x : 4 * x - 4; break;
                         case 2: y = 1 - 2 * x; break; case 3: y = x < 0.5f ? 1.0f : -1.0f; break;
                         default: y = std::sin (std::floor (t * 12.0f) * 7.3f); break; }
            const float px = b.getX() + t * b.getWidth(), py = b.getCentreY() - y * b.getHeight() * 0.44f;
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        g.setColour (d.acc); g.strokePath (p, juce::PathStrokeType (2.0f));
        const float dy = b.getCentreY() - last * b.getHeight() * 0.44f;
        g.setColour (theme::ink); g.fillRoundedRectangle (b.getRight() - 6, dy - 3, 6, 6, 3);
    }
private:
    Ed& d; int lfo; float last = 2;
};

class EnvView : public Disp
{
public:
    EnvView (Ed& ed, int index) : Disp (ed, { envParam (index, EV_ATT), envParam (index, EV_DEC), envParam (index, EV_SUS), envParam (index, EV_REL), envParam (index, EV_CURVE) }), env (index) {}
    void draw (Graphics& g, Rectangle<float> b) override
    {
        const auto n = [&] (int k) { return d.p.param (envParam (env, k))->getValue(); };
        const float a = n (EV_ATT) * 0.33f, de = a + n (EV_DEC) * 0.33f, su = n (EV_SUS), r = de + 0.1f + n (EV_REL) * 0.23f;
        const float c = 0.6f + d.p.rawValue (envParam (env, EV_CURVE)) * 5.4f;
        const auto X = [&] (float x) { return b.getX() + x * b.getWidth(); }; const auto Y = [&] (float v) { return b.getBottom() - v * b.getHeight(); };
        Path p; p.startNewSubPath (X (0), Y (0)); p.lineTo (X (a), Y (1));
        for (int i = 1; i <= 50; ++i) { const float t = (float) i / 50.0f; p.lineTo (X (a + (de - a) * t), Y (su + (1 - su) * std::pow (1 - t, c))); }
        p.lineTo (X (de + 0.1f), Y (su));
        for (int i = 1; i <= 30; ++i) { const float t = (float) i / 30.0f; p.lineTo (X (de + 0.1f + (r - de - 0.1f) * t), Y (su * std::pow (1 - t, 3.0f))); }
        fillUnder (g, p, b); stroke (g, p);
    }
private:
    int env;
};

class LfoBox : public Ticking
{
public:
    LfoBox (Ed& ed, int i) : view (ed, i), shape (ed, lfoParam (i, LF_SHAPE)), sync (ed, lfoParam (i, LF_SYNC)), div (ed, lfoParam (i, LF_DIV), 4), retrig (ed, lfoParam (i, LF_RETRIG)),
                             rate (ed, lfoParam (i, LF_HZ), "Tempo")
    {
        for (juce::Component* c : std::initializer_list<juce::Component*> { &view, &shape, &sync, &div, &retrig, &rate }) addAndMakeVisible (c);
    }
    void resized() override
    {
        auto b = getLocalBounds();
        div.setBounds (b.removeFromBottom (40)); b.removeFromBottom (2);
        auto r = b.removeFromBottom (20); sync.setBounds (r.removeFromLeft (r.getWidth() / 2)); retrig.setBounds (r);
        b.removeFromBottom (2);
        shape.setBounds (b.removeFromBottom (20)); b.removeFromBottom (2);
        rate.setBounds (b.removeFromRight (56)); view.setBounds (b);
    }
    void tick() override { view.tick(); shape.tick(); sync.tick(); div.tick(); retrig.tick(); rate.tick(); }
private:
    LfoView view; Strip shape, sync, div, retrig; Knob rate;
};

// ---------------- Seite: Synth ---------------------------------------------
class SynthPage : public Ticking
{
public:
    SynthPage (Ed& ed, std::function<int (std::vector<float>&, std::vector<float>&, float&)> src, std::function<void (std::vector<float>&)> spec)
        : d (ed), wave (d, src), spectrum (d, spec)
    {
        addAndMakeVisible (wave); addAndMakeVisible (spectrum);
        auto add = [&] (const String& t) { panels.push_back (std::make_unique<Panel> (d, t)); addAndMakeVisible (*panels.back()); return panels.back().get(); };

        for (int o = 0; o < NUM_OSCS; ++o)
        {
            auto* p = add ("Oszillator " + String::charToString ((juce::juce_wchar) ('A' + o)));
            p->oscIndex = o;
            p->fill = p->own (std::make_unique<WaveDisp> (d, o));
            p->headStrip = p->own (std::make_unique<Strip> (d, modeParam (d.mode, O (o, O_SRC)))); p->headWidth = 130;
            p->knob (O (o, O_LVL), "Pegel"); p->knob (O (o, O_WAVE), "Welle"); p->knob (O (o, O_TUNE), "Tune"); p->knob (O (o, O_FINE), "Fein");
            p->knob (O (o, O_UNI), "Stimmen"); p->knob (O (o, O_DETUNE), "Verst."); p->knob (O (o, O_WIDTH), "Breite");
        }
        auto* sub = add ("Sub"); sub->fill = sub->own (std::make_unique<SubDisp> (d));
        sub->headStrip = sub->own (std::make_unique<Strip> (d, modeParam (d.mode, M_SUBOCT))); sub->headWidth = 100;
        sub->knob (M_SUBLVL, "Lautstärke");

        auto* out = add ("Ausgang");
        out->knob (M_LOWCUT, "Lowcut"); out->knob (M_VEL, "Velocity"); out->knob (M_LEVEL, "Pegel");

        auto* amp = add (U ("Lautstärke")); amp->fill = amp->own (std::make_unique<AmpDisp> (d));
        amp->headStrip = amp->own (std::make_unique<Strip> (d, modeParam (d.mode, M_GATE))); amp->headWidth = 220;
        amp->knob (M_LENGTH, "Länge"); amp->knob (M_ATT, "Attack"); amp->knob (M_HOLD, "Hold"); amp->knob (M_DEC, "Decay");
        amp->knob (M_SUS, "Sustain"); amp->knob (M_REL, "Release"); amp->knob (M_ACURVE, "Kurve");

        auto* pit = add (U ("Pitch-Hüllkurve")); pit->fill = pit->own (std::make_unique<PitchDisp> (d));
        pit->knob (M_PAMT, "Menge"); pit->knob (M_PTIME, "Zeit"); pit->knob (M_PCURVE, "Kurve");

        auto* fil = add ("Filter"); fil->fill = fil->own (std::make_unique<FilterDisp> (d));
        fil->headStrip = fil->own (std::make_unique<Strip> (d, modeParam (d.mode, M_FTYPE))); fil->headWidth = 130;
        fil->knob (M_CUT, "Cutoff"); fil->knob (M_RES, "Reso"); fil->knob (M_FENV, "Env"); fil->knob (M_FDEC, "Env Zeit");

        if (modeIsMono (d.mode))
        {
            auto* clk = add ("Click"); clk->fill = clk->own (std::make_unique<ClickDisp> (d));
            clk->knob (M_CLKLVL, "Pegel"); clk->knob (M_CLKTONE, "Ton"); clk->knob (M_CLKLEN, "Länge"); clk->knob (M_CLKNOISE, "Rauschen");
        }
        else
        {
            auto* cho = add ("Akkord");
            cho->fill = cho->own (std::make_unique<ChordBox> (d));
            cho->knob (M_STRUM, "Strum");
        }
        auto* drv = add ("Drive"); drv->fill = drv->own (std::make_unique<DriveDisp> (d));
        drv->headStrip = drv->own (std::make_unique<Strip> (d, modeParam (d.mode, M_DRVTYPE))); drv->headWidth = 108;
        drv->knob (M_DRV, "Drive"); drv->knob (M_DRVTONE, "Ton");

        for (int i = 0; i < NUM_LFOS; ++i)
        {
            auto* p = add ("LFO " + String (i + 1));
            p->fill = p->own (std::make_unique<LfoBox> (d, i));
        }
        for (int i = 0; i < NUM_ENVS; ++i)
        {
            auto* p = add ("Env " + String (i + 1));
            p->fill = p->own (std::make_unique<EnvView> (d, i));
            p->knobAbs (envParam (i, EV_ATT), "Attack"); p->knobAbs (envParam (i, EV_DEC), "Decay"); p->knobAbs (envParam (i, EV_SUS), "Sustain");
            p->knobAbs (envParam (i, EV_REL), "Release"); p->knobAbs (envParam (i, EV_CURVE), "Kurve");
        }
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
        auto topRow = b.removeFromTop (116);
        wave.setBounds (topRow.removeFromLeft (topRow.getWidth() * 45 / 100)); topRow.removeFromLeft (8); spectrum.setBounds (topRow);
        b.removeFromTop (8);
        const int rowH = (b.getHeight() - 16) / 3;
        auto r1 = b.removeFromTop (rowH); b.removeFromTop (8);
        auto r2 = b.removeFromTop (rowH); b.removeFromTop (8);
        const auto place = [] (juce::Rectangle<int>& row, Panel& p, int w) { p.setBounds (row.removeFromLeft (w)); row.removeFromLeft (8); };
        size_t i = 0;
        for (int o = 0; o < NUM_OSCS; ++o) place (r1, *panels[i++], 306);
        place (r1, *panels[i++], 166); panels[i++]->setBounds (r1);
        place (r2, *panels[i++], 400); place (r2, *panels[i++], 180); place (r2, *panels[i++], 270); place (r2, *panels[i++], 210); panels[i++]->setBounds (r2);
        for (int k = 0; k < NUM_LFOS; ++k) place (b, *panels[i++], 186);
        const int ew = (b.getWidth() - 16) / NUM_ENVS;
        for (int k = 0; k < NUM_ENVS; ++k) place (b, *panels[i++], ew);
    }
private:
    Ed& d; WaveShapeView wave; Spectrum spectrum;
    std::vector<std::unique_ptr<Panel>> panels;
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
                g.drawText (U ("Wähle oben einen Effekt für diesen Slot."), b, juce::Justification::centred, false); return;
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
    FxRow (Ed& ed, int slotIndex, std::function<void (int)> sel, std::function<void (int, int)> mv) : d (ed), slot (slotIndex), select (std::move (sel)), move (std::move (mv)) {}
    void paint (Graphics& g) override
    {
        const int t = (int) d.p.rawValue (slotParam (slot, S_TYPE)); const bool on = d.p.rawValue (slotParam (slot, S_ON)) > 0.5f, sel = d.e.selectedSlot == slot;
        auto b = getLocalBounds().toFloat();
        g.setColour (sel ? d.acc.withAlpha (0.22f) : theme::plateHi.withAlpha (0.5f)); g.fillRoundedRectangle (b, 6.0f);
        if (sel) { g.setColour (d.acc); g.fillRoundedRectangle (b.withWidth (4.0f), 2.0f); }
        g.setColour (theme::dim); g.setFont (theme::font (13.0f)); g.drawText (String (slot + 1), 12, 0, 18, getHeight(), juce::Justification::centredLeft, false);
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
        const int to = juce::jlimit (0, NUM_SLOTS - 1, slot + (int) std::floor ((float) e.y / (float) getHeight()));
        if (to != slot) move (slot, to);
    }
    void tick() override
    {
        float sum = 0; for (int k : { S_ON, S_TYPE, S_MIX }) sum = sum * 7 + d.p.rawValue (slotParam (slot, k));
        if (sum != last) { last = sum; repaint(); }
    }
private:
    Ed& d; int slot; float last = -1; std::function<void (int)> select; std::function<void (int, int)> move;
};

/** Effektauswahl als Raster: Beim Wechsel starten die Regler mit sinnvollen Werten. */
class FxTypeGrid : public Ticking
{
public:
    FxTypeGrid (Ed& ed, int slotIndex, std::function<void()> changed) : d (ed), slot (slotIndex), onChange (std::move (changed)) {}
    void paint (Graphics& g) override
    {
        const int cur = (int) d.p.rawValue (slotParam (slot, S_TYPE));
        const float w = (float) getWidth() / 7.0f, h = (float) getHeight() / 2.0f;
        for (int i = 0; i < FX_TYPES; ++i)
        {
            Rectangle<float> r ((float) (i % 7) * w, (float) (i / 7) * h, w, h); r.reduce (2, 2);
            g.setColour (i == cur ? d.acc : theme::well); g.fillRoundedRectangle (r, 5.0f);
            g.setColour (i == cur ? theme::well : theme::ink); g.setFont (theme::font (13.5f, i == cur));
            g.drawText (U (fxNames()[(size_t) i]), r, juce::Justification::centred, false);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, FX_TYPES - 1, e.y * 2 / getHeight() * 7 + e.x * 7 / getWidth());
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
        for (int s = 0; s < NUM_SLOTS; ++s)
        {
            rows.push_back (std::make_unique<FxRow> (d, s, [this] (int x) { d.e.selectedSlot = x; d.e.requestRebuild(); },
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
        const int h = left.getHeight() / NUM_SLOTS;
        for (auto& r : rows) r->setBounds (left.removeFromTop (h).reduced (0, 3));
        grid->setBounds (b.removeFromTop (96)); b.removeFromTop (8);
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
    std::vector<std::unique_ptr<Knob>> knobs;
    std::unique_ptr<FxTypeGrid> grid; std::unique_ptr<Spectrum> analyzer; std::unique_ptr<FxVisual> visual;
};

// ---------------- Seite: Automation ----------------------------------------
class CurveEdit : public Ticking
{
public:
    CurveEdit (Ed& ed, int laneIndex) : d (ed), lane (laneIndex) {}
    void tick() override
    {
        const float p = d.p.engine.lanePos[lane].load (std::memory_order_relaxed);
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
        for (int i = 0; i < LANE_POINTS; ++i)
        {
            const float x = b.getX() + b.getWidth() * (float) i / (float) (LANE_POINTS - 1), y = b.getBottom() - c[(size_t) i].load() * b.getHeight();
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        Path f (p); f.lineTo (b.getRight(), b.getBottom()); f.lineTo (b.getX(), b.getBottom()); f.closeSubPath();
        g.setColour (d.acc.withAlpha (0.18f)); g.fillPath (f);
        g.setColour (d.acc); g.strokePath (p, juce::PathStrokeType (2.0f));
        if (d.p.rawValue (laneParam (lane, L_ON)) > 0.5f)
        {
            g.setColour (theme::ink);
            g.drawVerticalLine ((int) (b.getX() + b.getWidth() * lastPos), b.getY(), b.getBottom());
        }
    }
    void paintAt (const juce::MouseEvent& e)
    {
        auto b = getLocalBounds().toFloat().reduced (6.0f);
        const int i = juce::jlimit (0, LANE_POINTS - 1, (int) std::round (((float) e.x - b.getX()) / b.getWidth() * (float) (LANE_POINTS - 1)));
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

class LaneRow : public Ticking
{
public:
    LaneRow (Ed& ed, int laneIndex) : d (ed), lane (laneIndex), curve (ed, laneIndex), on (ed, laneParam (laneIndex, L_ON)),
          rate (ed, laneParam (laneIndex, L_RATE), 4), trig (ed, laneParam (laneIndex, L_TRIG)), amount (ed, laneParam (laneIndex, L_AMOUNT), "Menge")
    {
        for (auto* c : std::initializer_list<juce::Component*> { &curve, &on, &rate, &trig, &amount, &target, &learn }) addAndMakeVisible (c);
        style (target); style (learn);
        target.onClick = [this] { pick(); };
        learn.setButtonText (d.e.learnLane == lane ? U ("Klick jetzt einen Regler") : U ("Ziel per Klick"));
        learn.onClick = [this] { d.e.learnLane = d.e.learnLane == lane ? -1 : lane; d.e.showPage (d.e.learnLane >= 0 ? 0 : 4); };
        static const char* shapes[] = { "Runter", "Hoch", "Dreieck", "Sinus", "Gate", "Zufall", "Leer" };
        for (int i = 0; i < 7; ++i)
        {
            auto b = std::make_unique<juce::TextButton> (U (shapes[i])); style (*b);
            b->onClick = [this, i] { d.p.shapeLane (lane, i); curve.repaint(); };
            addAndMakeVisible (*b); shape.push_back (std::move (b));
        }
        refresh();
    }
    void style (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, theme::well); b.setColour (juce::TextButton::textColourOffId, theme::ink);
        b.setColour (juce::ComboBox::outlineColourId, theme::line);
    }
    void refresh() { target.setButtonText (d.p.targetName ((int) d.p.rawValue (laneParam (lane, L_TARGET)))); }
    void pick()
    {
        juce::PopupMenu m, modeMenu, fx, glob;
        m.addItem (1, U ("Kein Ziel"));
        const auto& t = params();
        for (int i = 0; i < M_COUNT; ++i) if (t[(size_t) modeParam (d.mode, i)].modulatable()) modeMenu.addItem (modeParam (d.mode, i) + 2, U (t[(size_t) modeParam (d.mode, i)].name));
        m.addSubMenu (U (modeName (d.mode)), modeMenu);
        for (int s = 0; s < NUM_SLOTS; ++s)
        {
            const int ty = (int) d.p.rawValue (slotParam (s, S_TYPE)); if (ty == 0) continue;
            juce::PopupMenu sm;
            sm.addItem (slotParam (s, S_MIX) + 2, "Mix");
            for (int k = 0; k < FX_PARAMS; ++k) if (fxInfo (ty)[k].label[0]) sm.addItem (slotParam (s, S_P1 + k) + 2, U (fxInfo (ty)[k].label));
            m.addSubMenu ("FX " + String (s + 1) + " " + U (fxNames()[(size_t) ty]), sm);
        }
        m.addItem (G_MASTER + 2, "Master");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [this] (int r)
        {
            if (r == 0) return;
            auto* p = d.p.param (laneParam (lane, L_TARGET));
            p->setValueNotifyingHost (p->convertTo0to1 ((float) (r - 2)));
            auto* o = d.p.param (laneParam (lane, L_ON));
            if (r >= 2) o->setValueNotifyingHost (1.0f);
            refresh();
        });
    }
    void paint (Graphics& g) override
    {
        g.setColour (theme::plateHi.withAlpha (0.5f)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    }
    void resized() override
    {
        auto b = getLocalBounds().reduced (8);
        auto left = b.removeFromLeft (320); b.removeFromLeft (8);
        auto r = left.removeFromTop (26); on.setBounds (r.removeFromLeft (84)); r.removeFromLeft (6); target.setBounds (r);
        left.removeFromTop (5);
        auto r2 = left.removeFromTop (26); learn.setBounds (r2); left.removeFromTop (5);
        amount.setBounds (left.removeFromRight (70));
        rate.setBounds (left.removeFromTop (juce::jmin (52, left.getHeight() - 28))); trig.setBounds (left.removeFromTop (26).withTrimmedRight (0));
        auto sh = b.removeFromBottom (26); const int w = sh.getWidth() / 7;
        for (auto& s : shape) s->setBounds (sh.removeFromLeft (w).reduced (2, 0));
        b.removeFromBottom (4); curve.setBounds (b);
    }
private:
    Ed& d; int lane; CurveEdit curve; Strip on, rate, trig; Knob amount; juce::TextButton target, learn;
    std::vector<std::unique_ptr<juce::TextButton>> shape;
};

class AutoPage : public Ticking
{
public:
    explicit AutoPage (Ed& ed)
    {
        for (int l = 0; l < NUM_LANES; ++l) { rows.push_back (std::make_unique<LaneRow> (ed, l)); addAndMakeVisible (*rows.back()); }
    }
    void resized() override
    {
        auto b = getLocalBounds(); const int h = b.getHeight() / NUM_LANES;
        for (auto& r : rows) r->setBounds (b.removeFromTop (h).reduced (0, 4));
    }
private:
    std::vector<std::unique_ptr<LaneRow>> rows;
};

// ---------------- Quellen-Leiste: immer sichtbar, von hier auf Regler ziehen ----
class SourceChip : public Ticking
{
public:
    SourceChip (Ed& ed, int sourceIndex) : d (ed), src (sourceIndex) {}
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (theme::well); g.fillRoundedRectangle (b, 5.0f);
        const float v = src < NUM_LFOS ? 0.5f + 0.5f * val : val;
        g.setColour (d.acc.withAlpha (0.35f)); g.fillRoundedRectangle (b.withWidth (b.getWidth() * juce::jlimit (0.0f, 1.0f, v)), 5.0f);
        g.setColour (theme::ink); g.setFont (theme::font (13.0f, true));
        g.drawText (U (sourceName (src)), b, juce::Justification::centred, false);
    }
    void tick() override
    {
        const float v = d.p.engine.sourceView[src].load (std::memory_order_relaxed);
        if (std::fabs (v - val) > 0.01f) { val = v; repaint(); }
    }
    void mouseDrag (const juce::MouseEvent&) override
    {
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! c->isDragAndDropActive())
                c->startDragging ("tkmod:" + String (src), this);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mouseWasDraggedSinceMouseDown()) d.e.showPage (3); }
    juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::DraggingHandCursor; }
private:
    Ed& d; int src; float val = 0;
};

class SourceStrip : public Ticking
{
public:
    explicit SourceStrip (Ed& ed)
    {
        for (int s = 0; s < NUM_SOURCES; ++s) { chips.push_back (std::make_unique<SourceChip> (ed, s)); addAndMakeVisible (*chips.back()); }
    }
    void paint (Graphics& g) override
    {
        g.setColour (theme::dim); g.setFont (theme::font (13.0f));
        g.drawText (U ("Auf einen Regler ziehen:"), 0, 0, 170, getHeight(), juce::Justification::centredLeft, false);
    }
    void resized() override
    {
        auto b = getLocalBounds().withTrimmedLeft (174); const int w = b.getWidth() / NUM_SOURCES;
        for (auto& c : chips) c->setBounds (b.removeFromLeft (w).reduced (3, 2));
    }
private:
    std::vector<std::unique_ptr<SourceChip>> chips;
};

// ---------------- Seite: Sample --------------------------------------------
class SampleWave : public Ticking
{
public:
    explicit SampleWave (Ed& ed) : d (ed), osc (ed.e.sampleOsc), start { ed.p.param (modeParam (ed.mode, O (ed.e.sampleOsc, O_START))) }
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
    explicit SamplePage (Ed& ed) : d (ed), wave (ed), osc (ed.e.sampleOsc), pitch (ed, modeParam (ed.mode, O (ed.e.sampleOsc, O_PITCH))), loop (ed, modeParam (ed.mode, O (ed.e.sampleOsc, O_LOOP))), len (ed, modeParam (ed.mode, O (ed.e.sampleOsc, O_FREE)))
    {
        addAndMakeVisible (wave); addAndMakeVisible (pitch); addAndMakeVisible (loop); addAndMakeVisible (len);
        for (auto [p, n] : std::initializer_list<std::pair<int, const char*>> { { O (osc, O_LVL), "Pegel" }, { O (osc, O_TUNE), "Tune" }, { O (osc, O_FINE), "Fein" }, { O (osc, O_ROOT), "Grundton" }, { O (osc, O_START), "Start" } })
        {
            knobs.push_back (std::make_unique<Knob> (d, modeParam (d.mode, p), U (n)));
            addAndMakeVisible (*knobs.back());
        }
        styleButton (load); styleButton (clear);
        load.setButtonText (U ("Sample laden")); clear.setButtonText (U ("Sample entfernen"));
        load.onClick = [this] { d.e.chooseSample (osc); };
        clear.onClick = [this] { d.p.clearSample (d.mode, osc); d.e.status = {}; };
        for (int o = 0; o < NUM_OSCS; ++o)
        {
            auto b = std::make_unique<juce::TextButton> ("Oszillator " + String::charToString ((juce::juce_wchar) ('A' + o)));
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
        const String hint = d.e.status.isNotEmpty() ? d.e.status : U ("Jeder Oszillator hat sein eigenes Sample. Es läuft durch Filter, Drive und FX und wird im Projekt und im Preset mitgespeichert.");
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
            rows[(size_t) i]->setBounds (list.getX() + (i / 8) * (rowW + 8), list.getY() + (i % 8) * 40, rowW, 34);
    }
    void paint (Graphics& g) override
    {
        g.setColour (theme::ink); g.setFont (theme::font (16.0f, true));
        g.drawText (U ("Verbindungen"), 0, 2, 300, 22, juce::Justification::centredLeft, false);
        if (! rows.empty()) return;
        g.setColour (theme::dim); g.setFont (theme::font (16.0f));
        g.drawText (U ("Noch keine Verbindung. Zieh einen LFO oder eine Env aus der Leiste unten auf einen Regler."), getLocalBounds(), juce::Justification::centred, false);
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
            for (int o = NUM_OSCS - 1; o >= 0; --o) if (d.p.samples[sampleSlot (d.mode, o)].load() != nullptr) useOsc = o;
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
    TopBar (TekkforgeEditor& editor, Ed& ed) : e (editor), d (ed), quality (d, G_QUALITY), master (d, G_MASTER, "Master")
    {
        addAndMakeVisible (quality); addAndMakeVisible (master);
    }
    Rectangle<int> modeArea() const { return { 136, 8, 400, 50 }; }
    Rectangle<int> pageArea() const { return { 544, 8, 386, 50 }; }
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
            g.drawText (U (m == 0 ? "Melo" : m == 1 ? "Stab" : m == 2 ? "Topkick" : m == 3 ? "Kickbass" : "Full Kick"), r, juce::Justification::centred, false);
        }
        static const char* pages[] = { "Synth", "Sample", "FX", "Mod", "Auto", "Guide", "Presets" };
        auto pa = pageArea(); const int pw = pa.getWidth() / 7;
        for (int i = 0; i < 7; ++i)
        {
            auto r = pa.removeFromLeft (pw).reduced (2, 8).toFloat();
            const bool sel = i == e.getPage();
            g.setColour (sel ? theme::ink : theme::plateHi); g.fillRoundedRectangle (r, 5.0f);
            g.setColour (sel ? theme::well : theme::ink); g.setFont (theme::font (13.0f, sel));
            g.drawFittedText (pages[i], r.toNearestInt().reduced (1, 0), juce::Justification::centred, 1, 0.8f);
        }
        // Note und Frequenz fuer sauberes Stimmen
        const int note = d.p.lastNote.load();
        auto info = Rectangle<int> (1034, 8, getWidth() - 1034 - 152, 50);
        if (note >= 0)
        {
            const float tune = d.p.rawValue (modeParam (d.mode, M_TUNE)) + d.p.rawValue (modeParam (d.mode, M_FINE)) * 0.01f;
            const float hz = 440.0f * std::pow (2.0f, ((float) note + tune - 69.0f) / 12.0f);
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
            e.showPage ((ev.x - pageArea().getX()) * 7 / pageArea().getWidth());
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
    top.reset(); page.reset(); sources.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void TekkforgeEditor::rebuild()
{
    const int mode = (int) proc.rawValue (G_MODE);
    lastMode = mode;
    page.reset(); top.reset(); sources.reset();
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
    sources = std::make_unique<SourceStrip> (d);
    addAndMakeVisible (*sources);

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
        case 4:  page = std::make_unique<AutoPage> (d); break;
        case 5:  page = std::make_unique<GuidePage> (d, specSrc); break;
        case 6:  page = std::make_unique<PresetsPage> (d); break;
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
    if (learnLane >= 0)
    {
        g.setColour (theme::accent ((int) proc.rawValue (G_MODE)));
        g.drawRect (getLocalBounds(), 3);
    }
}

void TekkforgeEditor::resized()
{
    auto b = getLocalBounds();
    if (top != nullptr) top->setBounds (b.removeFromTop (66));
    keys->setBounds (b.removeFromBottom (72).reduced (10, 0).withTrimmedBottom (8));
    if (sources != nullptr) sources->setBounds (b.removeFromBottom (32).reduced (10, 1));
    if (update != nullptr) update->setBounds (10, b.getBottom() + 2, 170, 28);
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
        set (O (osc, O_SRC), 1.0f);
        if (proc.rawValue (modeParam (mode, O (osc, O_LVL))) <= 0.0f) set (O (osc, O_LVL), 1.0f);
        status = msg.isNotEmpty() ? msg : U ("Sample geladen. Es läuft auf diesem Oszillator, die anderen bleiben aktiv.");
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
    for (int i = 0; i < NUM_PARAMS; ++i) if (now[i] != lastValues[i]) { changed = true; lastValues[i] = now[i]; }
    const int defaultNote = modeIsMono (mode) ? 29 : 60;
    const int ln = proc.lastNote.load(), note = ln >= 0 ? ln : defaultNote;
    if (auto* t = dynamic_cast<Ticking*> (sources.get())) t->tick();
    if (changed || note != lastPreviewNote) { lastPreviewNote = note; preview->request (now, note); }

    if (auto* t = dynamic_cast<Ticking*> (page.get())) t->tick();
    if (top != nullptr) top->tick();
}
