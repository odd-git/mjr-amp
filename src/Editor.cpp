// Editor: top bar (capture selection, library, in/out), Amp view (panel with the six
// knobs), Cab view (four IR slots). Layout and colours are our own.
#include "Processor.h"
#include "BinaryData.h"
#include "Pitch.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
namespace col
{
const juce::Colour bg { 0xff121212 }, bar { 0xff1b1b1b }, line { 0xff2c2c2c }, text { 0xffd9d4c7 },
    dim { 0xff8a857a }, accent { 0xffd8b25a }, plate { 0xffe4d29b }, tolex { 0xff1e1d1b };
}

// Knob centres on the amp head image (1034 x 390), same for the photo and the drawn head.
constexpr float knobX[6] = { 413, 466.5f, 520, 574, 628, 682 };
constexpr float knobY = 319.5f;
constexpr float panelW = 1034, panelH = 390, knobR = 16;

struct Look : juce::LookAndFeel_V4
{
    Look()
    {
        setColour(juce::ComboBox::backgroundColourId, col::bar);
        setColour(juce::ComboBox::outlineColourId, col::line);
        setColour(juce::ComboBox::textColourId, col::text);
        setColour(juce::ComboBox::arrowColourId, col::accent);
        setColour(juce::PopupMenu::backgroundColourId, col::bar);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha(0.25f));
        setColour(juce::TextButton::buttonColourId, col::bar);
        setColour(juce::TextButton::buttonOnColourId, col::accent);
        setColour(juce::TextButton::textColourOffId, col::text);
        setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        setColour(juce::ToggleButton::tickColourId, col::accent);
        setColour(juce::Slider::rotarySliderFillColourId, col::accent);
        setColour(juce::Slider::textBoxTextColourId, col::text);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::BubbleComponent::backgroundColourId, col::bar);
        setColour(juce::BubbleComponent::outlineColourId, col::line);
        setColour(juce::TooltipWindow::textColourId, col::text);
    }

    // Amp knobs ("amp" property): chrome cap on a knurled brass ring, like the panel's.
    // Other rotaries: a thin arc.
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
    {
        const auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(2);
        const auto c = r.getCentre();
        const float rad = std::min(r.getWidth(), r.getHeight()) * 0.5f;
        const float a = start + pos * (end - start);

        if (!s.getProperties().contains("amp"))
        {
            juce::Path track, fill;
            track.addCentredArc(c.x, c.y, rad - 2, rad - 2, 0, start, end, true);
            fill.addCentredArc(c.x, c.y, rad - 2, rad - 2, 0, start, a, true);
            g.setColour(juce::Colour(0xff3a3a3a));
            g.strokePath(track, juce::PathStrokeType(4, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            g.setColour(juce::Colour(0xffb8b8b8));
            g.strokePath(fill, juce::PathStrokeType(4, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            const float ring = rad * 0.72f;
            g.setColour(juce::Colour(0xff0c0c0c));
            g.fillEllipse(juce::Rectangle<float>(ring * 2, ring * 2).withCentre(c));
            g.setColour(juce::Colours::white);
            g.drawEllipse(juce::Rectangle<float>(ring * 2, ring * 2).withCentre(c), juce::jmax(2.0f, rad * 0.07f));
            g.fillEllipse(juce::Rectangle<float>(rad * 0.13f, rad * 0.13f).withCentre(c.getPointOnCircumference(ring * 0.72f, a)));
            return;
        }

        g.setColour(juce::Colours::black.withAlpha(0.35f)); // shadow cast downwards, as on the panel
        g.fillEllipse(juce::Rectangle<float>(rad * 1.8f, rad * 2.2f).withCentre(c.translated(0, rad * 0.45f)));

        g.setColour(juce::Colour(0xff6b5a2e)); // knurled brass ring
        g.fillEllipse(juce::Rectangle<float>(rad * 2, rad * 2).withCentre(c));
        g.setColour(juce::Colour(0xff2e2614));
        for (int i = 0; i < 40; ++i)
        {
            const float t = (float)i * juce::MathConstants<float>::twoPi / 40.0f;
            g.drawLine(juce::Line<float>(c.getPointOnCircumference(rad * 0.8f, t), c.getPointOnCircumference(rad, t)), 0.8f);
        }

        const float cap = rad * 0.8f; // chrome cap with a spun-metal sheen
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff4f4f2), c.x - cap * 0.6f, c.y - cap * 0.8f,
                                               juce::Colour(0xff7d7d7a), c.x + cap * 0.6f, c.y + cap, false));
        g.fillEllipse(juce::Rectangle<float>(cap * 2, cap * 2).withCentre(c));
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        for (float t : { -2.4f, -0.8f, 0.7f, 2.3f })
            g.drawLine(juce::Line<float>(c, c.getPointOnCircumference(cap * 0.95f, t)), juce::jmax(1.0f, rad * 0.08f));
        g.setColour(juce::Colour(0xff1a1a1a));
        g.drawLine(juce::Line<float>(c.getPointOnCircumference(cap * 0.2f, a), c.getPointOnCircumference(cap * 0.95f, a)), juce::jmax(1.5f, rad * 0.12f));
    }
};

