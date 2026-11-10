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

    // Graphic EQ faders ("fader" property): dark slot, lit cap that glows while the EQ is on ("lit").
    int getSliderThumbRadius(juce::Slider& s) override { return s.getProperties().contains("fader") ? 13 : LookAndFeel_V4::getSliderThumbRadius(s); }
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (!s.getProperties().contains("fader"))
            return LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, minPos, maxPos, style, s);
        const auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h);
        const float cx = r.getCentreX();
        const auto slot = juce::Rectangle<float>(9, r.getHeight() + 20).withCentre({ cx, r.getCentreY() });
        g.setColour(juce::Colour(0xff0b0c0e));
        g.fillRoundedRectangle(slot, 4.5f);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawRoundedRectangle(slot, 4.5f, 1);
        const bool lit = s.getProperties()["lit"];
        const auto cap = juce::Rectangle<float>(15, 24).withCentre({ cx, pos });
        if (lit)
        {
            g.setGradientFill(juce::ColourGradient(juce::Colour(0x996fb6ff), cx, pos, juce::Colour(0x006fb6ff), cx + 22, pos, true));
            g.fillEllipse(cap.expanded(14, 10));
        }
        g.setColour(lit ? juce::Colour(0xffeef6ff) : juce::Colour(0xff6c7178));
        g.fillRoundedRectangle(cap, 3);
        g.setColour(lit ? juce::Colour(0xff9fcfff) : juce::Colour(0xff3c4046));
        g.drawRoundedRectangle(cap, 3, 1.2f);
    }

    // Amp knobs ("amp" property): chrome cap on a knurled brass ring, like the panel's.
    // Other rotaries: a thin arc.
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
    {
        const auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(2);
        const auto c = r.getCentre();
        const float rad = std::min(r.getWidth(), r.getHeight()) * 0.5f;
        const float a = start + pos * (end - start);

        if (s.getProperties().contains("pedal")) // stompbox knob: plain cap with a pointer line, min/max dots on the face
        {
            const bool dark = s.getProperties()["pedal"];
            const float cap = rad * 0.82f;
            g.setColour(s.findColour(juce::Slider::rotarySliderOutlineColourId));
            for (float t : { start, end })
                g.fillEllipse(juce::Rectangle<float>(rad * 0.13f, rad * 0.13f).withCentre(c.getPointOnCircumference(rad * 0.93f, t)));
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillEllipse(juce::Rectangle<float>(cap * 2.1f, cap * 2.1f).withCentre(c.translated(0, cap * 0.2f)));
            const auto skirt = juce::Rectangle<float>(cap * 2, cap * 2).withCentre(c);
            g.setGradientFill(dark ? juce::ColourGradient(juce::Colour(0xff3a3a3c), c.x, skirt.getY(), juce::Colour(0xff060606), c.x, skirt.getBottom(), false)
                                   : juce::ColourGradient(juce::Colour(0xffffffff), c.x, skirt.getY(), juce::Colour(0xffb9bcc0), c.x, skirt.getBottom(), false));
            g.fillEllipse(skirt);
            if (dark) // knurled edge
            {
                g.setColour(juce::Colour(0xff4a4a4c));
                for (int i = 0; i < 28; ++i)
                {
                    const float t = (float)i * juce::MathConstants<float>::twoPi / 28.0f;
                    g.drawLine(juce::Line<float>(c.getPointOnCircumference(cap * 0.86f, t), c.getPointOnCircumference(cap * 0.99f, t)), 0.8f);
                }
            }
            const auto top = skirt.reduced(cap * 0.14f);
            g.setGradientFill(dark ? juce::ColourGradient(juce::Colour(0xff121213), c.x, top.getY(), juce::Colour(0xff2c2c2e), c.x, top.getBottom(), false)
                                   : juce::ColourGradient(juce::Colour(0xffe4e6e8), c.x, top.getY(), juce::Colour(0xfffbfbfb), c.x, top.getBottom(), false));
            g.fillEllipse(top);
            g.setColour(dark ? juce::Colours::white : juce::Colour(0xff26282c));
            g.drawLine(juce::Line<float>(c.getPointOnCircumference(cap * 0.3f, a), c.getPointOnCircumference(cap * 0.8f, a)), juce::jmax(2.0f, rad * 0.09f));
            return;
        }

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
    enum Icon { tuner, metronome, folder, audio, pedal, amp, eq };
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
            case pedal: // stompbox
                p.addRoundedRectangle(b.getX() + 6, b.getY() + 1, 18, 28, 3);
                p.addEllipse(b.getCentreX() - 4, b.getBottom() - 11, 8, 8);
                p.addEllipse(b.getX() + 10, b.getY() + 5, 4, 4);
                p.addEllipse(b.getRight() - 14, b.getY() + 5, 4, 4);
                break;
            case amp: // head on a cab
                p.addRoundedRectangle(b.getX() + 1, b.getY() + 2, 28, 9, 2);
                p.addRoundedRectangle(b.getX() + 1, b.getY() + 13, 28, 16, 2);
                p.addEllipse(b.getX() + 5, b.getY() + 16, 9, 9);
                p.addEllipse(b.getRight() - 14, b.getY() + 16, 9, 9);
                break;
            case eq: // sliders
                for (int k = 0; k < 3; ++k)
                {
                    const float x = b.getX() + 6 + 9.0f * (float)k, y = b.getY() + (k == 1 ? 18.0f : k == 0 ? 8.0f : 13.0f);
                    p.startNewSubPath(x, b.getY() + 1);
                    p.lineTo(x, b.getBottom() - 1);
                    p.addRectangle(x - 3, y, 6, 4);
                }
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

// Effect pedal drawn as a stompbox: knobs, name, LED, footswitch. Click toggles the module on/off.
struct Pedal : juce::ToggleButton
{
    Pedal(MJRAmpProcessor& p, int fxIndex, juce::Colour c)
        : juce::ToggleButton(fxChain[fxIndex].name), proc(p), colour(c), index(fxIndex), nam(p.namPedalFor(fxIndex)),
          attachment(p.params, juce::String(fxChain[fxIndex].id) + "On", *this)
    {
        auto& params = p.params;
        const auto& fx = fxChain[fxIndex];
        setTooltip(fx.name);
        if (nam != nullptr) // captures folder of the real pedal
        {
            folder.setButtonText("...");
            folder.setTooltip("Folder with the pedal's NAM captures");
            folder.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser>("Pedal captures folder", nam->getFolder());
                chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc)
                {
                    if (fc.getResult() != juce::File())
                        proc.setPedalFolder(index, fc.getResult());
                });
            };
            addAndMakeVisible(folder);
        }
        for (int k = 0; k < fx.numKnobs(); ++k)
        {
            auto* knob = knobs.add(new LabeledKnob(params, juce::String(fx.id) + "K" + juce::String(k), fx.knobs[(size_t)k].name));
            knob->knob.getProperties().set("pedal", false); // white caps on every pedal, like the compressor's
            knob->knob.setColour(juce::Slider::rotarySliderOutlineColourId, ink().withAlpha(0.7f));
            knob->label.setText(knob->label.getText().toLowerCase(), juce::dontSendNotification);
            knob->label.setColour(juce::Label::textColourId, ink());
            knob->label.setFont(juce::FontOptions(11.5f, juce::Font::bold));
            knob->value.setColour(juce::Label::textColourId, ink().withAlpha(0.6f));
            knob->value.setFont(juce::FontOptions(10.5f));
            addAndMakeVisible(knob);
        }
    }
    juce::String getTooltip() override
    {
        if (nam == nullptr)
            return juce::ToggleButton::getTooltip();
        const auto shot = nam->currentName();
        return getName() + (shot.isEmpty() ? ": no NAM captures found, pick their folder with ..." : ", nearest capture: " + shot);
    }
    bool light() const { return colour.getPerceivedBrightness() > 0.55f; }
    juce::Colour ink() const { return light() ? juce::Colour(0xff2a2e35) : juce::Colours::white.withAlpha(0.92f); }
    juce::Rectangle<float> face() const { return getLocalBounds().toFloat().reduced(10, 4).withTrimmedBottom(8).reduced(4); }

    void resized() override
    {
        folder.setBounds(getWidth() - 46, 16, 28, 18);
        auto r = face().toNearestInt().reduced(8).withTrimmedTop(22);
        const int n = knobs.size(), cols = n > 4 ? 3 : juce::jmin(n, 2), rowH = n > 2 ? 104 : 124;
        for (int k = 0; k < n; ++k)
        {
            const int row = k / cols, inRow = juce::jmin(cols, n - row * cols), cw = r.getWidth() / juce::jmax(cols, 1);
            const int x = r.getX() + (r.getWidth() - inRow * cw) / 2 + (k % cols) * cw;
            knobs[k]->setBounds(x, r.getY() + row * rowH, cw, rowH);
        }
    }
    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const auto box = getLocalBounds().toFloat().reduced(10, 4);
        g.setColour(juce::Colour(0xff1c1c1e)); // jacks on the sides
        for (float x : { box.getX() - 6, box.getRight() - 2 })
            g.fillRoundedRectangle(x, box.getY() + 50, 8, 30, 2);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRoundedRectangle(box.translated(0, 6), 16);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd8dadc), 0, box.getY(), juce::Colour(0xff6c6e72), 0, box.getBottom(), false));
        g.fillRoundedRectangle(box, 16); // bent-metal rim, deeper at the bottom
        const auto f = face();
        g.setGradientFill(juce::ColourGradient(colour.brighter(0.08f), 0, f.getY(), colour.darker(0.12f), 0, f.getBottom(), false));
        g.fillRoundedRectangle(f, 13);
        g.setColour(juce::Colours::white.withAlpha(over ? 0.45f : 0.18f));
        g.drawRoundedRectangle(f, 13, over ? 1.6f : 1.0f);

        g.setColour(ink());
        g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
        const auto nameArea = f.withTrimmedTop(f.getHeight() - 158).removeFromTop(36).reduced(8, 0);
        g.drawFittedText(getName().toLowerCase(), nameArea.toNearestInt(), juce::Justification::centred, 1, 0.6f);

        const bool on = getToggleState();
        const auto led = juce::Rectangle<float>(9, 9).withCentre({ f.getCentreX(), f.getBottom() - 92 });
        if (on)
        {
            g.setGradientFill(juce::ColourGradient(juce::Colour(0x996fb6ff), led.getCentreX(), led.getCentreY(), juce::Colour(0x006fb6ff), led.getCentreX(), led.getY() - 9, true));
            g.fillEllipse(led.expanded(9));
        }
        g.setColour(on ? juce::Colour(0xffd6ecff) : juce::Colour(0xff3a3f46));
        g.fillEllipse(led);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawEllipse(led, 1);

        const auto c = juce::Point<float>(f.getCentreX(), f.getBottom() - 46);
        juce::Path nut; // hex nut under the stomp switch
        nut.addPolygon(c, 6, 25, juce::MathConstants<float>::pi / 6);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff2f2f2), c.x - 20, c.y - 20, juce::Colour(0xff6a6c70), c.x + 20, c.y + 22, false));
        g.fillPath(nut);
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.strokePath(nut, juce::PathStrokeType(1));
        const auto sw = juce::Rectangle<float>(30, 30).withCentre(c);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff8e9196), sw.getX(), sw.getY(), juce::Colour(0xfff6f6f6), sw.getRight(), sw.getBottom(), false));
        g.fillEllipse(sw);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xfffafafa), sw.getX(), sw.getY(), juce::Colour(0xff9a9da2), sw.getRight(), sw.getBottom(), false));
        g.fillEllipse(sw.reduced(4));
    }
    MJRAmpProcessor& proc;
    juce::Colour colour;
    int index;
    NamPedal* nam;
    juce::TextButton folder;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::OwnedArray<LabeledKnob> knobs;
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
};

