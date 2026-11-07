// Editor: top bar (capture selection, library, in/out), Amp view (panel with the six
// knobs), Cab view (four IR slots). Layout and colours are our own.
#include "Processor.h"
#include "BinaryData.h"
#include "Pitch.h"

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
            track.addCentredArc(c.x, c.y, rad - 3, rad - 3, 0, start, end, true);
            fill.addCentredArc(c.x, c.y, rad - 3, rad - 3, 0, start, a, true);
            g.setColour(col::line);
            g.strokePath(track, juce::PathStrokeType(3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(col::accent);
            g.strokePath(fill, juce::PathStrokeType(3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillEllipse(juce::Rectangle<float>(5, 5).withCentre(c.getPointOnCircumference(rad - 3, a)));
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

class AmpView : public juce::Component
{
public:
    AmpView(MJRAmpProcessor& p)
    {
#if MJR_HAS_PHOTO
        photo = juce::ImageCache::getFromMemory(BinaryData::amp_png, BinaryData::amp_pngSize);
#endif
        for (int i = 0; i < 6; ++i)
            addAndMakeVisible(*knobs.add(new Knob(p.params, MJRAmpProcessor::knobIds[i], true)));
        for (auto* l : { &captured, &missing })
        {
            l->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(*l);
        }
        captured.setColour(juce::Label::textColourId, col::dim);
        missing.setColour(juce::Label::textColourId, col::accent);
    }

    juce::Label captured, missing;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(col::bg);
        const auto area = panelArea();
        if (photo.isValid())
        {
            g.drawImage(photo, area);
            return;
        }
        // Drawn head, same geometry as the photo: tolex box, piping, gold plate.
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
        const char* names[] = { "PRESENCE", "BASS", "MIDDLE", "TREBLE", "VOLUME I", "VOLUME II" };
        for (int i = 0; i < 6; ++i)
            g.drawText(names[i], at(knobX[i] - 30, 288, 60, 10), juce::Justification::centred);
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

    void resized() override
    {
        const auto area = panelArea();
        const float s = area.getWidth() / panelW;
        for (int i = 0; i < 6; ++i)
            knobs[i]->setBounds(juce::Rectangle<float>(knobR * 2.4f * s, knobR * 2.4f * s).withCentre(knobCentre(i)).toNearestInt());
        captured.setBounds(getLocalBounds().removeFromBottom(28));
        missing.setBounds(area.toNearestInt().removeFromTop((int)(area.getHeight() * 0.3f)));
    }

private:
    juce::Rectangle<float> panelArea() const
    {
        auto r = getLocalBounds().toFloat().reduced(16, 30);
        return r.withSizeKeepingCentre(std::min(r.getWidth(), r.getHeight() * panelW / panelH),
                                       std::min(r.getHeight(), r.getWidth() * panelH / panelW));
    }
    juce::Point<float> knobCentre(int i) const
    {
        const auto a = panelArea();
        return { a.getX() + knobX[i] / panelW * a.getWidth(), a.getY() + knobY / panelH * a.getHeight() };
    }

    juce::Image photo;
    juce::OwnedArray<Knob> knobs;
};

class CabView : public juce::Component
{
public:
    CabView(MJRAmpProcessor& proc) : p(proc)
    {
        for (int s = 0; s < MJRAmpProcessor::numIrSlots; ++s)
        {
            auto& sl = *slots.add(new Slot);
            const juce::String id = "ir" + juce::String(s + 1);
            sl.title.setText("IR " + juce::String(s + 1), juce::dontSendNotification);
            sl.title.setColour(juce::Label::textColourId, col::accent);
            sl.on = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.params, id + "On", sl.power);
            sl.power.setButtonText("On");
            sl.level = std::make_unique<Knob>(p.params, id + "Level", false);
            sl.pan = std::make_unique<Knob>(p.params, id + "Pan", false);
            sl.file.onClick = [this, s] { choose(s); };
            sl.file.setTooltip("Load an impulse response (.wav)");
            for (juce::Component* c : { (juce::Component*)&sl.title, (juce::Component*)&sl.power, (juce::Component*)&sl.file,
                                        (juce::Component*)sl.level.get(), (juce::Component*)sl.pan.get(), (juce::Component*)&sl.levelLabel,
                                        (juce::Component*)&sl.panLabel })
                addAndMakeVisible(c);
            sl.levelLabel.setText("LEVEL", juce::dontSendNotification);
            sl.panLabel.setText("PAN", juce::dontSendNotification);
            for (auto* l : { &sl.levelLabel, &sl.panLabel })
            {
                l->setJustificationType(juce::Justification::centred);
                l->setColour(juce::Label::textColourId, col::dim);
            }
        }
        refresh();
    }

    void refresh()
    {
        for (int s = 0; s < slots.size(); ++s)
        {
            const auto f = p.getIrFile(s);
            slots[s]->file.setButtonText(f.existsAsFile() ? f.getFileNameWithoutExtension() : "Load IR...");
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(col::bg);
        for (auto* sl : slots)
        {
            g.setColour(col::bar);
            g.fillRoundedRectangle(sl->card.toFloat(), 8);
            g.setColour(col::line);
            g.drawRoundedRectangle(sl->card.toFloat(), 8, 1);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(24, 40);
        const int w = (r.getWidth() - 3 * 16) / 4;
        r = r.withSizeKeepingCentre(r.getWidth(), std::min(r.getHeight(), w + 90));
        for (auto* sl : slots)
        {
            sl->card = r.removeFromLeft(w);
            r.removeFromLeft(16);
            auto c = sl->card.reduced(14);
            auto top = c.removeFromTop(28);
            sl->power.setBounds(top.removeFromRight(56));
            sl->title.setBounds(top);
            c.removeFromTop(10);
            sl->file.setBounds(c.removeFromTop(30));
            c.removeFromTop(20);
            auto knobs = c.removeFromTop(std::min(c.getHeight() - 20, w / 2 - 10));
            auto labels = c.removeFromTop(20);
            sl->level->setBounds(knobs.removeFromLeft(knobs.getWidth() / 2).reduced(8));
            sl->pan->setBounds(knobs.reduced(8));
            sl->levelLabel.setBounds(labels.removeFromLeft(labels.getWidth() / 2));
            sl->panLabel.setBounds(labels);
        }
    }

private:
    struct Slot
    {
        juce::Label title, levelLabel, panLabel;
        juce::ToggleButton power;
        juce::TextButton file;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> on;
        std::unique_ptr<Knob> level, pan;
        juce::Rectangle<int> card;
    };

    void choose(int s)
    {
        chooser = std::make_unique<juce::FileChooser>("Impulse response", p.getIrFile(s).existsAsFile() ? p.getIrFile(s) : p.getLibraryDir(),
                                                      "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this, s](const juce::FileChooser& fc)
                             {
                                 if (fc.getResult().existsAsFile())
                                     p.setIrFile(s, fc.getResult()), refresh();
                             });
    }

    MJRAmpProcessor& p;
    juce::OwnedArray<Slot> slots;
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
    Editor(MJRAmpProcessor& proc)
        : AudioProcessorEditor(proc), p(proc), amp(proc), cab(proc), tuner(proc), input(proc.params, "input", false), output(proc.params, "output", false)
    {
        setLookAndFeel(&look);
        juce::LookAndFeel::setDefaultLookAndFeel(&look); // popups and choosers

        title.setText("MJR AMP", juce::dontSendNotification);
        title.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, col::accent);

        for (auto& ch : channels())
        {
            auto* b = channelButtons.add(new juce::TextButton(ch));
            b->setClickingTogglesState(false);
            b->setTooltip(ch == "I" ? "Channel I" : "Channels I + II jumpered");
            b->onClick = [this, ch] { select(ch, current().level, current().voicing, current().pedal); };
            addAndMakeVisible(b);
        }
        level.onChange = [this] { if (!updating) select(current().channel, level.getText(), current().voicing, current().pedal); };
        voicing.onChange = [this]
        {
            if (updating)
                return;
            auto parts = juce::StringArray::fromTokens(voicing.getText(), " ", "");
            select(current().channel, current().level, parts[0], parts[1]);
        };
        level.setTooltip("Gain level of the capture");
        voicing.setTooltip("EQ voicing of the capture (and pedal, if any)");

        library.setButtonText("Library...");
        library.setTooltip("Folder with the .nam captures and IRs");
        library.onClick = [this] { chooseLibrary(); };
        resetKnobs.setButtonText("Reset knobs");
        resetKnobs.setTooltip("Set the knobs back to the capture's recorded settings");
        resetKnobs.onClick = [this] { p.resetKnobsToCapture(); };

        tunerButton.setButtonText("TUNER");
        tunerButton.setClickingTogglesState(true);
        tunerButton.setTooltip("Tuner (mutes the output)");
        tunerButton.onClick = [this] { showView(); };
        metroButton.setButtonText("METRO");
        metroButton.setTooltip("Metronome");
        metroButton.onClick = [this]
        {
            juce::CallOutBox::launchAsynchronously(std::make_unique<MetroPanel>(p), getLocalArea(&metroButton, metroButton.getLocalBounds()), this);
        };

        for (auto* t : { &ampTab, &cabTab })
        {
            t->setClickingTogglesState(true);
            t->setRadioGroupId(1);
            t->onClick = [this] { showView(); };
            addAndMakeVisible(t);
        }
        ampTab.setButtonText("AMP");
        cabTab.setButtonText("CAB");
        ampTab.setToggleState(true, juce::dontSendNotification);

        for (auto* l : { &inLabel, &outLabel })
        {
            l->setColour(juce::Label::textColourId, col::dim);
            l->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(l);
        }
        inLabel.setText("IN", juce::dontSendNotification);
        outLabel.setText("OUT", juce::dontSendNotification);

        for (juce::Component* c : { (juce::Component*)&title, (juce::Component*)&level, (juce::Component*)&voicing,
                                    (juce::Component*)&library, (juce::Component*)&resetKnobs, (juce::Component*)&input,
                                    (juce::Component*)&output, (juce::Component*)&inMeter, (juce::Component*)&outMeter,
                                    (juce::Component*)&amp, (juce::Component*)&cab, (juce::Component*)&tuner,
                                    (juce::Component*)&tunerButton, (juce::Component*)&metroButton })
            addAndMakeVisible(c);
        showView();
        refreshCapture();

        setResizable(true, true);
        setResizeLimits(800, 480, 1800, 1080);
        getConstrainer()->setFixedAspectRatio(1200.0 / 700.0);
        setSize(1200, 700);
        startTimerHz(30);
    }

    ~Editor() override
    {
        p.tunerActive = false; // never leave the plugin muted when the window closes
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(col::bg);
        g.setColour(col::bar);
        g.fillRect(getLocalBounds().removeFromTop(topH));
        g.fillRect(getLocalBounds().removeFromBottom(navH));
        g.setColour(col::line);
        g.drawHorizontalLine(topH, 0, (float)getWidth());
        g.drawHorizontalLine(getHeight() - navH, 0, (float)getWidth());

        const bool lit = p.params.getRawParameterValue("metroOn")->load() > 0.5f && juce::Time::getMillisecondCounter() - beatLit < 110;
        g.setColour(lit ? (downbeat ? col::accent : col::text) : col::line);
        g.fillEllipse(beatLight.toFloat());
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop(topH).reduced(14, 10);
        title.setBounds(top.removeFromLeft(110));
        for (auto* b : channelButtons)
            b->setBounds(top.removeFromLeft(44)), top.removeFromLeft(4);
        top.removeFromLeft(8);
        level.setBounds(top.removeFromLeft(120));
        top.removeFromLeft(8);
        voicing.setBounds(top.removeFromLeft(130));
        top.removeFromLeft(8);
        resetKnobs.setBounds(top.removeFromLeft(100));

        outMeter.setBounds(top.removeFromRight(8));
        top.removeFromRight(4);
        output.setBounds(top.removeFromRight(top.getHeight()));
        outLabel.setBounds(top.removeFromRight(34));
        top.removeFromRight(10);
        inMeter.setBounds(top.removeFromRight(8));
        top.removeFromRight(4);
        input.setBounds(top.removeFromRight(top.getHeight()));
        inLabel.setBounds(top.removeFromRight(28));
        top.removeFromRight(14);
        library.setBounds(top.removeFromRight(90));
        top.removeFromRight(14);
        beatLight = top.removeFromRight(10).withSizeKeepingCentre(10, 10);
        top.removeFromRight(6);
        metroButton.setBounds(top.removeFromRight(72));
        top.removeFromRight(6);
        tunerButton.setBounds(top.removeFromRight(72));

        auto nav = r.removeFromBottom(navH).withSizeKeepingCentre(260, navH - 14);
        ampTab.setBounds(nav.removeFromLeft(126));
        cabTab.setBounds(nav.removeFromRight(126));
        amp.setBounds(r);
        cab.setBounds(r);
        tuner.setBounds(r);
    }

private:
    static constexpr int topH = 56, navH = 50;

    juce::StringArray channels() const
    {
        juce::StringArray s;
        for (auto& c : p.captures)
            s.addIfNotAlreadyThere(c.channel);
        return s;
    }
    int captureIndex() const { return ((juce::AudioParameterChoice*)p.params.getParameter("capture"))->getIndex(); }
    const Capture& current() const { return p.captures[(size_t)captureIndex()]; }

    // Closest existing capture to the request: same channel, then level, then voicing.
    void select(const juce::String& ch, const juce::String& lv, const juce::String& vo, const juce::String& pe)
    {
        int best = -1, bestScore = -1;
        for (int i = 0; i < (int)p.captures.size(); ++i)
        {
            const auto& c = p.captures[(size_t)i];
            const int score = (c.channel == ch) * 8 + (c.level == lv) * 4 + (c.voicing == vo) * 2 + (c.pedal == pe);
            if (score > bestScore)
                best = i, bestScore = score;
        }
        auto* prm = p.params.getParameter("capture");
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1((float)best));
        prm->endChangeGesture();
    }

    void refreshCapture()
    {
        const juce::ScopedValueSetter<bool> guard(updating, true);
        const auto& c = current();
        for (auto* b : channelButtons)
            b->setToggleState(b->getButtonText() == c.channel, juce::dontSendNotification);

        level.clear(juce::dontSendNotification);
        voicing.clear(juce::dontSendNotification);
        juce::StringArray levels, voicings;
        for (auto& x : p.captures)
            if (x.channel == c.channel)
            {
                levels.addIfNotAlreadyThere(x.level);
                if (x.level == c.level)
                    voicings.add(x.pedal.isEmpty() ? x.voicing : x.voicing + " " + x.pedal);
            }
        level.addItemList(levels, 1);
        voicing.addItemList(voicings, 1);
        level.setText(c.level, juce::dontSendNotification);
        voicing.setText(c.pedal.isEmpty() ? c.voicing : c.voicing + " " + c.pedal, juce::dontSendNotification);

        juce::String info;
        if (c.hasKnobs)
        {
            const char* k[] = { "P", "B", "M", "T", "VI", "VII" };
            for (int i = 0; i < 6; ++i)
                info << k[i] << juce::String(c.knobs[(size_t)i], 1).trimCharactersAtEnd("0").trimCharactersAtEnd(".") << "  ";
            info = "Captured at  " + info.trim();
        }
        else
            info = "Capture settings unknown: knobs are relative to where they were when it was loaded";
        amp.captured.setText(info, juce::dontSendNotification);
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
                                     p.setLibraryDir(fc.getResult()), cab.refresh();
                             });
    }

    void showView()
    {
        const bool tuning = tunerButton.getToggleState();
        amp.setVisible(!tuning && ampTab.getToggleState());
        cab.setVisible(!tuning && cabTab.getToggleState());
        tuner.setVisible(tuning);
    }

    void timerCallback() override
    {
        if (captureIndex() != shownCapture)
            refreshCapture();
        const bool missing = !p.getLibraryDir().getChildFile(current().file).existsAsFile();
        amp.missing.setText(missing ? "Capture not found: choose the library folder (Library...)" : "", juce::dontSendNotification);

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
            downbeat = p.beatInBar.load() == 0;
        }
        repaint(beatLight);
    }

    MJRAmpProcessor& p;
    Look look;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Label title, inLabel, outLabel;
    juce::OwnedArray<juce::TextButton> channelButtons;
    juce::ComboBox level, voicing;
    juce::TextButton library, resetKnobs, ampTab, cabTab, tunerButton, metroButton;
    AmpView amp;
    CabView cab;
    TunerView tuner;
    Knob input, output;
    Meter inMeter, outMeter;
    std::unique_ptr<juce::FileChooser> chooser;
    int shownCapture = -1, shownBeat = 0;
    juce::uint32 beatLit = 0;
    bool downbeat = false;
    juce::Rectangle<int> beatLight;
    bool updating = false;
};
} // namespace

juce::AudioProcessorEditor* createMJREditor(MJRAmpProcessor& p) { return new Editor(p); }