struct Knob : juce::Slider
{
    Knob(juce::AudioProcessorValueTreeState& params, const juce::String& id, bool amp)
        : juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
          attachment(params, id, *this)
    {
        if (amp)
            getProperties().set("amp", true);
        setPopupDisplayEnabled(true, true, nullptr);
        setTooltip(params.getParameter(id)->getName(64));
    }
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

struct Meter : juce::Component
{
    float level = -60; // dB, decays in the editor timer
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(col::line);
        g.fillRoundedRectangle(r, 2);
        const float t = juce::jlimit(0.0f, 1.0f, (level + 60.0f) / 60.0f);
        g.setColour(level > -1.0f ? juce::Colours::red : col::accent);
        g.fillRoundedRectangle(r.removeFromBottom(r.getHeight() * t), 2);
    }
};

// A knob with its name and current value underneath.
struct LabeledKnob : juce::Component
{
    LabeledKnob(juce::AudioProcessorValueTreeState& params, const juce::String& id, const juce::String& name)
        : knob(params, id, false), unit(params.getParameter(id)->getLabel())
    {
        label.setText(name, juce::dontSendNotification);
        for (auto* l : { &label, &value })
        {
            l->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(*l);
        }
        label.setColour(juce::Label::textColourId, col::text);
        label.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        value.setColour(juce::Label::textColourId, col::dim);
        value.setFont(juce::FontOptions(12.0f));
        knob.onValueChange = [this] { showValue(); };
        addAndMakeVisible(knob);
        showValue();
    }
    void showValue()
    {
        const auto text = knob.getTextFromValue(knob.getValue());
        value.setText(unit.isEmpty() || !text.containsAnyOf("0123456789") ? text : text + " " + unit, juce::dontSendNotification);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        value.setBounds(r.removeFromBottom(16));
        label.setBounds(r.removeFromBottom(16));
        knob.setBounds(r.withSizeKeepingCentre(std::min(r.getWidth(), r.getHeight()), std::min(r.getWidth(), r.getHeight())));
    }
    Knob knob;
    juce::Label label, value;
    juce::String unit;
};

// Top-bar button: drawn icon with its caption underneath.
struct IconButton : juce::Button
{
    enum Icon { tuner, metronome, folder, audio };
    IconButton(const juce::String& caption, Icon i) : juce::Button(caption), icon(i) {}
    bool lit = false; // metronome beat light
    bool downbeat = false;

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        const auto c = getToggleState() ? col::accent : (over ? juce::Colours::white : col::text);
        g.setColour(down ? c.darker(0.3f) : c);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(getName(), r.removeFromBottom(16), juce::Justification::centred);
        const auto b = r.withSizeKeepingCentre(30, 30);
        juce::Path p;
        switch (icon)
        {
            case tuner: // tuning fork
                p.startNewSubPath(b.getX() + 8, b.getY());
                p.lineTo(b.getX() + 8, b.getY() + 14);
                p.quadraticTo(b.getCentreX(), b.getY() + 24, b.getRight() - 8, b.getY() + 14);
                p.lineTo(b.getRight() - 8, b.getY());
                p.startNewSubPath(b.getCentreX(), b.getY() + 19);
                p.lineTo(b.getCentreX(), b.getBottom());
                break;
            case metronome:
                p.startNewSubPath(b.getX() + 11, b.getY());
                p.lineTo(b.getRight() - 11, b.getY());
                p.lineTo(b.getRight() - 4, b.getBottom());
                p.lineTo(b.getX() + 4, b.getBottom());
                p.closeSubPath();
                p.startNewSubPath(b.getCentreX(), b.getBottom() - 6);
                p.lineTo(b.getRight() - 6, b.getY() + 4);
                break;
            case audio: // speaker
                p.addRectangle(b.getX() + 3, b.getY() + 10, 7, 10);
                p.startNewSubPath(b.getX() + 10, b.getY() + 10);
                p.lineTo(b.getX() + 18, b.getY() + 3);
                p.lineTo(b.getX() + 18, b.getBottom() - 3);
                p.lineTo(b.getX() + 10, b.getY() + 20);
                p.addCentredArc(b.getX() + 18, b.getCentreY(), 6, 6, 0, 0.6f, 2.55f, true);
                p.addCentredArc(b.getX() + 18, b.getCentreY(), 11, 11, 0, 0.6f, 2.55f, true);
                break;
            case folder:
                p.startNewSubPath(b.getX() + 1, b.getY() + 5);
                p.lineTo(b.getX() + 11, b.getY() + 5);
                p.lineTo(b.getX() + 14, b.getY() + 9);
                p.lineTo(b.getRight() - 1, b.getY() + 9);
                p.lineTo(b.getRight() - 1, b.getBottom() - 3);
                p.lineTo(b.getX() + 1, b.getBottom() - 3);
                p.closeSubPath();
                break;
        }
        g.strokePath(p, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (lit)
        {
            g.setColour(downbeat ? col::accent : juce::Colours::white);
            g.fillEllipse(juce::Rectangle<float>(7, 7).withCentre({ b.getRight() + 6, b.getY() + 2 }));
        }
    }
    Icon icon;
};

