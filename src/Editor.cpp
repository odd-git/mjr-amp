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

// Placeholder effect pedal: name, LED, footswitch. Click toggles the module on/off.
struct Pedal : juce::ToggleButton
{
    Pedal(juce::AudioProcessorValueTreeState& params, const FxInfo& fx, juce::Colour c)
        : juce::ToggleButton(fx.name), colour(c), placeholder(fx.create == nullptr), attachment(params, juce::String(fx.id) + "On", *this)
    {
        setTooltip(placeholder ? juce::String(fx.name) + " (placeholder: audio passes through)" : juce::String(fx.name));
        for (int k = 0; k < fx.numKnobs(); ++k)
            addAndMakeVisible(knobs.add(new LabeledKnob(params, juce::String(fx.id) + "K" + juce::String(k), fx.knobs[(size_t)k].name)));
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced(10).withTrimmedTop(80).withTrimmedBottom(96);
        const int cols = knobs.size() > 2 ? 2 : knobs.size(), cw = cols > 0 ? r.getWidth() / cols : 0;
        for (int k = 0; k < knobs.size(); ++k)
            knobs[k]->setBounds(r.getX() + (k % cols) * cw, r.getY() + (k / cols) * 84, cw, 84);
    }
    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced(4);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRoundedRectangle(r.translated(3, 5), 12);
        g.setGradientFill(juce::ColourGradient(colour.brighter(0.15f), r.getX(), r.getY(), colour.darker(0.5f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle(r, 12);
        if (over)
        {
            g.setColour(juce::Colours::white.withAlpha(0.25f));
            g.drawRoundedRectangle(r, 12, 2);
        }
        const bool on = getToggleState();
        const auto led = juce::Rectangle<float>(12, 12).withCentre({ r.getCentreX(), r.getY() + 24 });
        g.setColour(on ? juce::Colour(0xffff3b30) : juce::Colour(0xff3a1210));
        g.fillEllipse(led);
        if (on)
        {
            g.setColour(juce::Colour(0x55ff3b30));
            g.fillEllipse(led.expanded(6));
        }
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        g.drawText(getName().toUpperCase(), r.withTrimmedTop(44).removeFromTop(30), juce::Justification::centred);
        if (placeholder)
        {
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText("placeholder", r.withTrimmedTop(74).removeFromTop(16), juce::Justification::centred);
        }
        const auto sw = juce::Rectangle<float>(46, 46).withCentre({ r.getCentreX(), r.getBottom() - 44 });
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe8e8e8), sw.getX(), sw.getY(), juce::Colour(0xff6a6a6a), sw.getRight(), sw.getBottom(), false));
        g.fillEllipse(sw);
        g.setColour(juce::Colour(0xff2a2a2a));
        g.drawEllipse(sw, 2);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(on ? "ON" : "OFF", r.removeFromBottom(14), juce::Justification::centred);
    }
    juce::Colour colour;
    bool placeholder;
    juce::OwnedArray<LabeledKnob> knobs;
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
};

// A page of pedals for one stage of the chain, laid out left to right in signal order.
class FxPage : public juce::Component
{
public:
    FxPage(juce::AudioProcessorValueTreeState& params, FxStage stage, juce::String from, juce::String to)
        : source(std::move(from)), destination(std::move(to))
    {
        const juce::Colour palette[] = { juce::Colour(0xff2f5d8c), juce::Colour(0xff6b3fa0), juce::Colour(0xffb5651d), juce::Colour(0xff2e7d6b),
                                         juce::Colour(0xff7a7a2a), juce::Colour(0xff3d6fb0), juce::Colour(0xff8c2f4f), juce::Colour(0xff4a4a8c),
                                         juce::Colour(0xff9c4a1a), juce::Colour(0xff2a7a8c) };
        for (int i = 0; i < numFx; ++i)
            if (fxChain[i].stage == stage)
                addAndMakeVisible(*pedals.add(new Pedal(params, fxChain[i], palette[i % 10])));
    }