// Bat toggle with its LED, for the graphic EQ's on/off.
struct EqSwitch : juce::ToggleButton
{
    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        const bool on = getToggleState();
        auto sw = r.removeFromLeft(r.getWidth() * 0.5f);
        g.setColour(juce::Colour(0xffb8bcc2).withAlpha(over ? 1.0f : 0.8f));
        g.setFont(juce::FontOptions(10.5f));
        g.drawText("on", sw.removeFromTop(16), juce::Justification::centred);
        g.drawText("off", sw.removeFromBottom(16), juce::Justification::centred);
        const auto c = sw.getCentre();
        const auto nut = juce::Rectangle<float>(22, 22).withCentre(c);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd9dbde), nut.getX(), nut.getY(), juce::Colour(0xff4a4d52), nut.getRight(), nut.getBottom(), false));
        g.fillEllipse(nut);
        const auto tip = c.translated(0, on ? -14.0f : 14.0f);
        g.setColour(juce::Colour(0xffc9ccd0));
        g.drawLine(juce::Line<float>(c, tip), 5);
        g.fillEllipse(juce::Rectangle<float>(8, 8).withCentre(tip));
        const auto led = juce::Rectangle<float>(10, 10).withCentre(r.getCentre());
        if (on)
        {
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xccdff0ff), led.getCentreX(), led.getCentreY(), juce::Colour(0x00dff0ff), led.getCentreX(), led.getY() - 14, true));
            g.fillEllipse(led.expanded(14));
        }
        g.setColour(on ? juce::Colour(0xfff4f9ff) : juce::Colour(0xff34383e));
        g.fillEllipse(led);
    }
};