// Input channel switch: which interface input the guitar is plugged into.
struct ChannelSwitch : juce::ToggleButton
{
    ChannelSwitch() : juce::ToggleButton("Input source") { setTooltip("Interface input with the guitar"); }
    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.setColour(col::text);
        g.drawText("INPUT CH", r.removeFromBottom(18), juce::Justification::centred);
        auto row = r.withSizeKeepingCentre(r.getWidth(), 26);
        const bool two = getToggleState();
        g.setColour(two ? col::dim : col::accent);
        g.drawText("1", row.removeFromLeft(22), juce::Justification::centred);
        g.setColour(two ? col::accent : col::dim);
        g.drawText("2", row.removeFromRight(22), juce::Justification::centred);
        auto pill = row.withSizeKeepingCentre(44, 22);
        g.setColour(over ? col::text : juce::Colour(0xffd0d0d0));
        g.drawRoundedRectangle(pill.reduced(1), 11, 2);
        g.fillEllipse(juce::Rectangle<float>(14, 14).withCentre({ two ? pill.getRight() - 11 : pill.getX() + 11, pill.getCentreY() }));
    }
};

// The rig: amp head (photo or drawn) standing on a drawn cabinet. The cabinet's top band
// names each amp knob and shows its value, right under the knob.
class Rig : public juce::Component
{
public:
    Rig(MJRAmpProcessor& p)
    {
#if MJR_HAS_PHOTO
        photo = juce::ImageCache::getFromMemory(BinaryData::amp_png, BinaryData::amp_pngSize);
#endif
        for (int i = 0; i < 6; ++i)
        {
            auto* k = knobs.add(new Knob(p.params, MJRAmpProcessor::knobIds[i], true));
            k->onValueChange = [this] { repaint(band.toNearestInt()); };
            addAndMakeVisible(k);
        }
        missing.setJustificationType(juce::Justification::centred);
        missing.setColour(juce::Label::textColourId, col::accent);
        missing.setColour(juce::Label::backgroundColourId, col::bg.withAlpha(0.85f));
        addChildComponent(missing);
    }

    juce::Label missing;

    void paint(juce::Graphics& g) override
    {
        paintCab(g);
        const auto area = head();
        if (photo.isValid())
            g.drawImage(photo, area);
        else
            paintDrawnHead(g, area);

        const char* names[] = { "PRESENCE", "BASS", "MIDDLE", "TREBLE", "VOL I", "VOL II" };
        for (int i = 0; i < 6; ++i)
        {
            const float x = knobCentre(i).x;
            g.setColour(col::text);
            g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            g.drawText(names[i], juce::Rectangle<float>(60, 14).withCentre({ x, band.getY() + 13 }), juce::Justification::centred);
            g.setColour(col::accent);
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(juce::String(knobs[i]->getValue(), 1), juce::Rectangle<float>(60, 14).withCentre({ x, band.getY() + 28 }), juce::Justification::centred);
        }
    }

    void resized() override
    {
        const auto area = head();
        const float s = area.getWidth() / panelW;
        for (int i = 0; i < 6; ++i)
            knobs[i]->setBounds(juce::Rectangle<float>(knobR * 2.4f * s, knobR * 2.4f * s).withCentre(knobCentre(i)).toNearestInt());
        band = { area.getX() + 40, area.getBottom() + 8, area.getWidth() - 80, 40 };
        missing.setBounds(area.reduced(area.getWidth() * 0.25f, area.getHeight() * 0.3f).toNearestInt());
    }

private:
    juce::Rectangle<float> head() const
    {
        const float w = (float)getWidth();
        return { 0, 0, w, w * panelH / panelW };
    }
    juce::Point<float> knobCentre(int i) const
    {
        const auto a = head();
        return { a.getX() + knobX[i] / panelW * a.getWidth(), a.getY() + knobY / panelH * a.getHeight() };
    }