    void paint(juce::Graphics& g) override
    {
        if (pedals.isEmpty())
            return;
        const float y = (float)pedals[0]->getBounds().getCentreY();
        g.setColour(col::line);
        g.drawLine(40, y, (float)getWidth() - 40, y, 3);
        g.setColour(col::dim);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(source, juce::Rectangle<float>(0, y - 40, 120, 20), juce::Justification::centred);
        g.drawText(destination, juce::Rectangle<float>((float)getWidth() - 120, y - 40, 120, 20), juce::Justification::centred);
    }

    void resized() override
    {
        const int w = 170, gap = 40, total = pedals.size() * w + (pedals.size() - 1) * gap;
        int x = (getWidth() - total) / 2;
        for (auto* p : pedals)
            p->setBounds(x, getHeight() / 2 - 230, w, 440), x += w + gap;
    }

private:
    juce::OwnedArray<Pedal> pedals;
    juce::String source, destination;
};

// The rig: amp head (photo or drawn) standing on a drawn cabinet. The cabinet's top band
// names each amp knob and shows its value, right under the knob.
class Rig : public juce::Component, public juce::TooltipClient
{
public:
    Rig(MJRAmpProcessor& p)
        : proc(p), ampAtt(*p.params.getParameter("ampOn"), [this](float) { repaint(); }),
          captureAtt(*p.params.getParameter("capture"), [this](float) { repaint(); })
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
        paintPanelControls(g);

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
        cable(at(735, 303), at(740, 360), at(770, 395), at(805, 430), juce::Colour(0xff151515)); // guitar lead, into I
        plug(at(735, 303));
        if (c != nullptr && c->channel == "JU") // jumper: I low -> II high
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
    juce::ParameterAttachment ampAtt, captureAtt;
    Hot hover = Hot::none;
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
            preferJack(*holder);
            audioButton.onClick = [this, holder]
            {
                juce::CallOutBox::launchAsynchronously(std::make_unique<AudioPanel>(*holder), content.getLocalArea(&audioButton, audioButton.getLocalBounds()),
                                                       &content);
            };
            content.addAndMakeVisible(audioButton);
        }
        libraryButton.onClick = [this] { chooseLibrary(); };

        for (int s = 0; s < MJRAmpProcessor::numIrSlots; ++s)
            irs.add(new IrBlock(p, s));

        for (juce::Component* c : { (juce::Component*)&title, (juce::Component*)&input, (juce::Component*)&gate, (juce::Component*)&transpose, (juce::Component*)&inputSwitch, (juce::Component*)&output, (juce::Component*)&inMeter,
                                    (juce::Component*)&outMeter, (juce::Component*)&tunerButton, (juce::Component*)&metroButton,
                                    (juce::Component*)&libraryButton, (juce::Component*)&prevCapture, (juce::Component*)&capture,
                                    (juce::Component*)&nextCapture, (juce::Component*)&captureLabel, (juce::Component*)&captured,
                                    (juce::Component*)&resetKnobs, (juce::Component*)&rig })
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

        prevCapture.setBounds(468, 88, 32, 34);
        capture.setBounds(506, 88, 360, 34);
        nextCapture.setBounds(872, 88, 32, 34);
        captureLabel.setBounds(506, 126, 360, 18);
        captured.setBounds(436, 146, 500, 18);
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
    IconButton preNav { "PRE FX", IconButton::pedal }, ampNav { "AMP", IconButton::amp }, eqNav { "EQ", IconButton::eq },
        postNav { "POST FX", IconButton::pedal };
    FxPage prePage { p.params, FxStage::pre, "INPUT", "AMP" }, eqPage { p.params, FxStage::eq, "AMP", "CAB" },
        postPage { p.params, FxStage::post, "CAB", "OUTPUT" };
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
    int shownCapture = -1, shownBeat = 0;
    juce::uint32 beatLit = 0;
};
} // namespace

juce::AudioProcessorEditor* createMJREditor(MJRAmpProcessor& p) { return new Editor(p); }