// The graphic EQ as a rack unit: a lit fader per band, its gain above and frequency below. Double-click a fader for 0 dB.
class GraphicEq : public juce::Component
{
public:
    GraphicEq(MJRAmpProcessor& p, const FxInfo& fx) : onAtt(p.params, juce::String(fx.id) + "On", power)
    {
        for (int k = 0; k < fx.numKnobs(); ++k)
        {
            auto* f = faders.add(new juce::Slider(juce::Slider::LinearVertical, juce::Slider::NoTextBox));
            f->getProperties().set("fader", true);
            atts.add(new juce::AudioProcessorValueTreeState::SliderAttachment(p.params, juce::String(fx.id) + "K" + juce::String(k), *f));
            f->setDoubleClickReturnValue(true, 0.0);
            f->setTooltip(juce::String(fx.knobs[(size_t)k].name) + " (double-click: 0 dB)");
            f->onValueChange = [this] { repaint(); };
            names.add(fx.knobs[(size_t)k].name);
            addAndMakeVisible(f);
        }
        power.setTooltip("Graphic EQ on/off");
        power.onStateChange = [this] { light(); };
        addAndMakeVisible(power);
        light();
    }

    void paint(juce::Graphics& g) override
    {
        const auto body = getLocalBounds().toFloat().withTrimmedBottom(14);
        g.setColour(juce::Colour(0xff0a0a0b)); // feet
        for (float fx : { body.getX() + 60, body.getRight() - 140 })
            g.fillRoundedRectangle(fx, body.getBottom() - 6, 80, 20, 5);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRoundedRectangle(body.translated(0, 5), 14);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3b3e43), 0, body.getY(), juce::Colour(0xff25272b), 0, body.getBottom(), false));
        g.fillRoundedRectangle(body, 14);
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawRoundedRectangle(body.reduced(0.5f), 14, 1);

        g.setColour(juce::Colour(0xffaeb2b8));
        g.setFont(juce::FontOptions(30.0f));
        g.drawText("graphic eq", body.withWidth(faderLeft - 20).withTrimmedLeft(36), juce::Justification::centredLeft);
        for (int k = 0; k < faders.size(); ++k)
        {
            const auto b = faders[k]->getBounds().toFloat();
            g.setColour(juce::Colours::white.withAlpha(0.92f));
            g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
            g.drawText(juce::String(faders[k]->getValue(), 1) + " dB", b.withY(b.getY() - 34).withHeight(16).expanded(12, 0), juce::Justification::centred);
            g.setColour(juce::Colour(0xff9a9fa6));
            g.setFont(juce::FontOptions(10.5f));
            g.drawText(names[k], b.withY(b.getBottom() + 18).withHeight(16).expanded(12, 0), juce::Justification::centred);
        }
    }

    void resized() override
    {
        const auto body = getLocalBounds().withTrimmedBottom(14);
        const int right = body.getRight() - 140, w = (right - faderLeft) / juce::jmax(1, faders.size());
        for (int k = 0; k < faders.size(); ++k)
            faders[k]->setBounds(faderLeft + k * w + (w - 36) / 2, body.getY() + 64, 36, body.getHeight() - 128);
        power.setBounds(right + 20, body.getCentreY() - 34, 92, 68);
    }

private:
    void light()
    {
        for (auto* f : faders)
            f->getProperties().set("lit", power.getToggleState()), f->repaint();
    }

    static constexpr int faderLeft = 240;
    juce::OwnedArray<juce::Slider> faders;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> atts;
    juce::StringArray names;
    EqSwitch power;
    juce::AudioProcessorValueTreeState::ButtonAttachment onAtt;
};

// A page of pedals for one stage of the chain, laid out left to right in signal order.
class FxPage : public juce::Component
{
public:
    FxPage(MJRAmpProcessor& p, FxStage stage, juce::String from, juce::String to)
        : source(std::move(from)), destination(std::move(to))
    {
        const juce::Colour palette[] = { juce::Colour(0xffaab6c4), juce::Colour(0xff1d1e21), juce::Colour(0xff4c5a73), juce::Colour(0xff2f6a64),
                                         juce::Colour(0xffd5dbde), juce::Colour(0xff9a8c78), juce::Colour(0xff7a4f5a), juce::Colour(0xff8c9a82),
                                         juce::Colour(0xff2c2d30), juce::Colour(0xff5e6f88) };
        for (int i = 0; i < numFx; ++i)
            if (fxChain[i].stage == stage)
            {
                if (std::string_view(fxChain[i].id) == "eq")
                    addAndMakeVisible(*(rack = std::make_unique<GraphicEq>(p, fxChain[i])));
                else
                    addAndMakeVisible(*pedals.add(new Pedal(p, i, palette[i % 10])));
            }
    }

    void paint(juce::Graphics& g) override
    {
        juce::Component* first = rack != nullptr ? (juce::Component*)rack.get() : pedals.isEmpty() ? nullptr : pedals[0];
        if (first == nullptr)
            return;
        const float y = (float)first->getBounds().getCentreY();
        g.setColour(col::line);
        g.drawLine(40, y, (float)getWidth() - 40, y, 3);
        g.setColour(col::dim);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(source, juce::Rectangle<float>(0, y - 40, 120, 20), juce::Justification::centred);
        g.drawText(destination, juce::Rectangle<float>((float)getWidth() - 120, y - 40, 120, 20), juce::Justification::centred);
    }

    void resized() override
    {
        const int n = pedals.size(), gap = 32, w = juce::jlimit(180, 260, (getWidth() - 140 - (n - 1) * gap) / juce::jmax(n, 1)), total = n * w + (n - 1) * gap;
        int x = (getWidth() - total) / 2;
        for (auto* p : pedals)
            p->setBounds(x, getHeight() / 2 - 220, w, 440), x += w + gap;
        if (rack != nullptr)
            rack->setBounds(juce::Rectangle<int>(juce::jmin(1000, getWidth() - 260), 300).withCentre(getLocalBounds().getCentre()));
    }

private:
    juce::OwnedArray<Pedal> pedals;
    std::unique_ptr<GraphicEq> rack;
    juce::String source, destination;
};