    void paintCab(juce::Graphics& g)
    {
        auto cab = getLocalBounds().toFloat().withTop(head().getBottom()).reduced(14, 0).withTrimmedBottom(-20);
        g.setColour(juce::Colour(0xff151515));
        g.fillRoundedRectangle(cab, 10);
        g.setColour(juce::Colour(0xff2e2e2e));
        g.drawRoundedRectangle(cab.reduced(1), 10, 2);

        // Grille cloth: dark basket weave with a soft light from the top.
        auto grille = cab.reduced(30).withTrimmedTop(32);
        g.setColour(juce::Colour(0xff26231f));
        g.fillRect(grille);
        g.saveState();
        g.reduceClipRegion(grille.toNearestInt());
        g.setColour(juce::Colour(0xff36322b));
        for (float x = grille.getX() - grille.getHeight(); x < grille.getRight(); x += 5)
        {
            g.drawLine(x, grille.getY(), x + grille.getHeight(), grille.getBottom(), 1.0f);
            g.drawLine(x + grille.getHeight(), grille.getY(), x, grille.getBottom(), 1.0f);
        }
        g.setGradientFill(juce::ColourGradient(juce::Colours::transparentBlack, grille.getCentreX(), grille.getY() + grille.getHeight() * 0.3f,
                                               juce::Colours::black.withAlpha(0.75f), grille.getX(), grille.getBottom(), true));
        g.fillRect(grille);
        g.restoreState();
        g.setColour(col::accent.withAlpha(0.35f)); // piping
        g.drawRect(grille.expanded(3), 1.5f);

        for (auto corner : { cab.getTopLeft(), cab.getTopRight() }) // metal corner protectors
        {
            auto r = juce::Rectangle<float>(34, 34).withCentre(corner.translated(corner.x < cab.getCentreX() ? 14 : -14, 14));
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff5a5a5a), r.getX(), r.getY(), juce::Colour(0xff1c1c1c), r.getRight(), r.getBottom(), false));
            g.fillRoundedRectangle(r, 7);
        }
    }

    void paintDrawnHead(juce::Graphics& g, juce::Rectangle<float> area)
    {
        const float sx = area.getWidth() / panelW, sy = area.getHeight() / panelH;
        auto at = [&](float x, float y, float w, float h) { return juce::Rectangle<float>(x * sx, y * sy, w * sx, h * sy) + area.getPosition(); };
        g.setColour(col::tolex);
        g.fillRoundedRectangle(area, 12 * sx);
        g.setColour(col::accent.withAlpha(0.6f));
        g.drawRoundedRectangle(at(22, 26, 990, 334), 4 * sx, 1.2f * sx);
        g.drawHorizontalLine((int)at(0, 155, 0, 0).getY(), at(22, 0, 0, 0).getX(), at(1012, 0, 0, 0).getX());
        g.setFont(juce::FontOptions(46.0f * sy, juce::Font::bold | juce::Font::italic));
        g.setColour(col::plate);
        g.drawText("MJR", at(367, 90, 300, 60), juce::Justification::centred);

        const auto plate = at(222, 282, 590, 78);
        g.setGradientFill(juce::ColourGradient(col::plate, plate.getX(), plate.getY(), col::plate.darker(0.3f), plate.getX(), plate.getBottom(), false));
        g.fillRect(plate);
        g.setColour(juce::Colour(0xff3a3222));
        g.setFont(juce::FontOptions(8.5f * sy));
        g.drawText("INPUTS", at(735, 316, 60, 10), juce::Justification::centred);
        for (float x : { 735.0f, 790.0f })
            for (float y : { 303.0f, 338.0f })
            {
                g.setColour(juce::Colour(0xff222222));
                g.fillEllipse(at(x - 9, y - 9, 18, 18));
                g.setColour(juce::Colour(0xff999999));
                g.drawEllipse(at(x - 9, y - 9, 18, 18), 1.5f * sx);
            }
    }

    juce::Image photo;
    juce::OwnedArray<Knob> knobs;
    juce::Rectangle<float> band;
};

// One IR slot: file picker with arrows through its folder, level, pan, active.
class IrBlock : public juce::Component
{
public:
    IrBlock(MJRAmpProcessor& proc, int s)
        : p(proc), slot(s), id("ir" + juce::String(s + 1)), level(p.params, id + "Level", "LEVEL"), pan(p.params, id + "Pan", "PAN"),
          activeAtt(p.params, id + "On", active)
    {
        title.setText("IR " + juce::String(s + 1), juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, col::accent);
        title.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        active.setButtonText("ACTIVE");
        prev.setButtonText("<");
        next.setButtonText(">");
        prev.onClick = [this] { step(-1); };
        next.onClick = [this] { step(1); };
        files.onChange = [this] { picked(); };
        files.setTooltip("Impulse response for this slot");
        for (juce::Component* c : { (juce::Component*)&title, (juce::Component*)&prev, (juce::Component*)&next, (juce::Component*)&files,
                                    (juce::Component*)&level, (juce::Component*)&pan, (juce::Component*)&active })
            addAndMakeVisible(c);
        refresh();
    }

    void refresh()
    {
        const auto cur = p.getIrFile(slot);
        const auto dir = cur.existsAsFile() ? cur.getParentDirectory() : p.getLibraryDir();
        list = dir.findChildFiles(juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac");
        list.sort();
        files.clear(juce::dontSendNotification);
        for (int i = 0; i < list.size(); ++i)
            files.addItem(list[i].getFileNameWithoutExtension(), i + 1);
        files.addSeparator();
        files.addItem("Browse...", browseId);
        files.addItem("Empty", emptyId);
        const int idx = list.indexOf(cur);
        if (idx >= 0)
            files.setSelectedId(idx + 1, juce::dontSendNotification);
        else
            files.setText(cur.existsAsFile() ? cur.getFileNameWithoutExtension() : "Empty", juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        title.setBounds(r.removeFromTop(20));
        auto row = r.removeFromTop(28);
        prev.setBounds(row.removeFromLeft(24));
        next.setBounds(row.removeFromRight(24));
        files.setBounds(row.reduced(4, 0));
        r.removeFromTop(10);
        auto knobs = r.removeFromTop(108);
        level.setBounds(knobs.removeFromLeft(knobs.getWidth() / 2).reduced(6, 0));
        pan.setBounds(knobs.reduced(6, 0));
        r.removeFromTop(8);
        active.setBounds(r.removeFromTop(24).withSizeKeepingCentre(90, 24));
    }

private:
    static constexpr int browseId = 1000, emptyId = 1001;

    void step(int d)
    {
        if (list.isEmpty())
            return;
        const int idx = list.indexOf(p.getIrFile(slot));
        p.setIrFile(slot, list[(idx < 0 ? 0 : idx + d + list.size()) % list.size()]);
        refresh();
    }

    void picked()
    {
        const int id = files.getSelectedId();
        if (id == emptyId)
            p.setIrFile(slot, {}), refresh();
        else if (id == browseId)
        {
            chooser = std::make_unique<juce::FileChooser>("Impulse response", p.getIrFile(slot).existsAsFile() ? p.getIrFile(slot) : p.getLibraryDir(),
                                                          "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [this](const juce::FileChooser& fc)
                                 {
                                     if (fc.getResult().existsAsFile())
                                         p.setIrFile(slot, fc.getResult());
                                     refresh();
                                 });
        }
        else if (id > 0 && id <= list.size())
            p.setIrFile(slot, list[id - 1]);
    }

    MJRAmpProcessor& p;
    int slot;
    juce::String id;
    juce::Label title;
    juce::TextButton prev, next;
    juce::ComboBox files;
    LabeledKnob level, pan;
    juce::ToggleButton active;
    juce::AudioProcessorValueTreeState::ButtonAttachment activeAtt;
    juce::Array<juce::File> list;
    std::unique_ptr<juce::FileChooser> chooser;
};

class TunerView : public juce::Component, private juce::Timer
{
public:
    TunerView(MJRAmpProcessor& proc) : p(proc) { buf.resize(4096); }

    void visibilityChanged() override
    {
        p.tunerActive = isVisible();
        isVisible() ? startTimerHz(20) : stopTimer();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(col::bg);
        auto r = getLocalBounds().reduced(40).withSizeKeepingCentre(std::min(getWidth() - 80, 640), 300);
        const bool held = juce::Time::getMillisecondCounter() - lastSeen < 600;

        g.setColour(held ? (std::abs(cents) < 3 ? col::accent : col::text) : col::line);
        g.setFont(juce::FontOptions(110.0f, juce::Font::bold));
        g.drawText(held ? note : "-", r.removeFromTop(130), juce::Justification::centred);
        g.setFont(juce::FontOptions(16.0f));
        g.setColour(col::dim);
        g.drawText(held ? juce::String(hz, 1) + " Hz" : "Play a string", r.removeFromTop(30), juce::Justification::centred);

        // Cents scale, -50..+50, needle on the detected deviation.
        auto scale = r.removeFromTop(90).reduced(0, 20).toFloat();
        for (int c = -50; c <= 50; c += 10)
        {
            const float x = scale.getX() + (float)(c + 50) / 100.0f * scale.getWidth();
            g.setColour(c == 0 ? col::accent : col::line);
            g.fillRect(juce::Rectangle<float>(c == 0 ? 3.0f : 2.0f, c == 0 ? scale.getHeight() : scale.getHeight() * 0.5f)
                           .withCentre({ x, scale.getCentreY() }));
        }
        if (held)
        {
            const float x = scale.getX() + (juce::jlimit(-50.0f, 50.0f, cents) + 50.0f) / 100.0f * scale.getWidth();
            g.setColour(std::abs(cents) < 3 ? col::accent : col::text);
            g.fillRoundedRectangle(juce::Rectangle<float>(6, scale.getHeight() + 16).withCentre({ x, scale.getCentreY() }), 3);
        }
        g.setColour(col::dim);
        g.setFont(juce::FontOptions(13.0f));
        g.drawText("A4 = 440 Hz  -  output muted while tuning", r.removeFromTop(24), juce::Justification::centred);
    }

private:
    void timerCallback() override
    {
        const int n = p.copyTunerInput(buf.data(), (int)buf.size());
        const double f = detectPitch(buf.data(), n, p.getSampleRate());
        if (f > 20 && f < 2000)
        {
            const double midi = 69 + 12 * std::log2(f / 440.0);
            const int nearest = (int)std::lround(midi);
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const juce::String newNote = juce::String(names[nearest % 12]) + juce::String(nearest / 12 - 1);
            const float newCents = (float)((midi - nearest) * 100.0);
            cents = newNote == note ? cents + 0.4f * (newCents - cents) : newCents; // smooth the needle on a held note
            note = newNote;
            hz = f;
            lastSeen = juce::Time::getMillisecondCounter();
        }
        repaint();
    }

    MJRAmpProcessor& p;
    std::vector<float> buf;
    juce::String note;
    float cents = 0;
    double hz = 0;
    juce::uint32 lastSeen = 0;
};

class MetroPanel : public juce::Component
{
public:
    MetroPanel(MJRAmpProcessor& proc)
        : p(proc), onAtt(p.params, "metroOn", on), bpmAtt(p.params, "bpm", bpm), beatsAtt(p.params, "beats", beats),
          level(p.params, "metroLevel", false)
    {
        on.setButtonText("Metronome");
        bpm.setSliderStyle(juce::Slider::LinearHorizontal);
        bpm.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 24);
        bpm.setTextValueSuffix(" BPM");
        beats.setSliderStyle(juce::Slider::IncDecButtons);
        beats.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 24);
        tap.setButtonText("Tap");
        tap.onClick = [this] { tapTempo(); };
        for (auto* l : { &beatsLabel, &levelLabel })
            l->setColour(juce::Label::textColourId, col::dim);
        beatsLabel.setText("Beats per bar", juce::dontSendNotification);
        levelLabel.setText("Level", juce::dontSendNotification);
        for (juce::Component* c : { (juce::Component*)&on, (juce::Component*)&bpm, (juce::Component*)&tap, (juce::Component*)&beats,
                                    (juce::Component*)&level, (juce::Component*)&beatsLabel, (juce::Component*)&levelLabel })
            addAndMakeVisible(c);
        setSize(320, 170);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(12);
        on.setBounds(r.removeFromTop(26));
        r.removeFromTop(8);
        auto row = r.removeFromTop(28);
        tap.setBounds(row.removeFromRight(56));
        bpm.setBounds(row.withTrimmedRight(8));
        r.removeFromTop(10);
        row = r.removeFromTop(28);
        beatsLabel.setBounds(row.removeFromLeft(110));
        beats.setBounds(row.removeFromLeft(100));
        r.removeFromTop(6);
        row = r.removeFromTop(40);
        levelLabel.setBounds(row.removeFromLeft(110));
        level.setBounds(row.removeFromLeft(40));
    }

private:
    void tapTempo()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (!taps.empty() && now - taps.back() > 2000)
            taps.clear();
        taps.push_back(now);
        if (taps.size() > 5)
            taps.erase(taps.begin());
        if (taps.size() >= 2)
            bpm.setValue(60000.0 * (double)(taps.size() - 1) / (taps.back() - taps.front()), juce::sendNotificationSync);
    }

    MJRAmpProcessor& p;
    juce::ToggleButton on;
    juce::Slider bpm, beats;
    juce::TextButton tap;
    juce::Label beatsLabel, levelLabel;
    juce::AudioProcessorValueTreeState::ButtonAttachment onAtt;
    juce::AudioProcessorValueTreeState::SliderAttachment bpmAtt, beatsAtt;
    Knob level;
    std::vector<double> taps;
};

class Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int baseW = 1280, baseH = 900; // laid out at this size, scaled to the window

    Editor(MJRAmpProcessor& proc)
        : AudioProcessorEditor(proc), p(proc), rig(proc), tuner(proc), input(proc.params, "input", "INPUT"), gate(proc.params, "gate", "GATE"),
          output(proc.params, "output", "OUTPUT")
    {
        setLookAndFeel(&look);
        juce::LookAndFeel::setDefaultLookAndFeel(&look); // popups and choosers
        addAndMakeVisible(content);

        title.setText("MJR AMP", juce::dontSendNotification);
        title.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, col::accent);

        // Captures sorted by channel, then gain level, grouped under headings.
        const juce::StringArray levels { "Clean", "Edge", "Crunch", "Dimed", "Drive", "Lead" };
        for (int i = 0; i < (int)p.captures.size(); ++i)
            order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b)
        {
            const auto &x = p.captures[(size_t)a], &y = p.captures[(size_t)b];
            if (x.channel != y.channel)
                return x.channel == "I";
            return levels.indexOf(x.level) < levels.indexOf(y.level);
        });
        juce::String group;
        for (int i : order)
        {
            const auto& c = p.captures[(size_t)i];
            const auto g = channelName(c) + "  -  " + c.level;
            if (g != group)
                capture.addSectionHeading(group = g);
            capture.addItem(fullName(c), i + 1);
        }
        capture.onChange = [this] { if (capture.getSelectedId() > 0) selectCapture(capture.getSelectedId() - 1); };
        capture.setTooltip("Amp capture: channel, gain level and voicing");
        prevCapture.setButtonText("<");
        nextCapture.setButtonText(">");
        prevCapture.onClick = [this] { stepCapture(-1); };
        nextCapture.onClick = [this] { stepCapture(1); };

        for (auto* l : { &captureLabel, &captured })
        {
            l->setJustificationType(juce::Justification::centred);
            l->setColour(juce::Label::textColourId, col::dim);
        }
        captureLabel.setText("CAPTURE", juce::dontSendNotification);
        captureLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        captureLabel.setColour(juce::Label::textColourId, col::text);

        resetKnobs.setButtonText("Reset knobs");
        resetKnobs.setTooltip("Set the amp knobs back to the capture's recorded settings");
        resetKnobs.onClick = [this] { p.resetKnobsToCapture(); };

        tunerButton.setClickingTogglesState(true);
        tunerButton.setTooltip("Tuner (mutes the output)");
        tunerButton.onClick = [this] { tuner.setVisible(tunerButton.getToggleState()); };
        metroButton.setTooltip("Metronome");
        metroButton.onClick = [this]
        {
            juce::CallOutBox::launchAsynchronously(std::make_unique<MetroPanel>(p), content.getLocalArea(&metroButton, metroButton.getLocalBounds()), &content);
        };
        libraryButton.setTooltip("Folder with the .nam captures and IRs");
        // Standalone app: a guitar amp needs its input, so don't keep JUCE's anti-feedback mute,
        // and give direct access to the audio device settings.
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
        {
            holder->getMuteInputValue().setValue(false);
            audioButton.setTooltip("Audio interface, inputs and outputs");
            audioButton.onClick = [holder] { holder->showAudioSettingsDialog(); };
            content.addAndMakeVisible(audioButton);
        }
        libraryButton.onClick = [this] { chooseLibrary(); };

        for (int s = 0; s < MJRAmpProcessor::numIrSlots; ++s)
            irs.add(new IrBlock(p, s));

        for (juce::Component* c : { (juce::Component*)&title, (juce::Component*)&input, (juce::Component*)&gate, (juce::Component*)&inputSwitch, (juce::Component*)&output, (juce::Component*)&inMeter,
                                    (juce::Component*)&outMeter, (juce::Component*)&tunerButton, (juce::Component*)&metroButton,
                                    (juce::Component*)&libraryButton, (juce::Component*)&prevCapture, (juce::Component*)&capture,
                                    (juce::Component*)&nextCapture, (juce::Component*)&captureLabel, (juce::Component*)&captured,
                                    (juce::Component*)&resetKnobs, (juce::Component*)&rig })
            content.addAndMakeVisible(c);
        for (auto* b : irs)
            content.addAndMakeVisible(b);
        content.addChildComponent(tuner);
        layout();
        refreshCapture();

        setResizable(true, true);
        setResizeLimits(baseW * 6 / 10, baseH * 6 / 10, baseW * 3 / 2, baseH * 3 / 2);
        getConstrainer()->setFixedAspectRatio((double)baseW / baseH);
        setSize(baseW * 9 / 10, baseH * 9 / 10);
        startTimerHz(30);
    }

    ~Editor() override
    {
        p.tunerActive = false; // never leave the plugin muted when the window closes
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override { g.fillAll(col::bg); }

    void resized() override
    {
        content.setBounds(0, 0, baseW, baseH);
        content.setTransform(juce::AffineTransform::scale((float)getWidth() / baseW));
    }

private:
    void layout()
    {
        title.setBounds(20, 8, 120, 20);
        const int iconsX = audioButton.isVisible() ? 500 : 535;
        tunerButton.setBounds(iconsX, 8, 70, 58);
        metroButton.setBounds(iconsX + 70, 8, 70, 58);
        libraryButton.setBounds(iconsX + 140, 8, 70, 58);
        audioButton.setBounds(iconsX + 210, 8, 70, 58);

        inMeter.setBounds(34, 60, 6, 140);
        input.setBounds(60, 56, 130, 150);
        gate.setBounds(196, 86, 92, 120);
        inputSwitch.setBounds(296, 120, 90, 50);
        output.setBounds(baseW - 190, 56, 130, 150);
        outMeter.setBounds(baseW - 40, 60, 6, 140);

        prevCapture.setBounds(400, 88, 32, 34);
        capture.setBounds(438, 88, 404, 34);
        nextCapture.setBounds(848, 88, 32, 34);
        captureLabel.setBounds(438, 126, 404, 18);
        captured.setBounds(380, 146, 520, 18);
        resetKnobs.setBounds(585, 170, 110, 26);

        for (int s = 0; s < irs.size(); ++s)
            irs[s]->setBounds(s < 2 ? 30 : baseW - 230, 250 + (s % 2) * 300, 200, 230);

        rig.setBounds(240, 236, 800, baseH - 236);
        tuner.setBounds(rig.getBounds());
    }

    static juce::String channelName(const Capture& c) { return c.channel == "I" ? "Channel I" : "Channels I + II"; }
    static juce::String fullName(const Capture& c)
    {
        return (c.channel == "I" ? "I" : "I+II") + juce::String("   ") + c.level + "   " + c.voicing + (c.pedal.isEmpty() ? "" : "   + " + c.pedal);
    }
    int captureIndex() const { return ((juce::AudioParameterChoice*)p.params.getParameter("capture"))->getIndex(); }

    void selectCapture(int idx)
    {
        auto* prm = p.params.getParameter("capture");
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1((float)idx));
        prm->endChangeGesture();
    }

    void stepCapture(int d)
    {
        const auto it = std::find(order.begin(), order.end(), captureIndex());
        const int pos = (int)(it - order.begin());
        selectCapture(order[(size_t)((pos + d + (int)order.size()) % (int)order.size())]);
    }

    void refreshCapture()
    {
        const auto& c = p.captures[(size_t)captureIndex()];
        capture.setSelectedId(captureIndex() + 1, juce::dontSendNotification);
        juce::String info;
        if (c.hasKnobs)
        {
            const char* k[] = { "Presence", "Bass", "Middle", "Treble", "Vol I", "Vol II" };
            for (int i = 0; i < 6; ++i)
                info << k[i] << " " << juce::String(c.knobs[(size_t)i], 1).trimCharactersAtEnd("0").trimCharactersAtEnd(".") << "    ";
            info = "Captured at:  " + info.trim();
        }
        else
            info = "Captured settings unknown: knobs act relative to where they were";
        captured.setText(info, juce::dontSendNotification);
        resetKnobs.setEnabled(c.hasKnobs);
        shownCapture = captureIndex();
    }

    void chooseLibrary()
    {
        chooser = std::make_unique<juce::FileChooser>("Capture library", p.getLibraryDir());
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                             [this](const juce::FileChooser& fc)
                             {
                                 if (fc.getResult().isDirectory())
                                 {
                                     p.setLibraryDir(fc.getResult());
                                     for (auto* b : irs)
                                         b->refresh();
                                 }
                             });
    }

    void timerCallback() override
    {
        if (captureIndex() != shownCapture)
            refreshCapture();
        const bool missing = !p.getLibraryDir().getChildFile(p.captures[(size_t)captureIndex()].file).existsAsFile();
        rig.missing.setText("Capture not found.\nChoose the library folder (LIBRARY).", juce::dontSendNotification);
        rig.missing.setVisible(missing);

        auto update = [](Meter& m, std::atomic<float>& peak)
        {
            const float db = juce::Decibels::gainToDecibels(peak.exchange(0.0f), -60.0f);
            m.level = std::max(db, m.level - 1.5f);
            m.repaint();
        };
        update(inMeter, p.inputPeak);
        update(outMeter, p.outputPeak);

        if (const int beat = p.beatCount.load(); beat != shownBeat)
        {
            shownBeat = beat;
            beatLit = juce::Time::getMillisecondCounter();
            metroButton.downbeat = p.beatInBar.load() == 0;
        }
        const bool lit = p.params.getRawParameterValue("metroOn")->load() > 0.5f && juce::Time::getMillisecondCounter() - beatLit < 110;
        if (lit != metroButton.lit)
            metroButton.lit = lit, metroButton.repaint();
    }

    MJRAmpProcessor& p;
    Look look;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Component content;
    juce::Label title, captureLabel, captured;
    std::vector<int> order;
    juce::ComboBox capture;
    juce::TextButton prevCapture, nextCapture, resetKnobs;
    IconButton tunerButton { "TUNER", IconButton::tuner }, metroButton { "METRO", IconButton::metronome },
        libraryButton { "LIBRARY", IconButton::folder }, audioButton { "AUDIO", IconButton::audio };
    ChannelSwitch inputSwitch;
    juce::AudioProcessorValueTreeState::ButtonAttachment inputSwitchAtt { p.params, "inputSource", inputSwitch };
    Rig rig;
    TunerView tuner;
    juce::OwnedArray<IrBlock> irs;
    LabeledKnob input, gate, output;
    Meter inMeter, outMeter;
    std::unique_ptr<juce::FileChooser> chooser;
    int shownCapture = -1, shownBeat = 0;
    juce::uint32 beatLit = 0;
};
} // namespace

juce::AudioProcessorEditor* createMJREditor(MJRAmpProcessor& p) { return new Editor(p); }