// The rig: amp head (photo or drawn) standing on a drawn cabinet. The cabinet's top band
// names each amp knob and shows its value, right under the knob.
class Rig : public juce::Component, public juce::TooltipClient
{
public:
    Rig(MJRAmpProcessor& p)
        : proc(p), ampAtt(*p.params.getParameter("ampOn"), [this](float) { repaint(); }),
          captureAtt(*p.params.getParameter("capture"), [this](float) { repaint(); }),
          modeAtt(*p.params.getParameter("ampMode"), [this](float) { repaint(); }),
          jackAtt(*p.params.getParameter("jack"), [this](float) { repaint(); }),
          modelAtt(*p.params.getParameter("circuit"), [this](float) { repaint(); })
    {
#if MJR_HAS_PHOTO
        photo = juce::ImageCache::getFromMemory(BinaryData::amp_png, BinaryData::amp_pngSize);
        tolex[0] = recolour(photo, juce::Colour(0xffe4e0d4), 0.35f); // amp A: white
        tolex[1] = recolour(photo, juce::Colour(0xff5c3822), 1.0f);  // amp B: brown
        tolex[2] = photo;                                           // amp C and the captures: black, as photographed
#endif
#if MJR_HAS_CAB
        cabPhoto = juce::ImageCache::getFromMemory(BinaryData::cab_png, BinaryData::cab_pngSize);
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
        const int look = proc.circuitMode() ? ((juce::AudioParameterChoice*)proc.params.getParameter("circuit"))->getIndex() : 2;
        if (headScaled[(size_t)look].isValid())
            g.drawImage(headScaled[(size_t)look], area);
        else
            paintDrawnHead(g, area);
        paintPanelControls(g);

        const char* names[] = { "PRESENCE", "BASS", "MIDDLE", "TREBLE", "VOL I", "VOL II" };
        const float pitch = knobCentre(1).x - knobCentre(0).x; // one column per knob, as wide as the knob spacing
        const float x0 = knobCentre(0).x - pitch / 2, x1 = knobCentre(5).x + pitch / 2; // dark plate: readable over tolex and grille
        g.setColour(juce::Colours::black.withAlpha(0.72f));
        g.fillRoundedRectangle(x0 - 4, band.getY() + 3, x1 - x0 + 8, 36, 6);
        for (int i = 0; i < 6; ++i)
        {
            const float x = knobCentre(i).x;
            g.setColour(col::accent.withAlpha(0.8f)); // tick pointing up at the knob
            g.fillRect(juce::Rectangle<float>(2, 4).withCentre({ x, band.getY() + 6 }));
            g.setColour(col::text);
            g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            g.drawFittedText(names[i], juce::Rectangle<float>(pitch - 4, 12).withCentre({ x, band.getY() + 16 }).toNearestInt(), juce::Justification::centred, 1, 0.5f);
            g.setColour(col::accent);
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(juce::String(knobs[i]->getValue(), 1), juce::Rectangle<float>(60, 14).withCentre({ x, band.getY() + 30 }), juce::Justification::centred);
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
        scalePhotos();
    }

    // Photos resampled once per size (at the display's pixel density), not on every paint.
    // The head's tolex in another colour: dark, neutral pixels outside the control panel take the colour,
    // keeping their texture (scaled by `texture`); the gold panel, piping and logo stay as they are.
    static juce::Image recolour(const juce::Image& src, juce::Colour c, float texture)
    {
        auto out = src.createCopy();
        const juce::Image::BitmapData d(out, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < d.height; ++y)
            for (int x = 0; x < d.width; ++x)
            {
                const float px = (float)x / (float)d.width * panelW, py = (float)y / (float)d.height * panelH;
                if (px > 215 && px < 820 && py > 278 && py < 362)
                    continue;
                const auto p = d.getPixelColour(x, y);
                if (p.getAlpha() == 0)
                    continue;
                const float l = 0.3f * p.getFloatRed() + 0.59f * p.getFloatGreen() + 0.11f * p.getFloatBlue();
                const float warm = p.getFloatRed() - p.getFloatBlue();
                const float w = juce::jlimit(0.0f, 1.0f, (0.5f - l) / 0.15f) * juce::jlimit(0.0f, 1.0f, (0.12f - warm) / 0.08f);
                if (w <= 0)
                    continue;
                const float k = 1.0f - 0.35f * texture + 1.6f * texture * l;
                const auto t = juce::Colour::fromFloatRGBA(std::min(1.0f, c.getFloatRed() * k), std::min(1.0f, c.getFloatGreen() * k),
                                                           std::min(1.0f, c.getFloatBlue() * k), p.getFloatAlpha());
                d.setPixelColour(x, y, p.interpolatedWith(t, w));
            }
        return out;
    }

    void scalePhotos()
    {
        const float px = juce::Component::getApproximateScaleFactorForComponent(this);
        const auto area = head();
        if (photo.isValid())
            for (size_t i = 0; i < tolex.size(); ++i)
                headScaled[i] = tolex[i].rescaled(juce::roundToInt(area.getWidth() * px), juce::roundToInt(area.getHeight() * px), juce::Graphics::highResamplingQuality);
        const auto cab = getLocalBounds().toFloat().withTop(area.getBottom()).reduced(14, 0);
        if (!cabPhoto.isValid() || cab.isEmpty())
            return;
        // Full width, like a real stack: the top edge, then the grille window around the logo.
        const int w = cabPhoto.getWidth(), H = cabPhoto.getHeight(), outW = juce::roundToInt(cab.getWidth() * px),
                  outH = juce::roundToInt(cab.getHeight() * px);
        const float sc = (float)outW / (float)w, top = 52; // source px: tolex and piping
        const float rest = (float)outH / sc - top;
        const int srcY = juce::jlimit((int)top, juce::jmax((int)top, H - (int)rest), (int)(H * 0.52f - rest / 2));
        cabScaled = juce::Image(juce::Image::ARGB, outW, outH, true);
        juce::Graphics g(cabScaled);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(cabPhoto, 0, 0, outW, (int)(top * sc + 1), 0, 0, w, (int)top);
        g.drawImage(cabPhoto, 0, (int)(top * sc), outW, (int)(rest * sc) + 1, 0, srcY, w, juce::jmin((int)rest, H - srcY));
    }

    // The panel's own controls: the two toggle switches power the amp on/off (off = pedals straight
    // to the cab), the input jacks pick the channel: cable in I, or I with a jumper to II (JU).
    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto h = hotspot(e.position);
        if (h == Hot::power)
        {
            auto* prm = proc.params.getParameter("ampOn");
            prm->setValueNotifyingHost(prm->getValue() > 0.5f ? 0.0f : 1.0f);
        }
        else if (h == Hot::inputs && proc.circuitMode())
            plugInto(e.position.x > at(762, 0).x);
        else if (h == Hot::inputs)
            switchChannel();
    }
    void mouseMove(const juce::MouseEvent& e) override
    {
        hover = hotspot(e.position);
        setMouseCursor(hover == Hot::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    }
    juce::String getTooltip() override
    {
        return hover == Hot::power ? "Amp on/off (off: pedals go straight to the cab)"
             : hover == Hot::inputs && proc.circuitMode() ? "Inputs: I (bright) or II (normal); click I again to jump it to II"
             : hover == Hot::inputs ? "Channel: input I, or I jumped to II (JU)"
                                    : juce::String();
    }

private:
    enum class Hot { none, power, inputs };
    Hot hotspot(juce::Point<float> pos) const
    {
        const auto a = head();
        const float x = (pos.x - a.getX()) / a.getWidth() * panelW, y = (pos.y - a.getY()) / a.getHeight() * panelH;
        if (y < 285 || y > 360)
            return Hot::none;
        return x > 270 && x < 372 ? Hot::power : x > 715 && x < 810 ? Hot::inputs : Hot::none;
    }
    juce::Point<float> at(float x, float y) const
    {
        const auto a = head();
        return { a.getX() + x / panelW * a.getWidth(), a.getY() + y / panelH * a.getHeight() };
    }
    const Capture* current() const
    {
        const int i = ((juce::AudioParameterChoice*)proc.params.getParameter("capture"))->getIndex();
        return i >= 0 && i < (int)proc.captures.size() ? &proc.captures[(size_t)i] : nullptr;
    }
    // Circuit: the lead goes into the jack clicked; clicking I again adds (or removes) the jumper to II.
    int jack() const { return ((juce::AudioParameterChoice*)proc.params.getParameter("jack"))->getIndex(); }
    void plugInto(bool two)
    {
        const int j = two ? 1 : jack() == 0 ? 2 : 0;
        auto* prm = proc.params.getParameter("jack");
        prm->setValueNotifyingHost(prm->convertTo0to1((float)j));
    }

    // Same level/voicing/pedal on the other channel if it was captured, else the closest match.
    void switchChannel()
    {
        const auto* c = current();
        if (c == nullptr)
            return;
        const juce::String target = c->channel == "I" ? "JU" : "I";
        int best = -1, bestScore = -1;
        for (int i = 0; i < (int)proc.captures.size(); ++i)
        {
            const auto& o = proc.captures[(size_t)i];
            if (o.channel != target)
                continue;
            const int score = (o.level == c->level) * 4 + (o.voicing == c->voicing) * 2 + (o.pedal == c->pedal);
            if (score > bestScore)
                best = i, bestScore = score;
        }
        if (best >= 0)
        {
            auto* prm = proc.params.getParameter("capture");
            prm->setValueNotifyingHost(prm->convertTo0to1((float)best));
        }
    }

    void paintPanelControls(juce::Graphics& g)
    {
        const float s = head().getWidth() / panelW;
        const bool on = proc.params.getParameter("ampOn")->getValue() > 0.5f;

        for (float x : { 288.0f, 324.0f }) // toggle levers: up = on
        {
            const auto c = at(x, 317);
            const auto nut = juce::Rectangle<float>(17 * s, 17 * s).withCentre(c);
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xffeeeeee), nut.getX(), nut.getY(), juce::Colour(0xff555555), nut.getRight(), nut.getBottom(), false));
            g.fillEllipse(nut);
            g.setColour(juce::Colour(0xff222222));
            g.drawEllipse(nut, 1.0f * s);
            const auto tip = c.translated(0, (on ? -14.0f : 14.0f) * s);
            g.setColour(juce::Colour(0xffd0d0d0));
            g.drawLine({ c, tip }, 4.0f * s);
            g.fillEllipse(juce::Rectangle<float>(7 * s, 7 * s).withCentre(tip));
        }
        const auto lamp = juce::Rectangle<float>(15 * s, 21 * s).withCentre(at(359.5f, 320.5f));
        if (on)
        {
            g.setColour(juce::Colour(0x55ff2a1a));
            g.fillEllipse(lamp.expanded(10 * s));
            g.setColour(juce::Colour(0xffff4a36));
            g.fillRoundedRectangle(lamp.reduced(2 * s), 2 * s);
        }
        else
        {
            g.setColour(juce::Colour(0xff2a0806));
            g.fillRoundedRectangle(lamp, 2 * s);
        }

        const auto* c = current();
        auto plug = [&](juce::Point<float> p)
        {
            const auto r = juce::Rectangle<float>(15 * s, 15 * s).withCentre(p);
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff0f0f0), r.getX(), r.getY(), juce::Colour(0xff606060), r.getRight(), r.getBottom(), false));
            g.fillEllipse(r);
            g.setColour(juce::Colour(0xff1a1a1a));
            g.fillEllipse(r.reduced(4 * s));
        };
        auto cable = [&](juce::Point<float> a, juce::Point<float> c1, juce::Point<float> c2, juce::Point<float> b, juce::Colour colour)
        {
            juce::Path p;
            p.startNewSubPath(a);
            p.cubicTo(c1, c2, b);
            g.setColour(colour);
            g.strokePath(p, juce::PathStrokeType(6.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(juce::Colours::white.withAlpha(0.18f));
            g.strokePath(p, juce::PathStrokeType(1.5f * s));
        };
        const bool circuit = proc.circuitMode(), intoII = circuit && jack() == 1;
        const bool jumped = circuit ? jack() == 2 : c != nullptr && c->channel == "JU";
        if (intoII)
            cable(at(790, 303), at(795, 360), at(800, 395), at(805, 430), juce::Colour(0xff151515)); // guitar lead, into II
        else
            cable(at(735, 303), at(740, 360), at(770, 395), at(805, 430), juce::Colour(0xff151515)); // guitar lead, into I
        plug(at(intoII ? 790.0f : 735.0f, 303));
        if (jumped) // jumper: I low -> II high
        {
            cable(at(735, 338), at(760, 372), at(820, 345), at(790, 303), juce::Colour(0xff8a2a22));
            plug(at(735, 338));
            plug(at(790, 303));
        }
    }

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
        if (cabScaled.isValid())
        {
            g.drawImage(cabScaled, cab.withBottom((float)getHeight()));
            return;
        }
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

    MJRAmpProcessor& proc;
    juce::ParameterAttachment ampAtt, captureAtt, modeAtt, jackAtt, modelAtt;
    Hot hover = Hot::none;
    juce::Image photo, cabPhoto, cabScaled;
    std::array<juce::Image, 3> tolex, headScaled; // amp A, B, C
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

// Device names worth showing: drops MIDI bridges and ALSA's per-speaker-layout/HDMI/S/PDIF PCMs.
static juce::StringArray usableDevices(juce::AudioIODeviceType& t, bool input)
{
    juce::StringArray out;
    for (auto& n : t.getDeviceNames(input))
    {
        const auto l = n.toLowerCase();
        if (!(l.contains("midi") || l.contains("surround") || l.contains("hdmi") || l.contains("iec958") || l.contains("direct sample mixing")))
            out.add(n);
    }
    return out;
}

// Under JACK the buffer belongs to the server. On PipeWire it can be forced graph-wide (like a DAW does);
// JUCE's JACK device only reads the size when it opens, so reopen it once the new quantum is live.
static bool pipeWireBuffer(juce::AudioDeviceManager& dm)
{
   #if JUCE_LINUX
    return dm.getCurrentAudioDeviceType() == "JACK";
   #else
    juce::ignoreUnused(dm);
    return false;
   #endif
}
static void forcePipeWireQuantum(int samples) // 0 hands the choice back to PipeWire
{
    juce::ChildProcess pw;
    if (pw.start(juce::StringArray { "pw-metadata", "-n", "settings", "0", "clock.force-quantum", juce::String(samples) }))
        pw.waitForProcessToFinish(2000);
}
static void setBufferSize(juce::StandalonePluginHolder& holder, int samples)
{
    auto& dm = holder.deviceManager;
    if (pipeWireBuffer(dm))
    {
        forcePipeWireQuantum(samples);
        if (holder.settings != nullptr)
            holder.settings->setValue("pipewireQuantum", samples);
        juce::Timer::callAfterDelay(300, [&dm] { dm.closeAudioDevice(), dm.restartLastAudioDevice(); });
        return;
    }
    auto s = dm.getAudioDeviceSetup();
    s.bufferSize = samples;
    dm.setAudioDeviceSetup(s, true);
}

// On PipeWire/JACK systems JACK is the right backend (ALSA hw devices fight PipeWire for the card),
// so switch to it and drop the ALSA list altogether.
static void preferJack(juce::StandalonePluginHolder& holder)
{
    auto& dm = holder.deviceManager;
   #if JUCE_LINUX
    juce::AudioIODeviceType *jack = nullptr, *alsa = nullptr;
    for (auto* t : dm.getAvailableDeviceTypes())
    {
        if (t->getTypeName() == "JACK") jack = t;
        if (t->getTypeName() == "ALSA") alsa = t;
    }
    if (jack == nullptr || alsa == nullptr)
        return;
    jack->scanForDevices();
    if (usableDevices(*jack, false).isEmpty())
        return;
    if (dm.getCurrentAudioDeviceType() != "JACK")
    {
        dm.setCurrentAudioDeviceType("JACK", true);
        // First switch: JACK opens its first client as input (often the laptop mic). A guitar
        // interface is both input and output, so take the input from the same device as the output.
        auto s = dm.getAudioDeviceSetup();
        if (s.inputDeviceName != s.outputDeviceName && jack->getDeviceNames(true).contains(s.outputDeviceName))
        {
            s.inputDeviceName = s.outputDeviceName;
            dm.setAudioDeviceSetup(s, true);
        }
        holder.saveAudioDeviceState();
    }
    if (dm.getCurrentAudioDevice() != nullptr)
        dm.removeAudioDeviceType(alsa);
    if (const int q = holder.settings != nullptr ? holder.settings->getIntValue("pipewireQuantum") : 0; q > 0 && pipeWireBuffer(dm))
        setBufferSize(holder, q);
   #else
    juce::ignoreUnused(holder);
   #endif
}

// Two plain choices (where the guitar comes in, where the sound goes out); the full JUCE page stays behind "Advanced".
class AudioPanel : public juce::Component
{
public:
    AudioPanel(juce::StandalonePluginHolder& h) : holder(h)
    {
        auto& dm = holder.deviceManager;
        const auto setup = dm.getAudioDeviceSetup();
        auto* type = dm.getCurrentDeviceTypeObject();
        if (type != nullptr)
            type->scanForDevices(); // interfaces plugged in after launch
        separate = type == nullptr || type->hasSeparateInputsAndOutputs();
        auto fill = [&](juce::ComboBox& box, bool input, const juce::String& current)
        {
            auto names = type != nullptr ? usableDevices(*type, input) : juce::StringArray();
            if (current.isNotEmpty())
                names.addIfNotAlreadyThere(current);
            box.addItemList(names, 1);
            box.setText(current, juce::dontSendNotification);
            box.onChange = [this] { apply(); };
        };
        fill(in, true, setup.inputDeviceName);
        fill(out, false, setup.outputDeviceName);
        if (auto* dev = dm.getCurrentAudioDevice())
        {
            const auto sizes = pipeWireBuffer(dm) ? juce::Array<int> { 32, 64, 128, 256, 512, 1024 } : dev->getAvailableBufferSizes();
            for (int n : sizes)
                buffer.addItem(juce::String(n) + " samples (" + juce::String(1000.0 * n / dev->getCurrentSampleRate(), 1) + " ms)", n);
            buffer.setSelectedId(dev->getCurrentBufferSizeSamples(), juce::dontSendNotification);
        }
        buffer.onChange = [this]
        {
            setBufferSize(holder, buffer.getSelectedId());
            status.setText("Buffer " + juce::String(buffer.getSelectedId()), juce::dontSendNotification);
        };
        bufLabel.setText("Buffer", juce::dontSendNotification);
        inLabel.setText(separate ? "Guitar input" : "Interface", juce::dontSendNotification);
        outLabel.setText("Output", juce::dontSendNotification);
        for (auto* l : { &inLabel, &outLabel, &bufLabel, &status })
            l->setColour(juce::Label::textColourId, col::dim);
        advanced.setButtonText("Advanced...");
        advanced.onClick = [this] { holder.showAudioSettingsDialog(); };
        for (juce::Component* c : { (juce::Component*)&inLabel, (juce::Component*)&in, (juce::Component*)&bufLabel, (juce::Component*)&buffer,
                                    (juce::Component*)&status })
            addAndMakeVisible(c);
        addChildComponent(advanced); // JUCE's page can't set the buffer under JACK and only confuses there
        advanced.setVisible(!pipeWireBuffer(dm));
        if (separate)
        {
            addAndMakeVisible(outLabel);
            addAndMakeVisible(out);
        }
        setSize(380, separate ? 186 : 150);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(12);
        for (auto [label, box] : { std::pair { &inLabel, &in }, std::pair { &outLabel, &out }, std::pair { &bufLabel, &buffer } })
        {
            if (!box->isVisible())
                continue;
            auto row = r.removeFromTop(28);
            label->setBounds(row.removeFromLeft(100));
            box->setBounds(row);
            r.removeFromTop(8);
        }
        auto row = r.removeFromBottom(28);
        advanced.setBounds(row.removeFromRight(110));
        status.setBounds(row);
    }

private:
    void apply()
    {
        auto& dm = holder.deviceManager;
        auto s = dm.getAudioDeviceSetup();
        s.inputDeviceName = in.getText();
        s.outputDeviceName = separate ? out.getText() : in.getText();
        s.useDefaultInputChannels = s.useDefaultOutputChannels = true;
        const auto err = dm.setAudioDeviceSetup(s, true);
        holder.saveAudioDeviceState(); // JUCE only saves on a clean quit
        status.setText(err.isEmpty() ? "OK" : err, juce::dontSendNotification);
    }

    juce::StandalonePluginHolder& holder;
    bool separate = true;
    juce::ComboBox in, out, buffer;
    juce::Label inLabel, outLabel, bufLabel, status;
    juce::TextButton advanced;
};

// Preset menu: re-reads the presets folder each time it opens, so files added by hand show up.
struct PresetBox : juce::ComboBox
{
    std::function<void()> beforePopup;
    void showPopup() override
    {
        if (beforePopup)
            beforePopup();
        juce::ComboBox::showPopup();
    }
};

class Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int baseW = 1280, baseH = 900; // laid out at this size, scaled to the window

    Editor(MJRAmpProcessor& proc)
        : AudioProcessorEditor(proc), p(proc), rig(proc), tuner(proc), input(proc.params, "input", "INPUT"), gate(proc.params, "gate", "GATE"), transpose(proc.params, "transpose", "TRANSPOSE"),
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

        captured.setJustificationType(juce::Justification::centred);
        captured.setColour(juce::Label::textColourId, col::dim);

        for (auto* b : { &circuitButton, &captureButton })
        {
            b->setRadioGroupId(2);
            b->setClickingTogglesState(true);
            b->onClick = [this, b]
            {
                auto* prm = p.params.getParameter("ampMode");
                prm->setValueNotifyingHost(prm->convertTo0to1(b == &captureButton ? 1.0f : 0.0f));
            };
        }
        const char* modelTips[] = { "Amp A: clean voicing", "Amp B: the 1967 circuit, as drawn",
                                    "Amp C: 1967 with the 1968 lead mods (100k feedback, -70 V bias, hotter input)" };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = modelButtons[(size_t)i];
            b.setButtonText(juce::String::charToString((juce::juce_wchar)('A' + i)));
            b.setTooltip(modelTips[i]);
            b.setRadioGroupId(3);
            b.setClickingTogglesState(true);
            b.onClick = [this, i]
            {
                auto* prm = p.params.getParameter("circuit");
                prm->setValueNotifyingHost(prm->convertTo0to1((float)i));
            };
            content.addChildComponent(b);
        }
        circuitButton.setButtonText("CIRCUIT");
        circuitButton.setTooltip("Circuit model of the amp: the knobs act as on the real panel");
        captureButton.setButtonText("CAPTURE");
        captureButton.setTooltip("NAM captures of the real amp, chosen from the knobs");

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
            preferJack(*holder);
            audioButton.onClick = [this, holder]
            {
                juce::CallOutBox::launchAsynchronously(std::make_unique<AudioPanel>(*holder), content.getLocalArea(&audioButton, audioButton.getLocalBounds()),
                                                       &content);
            };
            content.addAndMakeVisible(audioButton);
        }
        libraryButton.onClick = [this] { chooseLibrary(); };

        presetBox.setTextWhenNothingSelected("No preset");
        presetBox.setTooltip("Presets: " + p.presetDir.getFullPathName());
        presetBox.beforePopup = [this] { refreshPresets(); };
        presetBox.onChange = [this]
        {
            const auto files = p.listPresets();
            if (const int i = presetBox.getSelectedId() - 1; i >= 0 && i < files.size())
                p.loadPreset(files[i]);
        };
        prevPreset.setButtonText("<");
        nextPreset.setButtonText(">");
        prevPreset.onClick = [this] { stepPreset(-1); };
        nextPreset.onClick = [this] { stepPreset(1); };
        savePresetButton.setButtonText("Save");
        savePresetButton.setTooltip("Save the current sound as a preset");
        savePresetButton.onClick = [this] { askPresetName(); };
        for (juce::Component* c : { (juce::Component*)&prevPreset, (juce::Component*)&presetBox, (juce::Component*)&nextPreset, (juce::Component*)&savePresetButton })
            content.addAndMakeVisible(c);
        refreshPresets();

        for (int s = 0; s < MJRAmpProcessor::numIrSlots; ++s)
            irs.add(new IrBlock(p, s));

        for (juce::Component* c : { (juce::Component*)&title, (juce::Component*)&input, (juce::Component*)&gate, (juce::Component*)&transpose, (juce::Component*)&inputSwitch, (juce::Component*)&output, (juce::Component*)&inMeter,
                                    (juce::Component*)&outMeter, (juce::Component*)&tunerButton, (juce::Component*)&metroButton,
                                    (juce::Component*)&libraryButton, (juce::Component*)&prevCapture, (juce::Component*)&capture,
                                    (juce::Component*)&nextCapture, (juce::Component*)&captured,
                                    (juce::Component*)&resetKnobs, (juce::Component*)&circuitButton, (juce::Component*)&captureButton,
                                    (juce::Component*)&rig })
            content.addAndMakeVisible(c);
        for (auto* b : irs)
            content.addAndMakeVisible(b);
        for (auto* page : { &prePage, &eqPage, &postPage })
            content.addChildComponent(page);
        int n = 0;
        for (auto* b : { &preNav, &ampNav, &eqNav, &postNav })
        {
            b->setClickingTogglesState(true);
            b->setRadioGroupId(2);
            b->onClick = [this] { showPage(); };
            b->setTooltip(n == 0 ? "Pre effects" : n == 1 ? "Amp and cab" : n == 2 ? "EQ" : "Post effects");
            content.addAndMakeVisible(b);
            ++n;
        }
        ampNav.setToggleState(true, juce::dontSendNotification);
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
        if (auto* holder = juce::StandalonePluginHolder::getInstance(); holder != nullptr && pipeWireBuffer(holder->deviceManager))
            forcePipeWireQuantum(0); // give the other apps their buffer back
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
        prevPreset.setBounds(baseW - 470, 14, 28, 28);
        presetBox.setBounds(baseW - 438, 14, 250, 28);
        nextPreset.setBounds(baseW - 184, 14, 28, 28);
        savePresetButton.setBounds(baseW - 148, 14, 70, 28);
        int x = baseW / 2 - 140; // page navigation, in signal order
        for (auto* b : { &preNav, &ampNav, &eqNav, &postNav })
            b->setBounds(x, 8, 70, 58), x += 70;
        x = 14; // utilities, bottom left
        for (auto* b : { &tunerButton, &metroButton, &libraryButton, &audioButton })
            b->setBounds(x, baseH - 72, 56, 58), x += 56;

        inMeter.setBounds(34, 60, 6, 140);
        input.setBounds(60, 56, 130, 150);
        gate.setBounds(190, 86, 86, 120);
        transpose.setBounds(278, 86, 86, 120);
        inputSwitch.setBounds(370, 120, 80, 50);
        output.setBounds(baseW - 190, 56, 130, 150);
        outMeter.setBounds(baseW - 40, 60, 6, 140);

        circuitButton.setBounds(586, 122, 100, 24); // where the CAPTURE heading was: the buttons name the section
        captureButton.setBounds(686, 122, 100, 24);
        for (int i = 0; i < 3; ++i) // circuit A / B / C, in the capture menu's place
            modelButtons[(size_t)i].setBounds(611 + 50 * i, 88, 50, 30);
        prevCapture.setBounds(468, 88, 32, 34);
        capture.setBounds(506, 88, 360, 34);
        nextCapture.setBounds(872, 88, 32, 34);
        captured.setBounds(436, 150, 500, 18);
        resetKnobs.setBounds(631, 170, 110, 26);

        for (int s = 0; s < irs.size(); ++s)
            irs[s]->setBounds(s < 2 ? 30 : baseW - 230, 250 + (s % 2) * 300, 200, 230);

        rig.setBounds(240, 236, 800, baseH - 236);
        for (auto* page : { &prePage, &eqPage, &postPage })
            page->setBounds(0, 236, baseW, baseH - 236 - 80);
        tuner.setBounds(0, 236, baseW, baseH - 236 - 80);
    }

    void showPage()
    {
        const bool amp = ampNav.getToggleState();
        rig.setVisible(amp);
        for (auto* b : irs)
            b->setVisible(amp);
        prePage.setVisible(preNav.getToggleState());
        eqPage.setVisible(eqNav.getToggleState());
        postPage.setVisible(postNav.getToggleState());
    }

    static juce::String channelName(const Capture& c) { return c.channel == "I" ? "Channel I" : "Channels I + II"; }
    static juce::String fullName(const Capture& c)
    {
        return (c.channel == "I" ? "I" : "I+II") + juce::String("   ") + c.level + "   " + c.voicing + (c.pedal.isEmpty() ? "" : "   + " + c.pedal);
    }
    int captureIndex() const { return ((juce::AudioParameterChoice*)p.params.getParameter("capture"))->getIndex(); }
    int modelIndex() const { return ((juce::AudioParameterChoice*)p.params.getParameter("circuit"))->getIndex(); }
    int jackIndex() const { return ((juce::AudioParameterChoice*)p.params.getParameter("jack"))->getIndex(); }

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
        const bool circuit = shownCircuit = p.circuitMode();
        shownJack = jackIndex();
        shownModel = modelIndex();
        for (int i = 0; i < 3; ++i)
            modelButtons[(size_t)i].setVisible(circuit), modelButtons[(size_t)i].setToggleState(i == shownModel, juce::dontSendNotification);
        (circuit ? circuitButton : captureButton).setToggleState(true, juce::dontSendNotification);
        for (juce::Component* c : { (juce::Component*)&prevCapture, (juce::Component*)&capture, (juce::Component*)&nextCapture,
                                    (juce::Component*)&resetKnobs })
            c->setVisible(!circuit);
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
        shownLibrary = p.getLibraryDir(); // the file check touches the disk: only on a change, not every tick
        rig.missing.setText("Capture not found.\nChoose the library folder (LIBRARY).", juce::dontSendNotification);
        rig.missing.setVisible(!circuit && !shownLibrary.getChildFile(c.file).existsAsFile());
        if (circuit)
        {
            const char* inputs[] = { "input I (bright)", "input II (normal)", "input I jumped to II" };
            const char* models[] = { "Amp A, clean voicing", "Amp B, the 1967 circuit as drawn", "Amp C, 1967 with the 1968 lead mods" };
            captured.setText(juce::String(models[shownModel]) + "  -  " + inputs[shownJack] + "  -  click the input jacks to change",
                             juce::dontSendNotification);
        }
    }

    // Lists the presets folder; the current preset (if its file is there) is shown selected.
    void refreshPresets()
    {
        shownPreset = p.currentPreset;
        presetBox.clear(juce::dontSendNotification);
        const auto files = p.listPresets();
        for (int i = 0; i < files.size(); ++i)
        {
            presetBox.addItem(files[i].getFileNameWithoutExtension(), i + 1);
            if (files[i].getFileNameWithoutExtension() == p.currentPreset)
                presetBox.setSelectedId(i + 1, juce::dontSendNotification);
        }
    }

    void stepPreset(int dir)
    {
        refreshPresets();
        if (const int n = presetBox.getNumItems(); n > 0)
            presetBox.setSelectedId((presetBox.getSelectedId() - 1 + dir + n) % n + 1); // loads it (onChange)
    }

    void askPresetName()
    {
        auto* w = new juce::AlertWindow("Save preset", "Name (an existing preset with this name is replaced):", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor("name", p.currentPreset);
        w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int result)
        {
            if (result == 1 && p.savePreset(w->getTextEditorContents("name")))
                refreshPresets();
        }), true);
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
        if (captureIndex() != shownCapture || p.getLibraryDir() != shownLibrary || p.circuitMode() != shownCircuit
            || jackIndex() != shownJack || modelIndex() != shownModel)
            refreshCapture();
        if (p.currentPreset != shownPreset) // a session or preset loaded from elsewhere
            refreshPresets();

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
    juce::Label title, captured;
    std::vector<int> order;
    juce::ComboBox capture;
    juce::TextButton prevCapture, nextCapture, resetKnobs, circuitButton, captureButton;
    std::array<juce::TextButton, 3> modelButtons;
    IconButton preNav { "PRE FX", IconButton::pedal }, ampNav { "AMP", IconButton::amp }, eqNav { "EQ", IconButton::eq },
        postNav { "POST FX", IconButton::pedal };
    FxPage prePage { p, FxStage::pre, "INPUT", "AMP" }, eqPage { p, FxStage::eq, "AMP", "CAB" },
        postPage { p, FxStage::post, "CAB", "OUTPUT" };
    IconButton tunerButton { "TUNER", IconButton::tuner }, metroButton { "METRO", IconButton::metronome },
        libraryButton { "LIBRARY", IconButton::folder }, audioButton { "AUDIO", IconButton::audio };
    ChannelSwitch inputSwitch;
    juce::AudioProcessorValueTreeState::ButtonAttachment inputSwitchAtt { p.params, "inputSource", inputSwitch };
    Rig rig;
    TunerView tuner;
    juce::OwnedArray<IrBlock> irs;
    LabeledKnob input, gate, transpose, output;
    Meter inMeter, outMeter;
    std::unique_ptr<juce::FileChooser> chooser;
    PresetBox presetBox;
    juce::TextButton prevPreset, nextPreset, savePresetButton;
    juce::String shownPreset;
    int shownCapture = -1, shownBeat = 0, shownJack = -1, shownModel = -1;
    bool shownCircuit = false;
    juce::File shownLibrary;
    juce::uint32 beatLit = 0;
};
} // namespace

juce::AudioProcessorEditor* createMJREditor(MJRAmpProcessor& p) { return new Editor(p); }
