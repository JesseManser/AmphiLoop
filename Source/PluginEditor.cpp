#include "PluginEditor.h"
#include <BinaryData.h>

using namespace juce;
namespace
{
const Colour background{0xff040405}, panel{0xff1b1c1f}, border{0xff4e5056};
const Colour blue{0xff3775b0}, red{0xffbe3136}, green{0xff2f8b5c};
const Colour secondaryText{0xffbec0c6}, controlBorder{0xffdcdee2};
}

void AmphiLoopLookAndFeel::drawButtonBackground(Graphics& g, Button& button, const Colour&, bool highlighted,
                                                bool down)
{
    auto fill = button.findColour(TextButton::buttonColourId);
    if (highlighted)
        fill = fill.brighter(0.08f);
    if (down)
        fill = fill.brighter(0.18f);
    g.setColour(fill);
    g.fillRect(button.getLocalBounds());
    g.setColour(button.findColour(TextButton::buttonOnColourId));
    g.drawRect(button.getLocalBounds().toFloat(), 1.0f);
}

void AmphiLoopLookAndFeel::drawButtonText(Graphics& g, TextButton& button, bool, bool)
{
    g.setColour(Colours::white);
    g.setFont(FontOptions(14.0f, Font::bold));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(8), Justification::centred, 1);
}

void AmphiLoopLookAndFeel::drawLinearSlider(Graphics& g, int x, int y, int width, int height, float sliderPos,
                                            float, float, Slider::SliderStyle, Slider&)
{
    const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    g.setColour(panel);
    g.fillRect(static_cast<float>(x), centreY - 3.0f, static_cast<float>(width), 6.0f);
    g.setColour(blue);
    g.fillRect(static_cast<float>(x), centreY - 3.0f, jmax(0.0f, sliderPos - static_cast<float>(x)), 6.0f);
    g.setColour(Colours::white);
    g.fillRect(sliderPos - 4.0f, centreY - 9.0f, 8.0f, 18.0f);
}

AmphiLoopEditor::AmphiLoopEditor(AmphiLoopProcessor& p) : AudioProcessorEditor(p), processor(p)
{
    setLookAndFeel(&lookAndFeel);
    wordmark = Drawable::createFromImageData(BinaryData::AmphiLoopWordmark_svg,
                                             BinaryData::AmphiLoopWordmark_svgSize);
    jassert(wordmark != nullptr);
    const StringArray names{"RECORD / DUB", "PLAY", "STOP", "CLEAR"};
    for (int i = 0; i < 4; ++i)
    {
        auto& b = buttons[static_cast<std::size_t>(i)];
        b.setButtonText(names[i]);
        b.setColour(TextButton::buttonColourId, panel);
        b.setColour(TextButton::buttonOnColourId, border);
        b.setColour(TextButton::textColourOffId, Colours::white);
        addAndMakeVisible(b);
        b.onStateChange = [this, i] { processor.uiGate(i, buttons[static_cast<std::size_t>(i)].isDown()); };
    }
    addAndMakeVisible(inputThru);
    inputThru.setClickingTogglesState(true);
    for (auto* button : {&inputThru, &setup})
    {
        button->setColour(TextButton::buttonColourId, panel);
        button->setColour(TextButton::buttonOnColourId, controlBorder);
        button->setColour(TextButton::textColourOffId, Colours::white);
    }
    inputThru.setTooltip("Hear live input alongside loop playback. Recording continues when off.");
    inputThruAttachment = std::make_unique<ButtonParameterAttachment>(*p.inputThru, inputThru, nullptr);
    addAndMakeVisible(setup);
    setup.onClick = [this]
    {
        expanded = !expanded;
        setSize(620, expanded ? 530 : 340);
        resized();
        timerCallback();
    };
    addChildComponent(offset);
    offsetLabel.setText("Recording correction (samples)", dontSendNotification);
    addChildComponent(offsetLabel);
    offset.setSliderStyle(Slider::LinearHorizontal);
    offset.setTextBoxStyle(Slider::TextBoxRight, false, 85, 25);
    offset.setColour(Slider::textBoxTextColourId, Colours::white);
    offset.setColour(Slider::textBoxBackgroundColourId, panel);
    offset.setColour(Slider::textBoxOutlineColourId, border);
    offsetLabel.setColour(Label::textColourId, secondaryText);
    offsetAttachment =
        std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(p.parameters, "offset", offset);
    setSize(620, 340);
    timerCallback();
    startTimerHz(15);
}

AmphiLoopEditor::~AmphiLoopEditor()
{
    stopTimer();
    for (int i = 0; i < 4; ++i)
        processor.uiGate(i, false);
    setLookAndFeel(nullptr);
}

void AmphiLoopEditor::timerCallback()
{
    const int mode = processor.state.load();
    buttons[0].setColour(TextButton::buttonColourId, mode == 1 || mode == 3 ? red : panel);
    buttons[1].setColour(TextButton::buttonColourId, mode == 2 || mode == 3 ? blue : panel);
    inputThru.setColour(TextButton::buttonColourId, inputThru.getToggleState() ? green : panel);
    setup.setColour(TextButton::buttonColourId, expanded ? blue : panel);
    repaint();
}

void AmphiLoopEditor::resized()
{
    buttons[0].setBounds(28, 170, 230, 62);
    buttons[1].setBounds(270, 170, 98, 62);
    buttons[2].setBounds(380, 170, 98, 62);
    buttons[3].setBounds(490, 170, 102, 62);
    inputThru.setBounds(28, 295, 150, 28);
    setup.setBounds(508, 295, 84, 28);
    for (Component* c : std::array<Component*, 2>{&offset, &offsetLabel})
        c->setVisible(expanded);
    offsetLabel.setBounds(28, 346, 350, 25);
    offset.setBounds(28, 376, 558, 32);
}

void AmphiLoopEditor::paint(Graphics& g)
{
    g.fillAll(background);
    Path infinity;
    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i <= 128; ++i)
    {
        const auto t = 2 * pi * i / 128;
        const auto x = static_cast<float>(65 + 34 * std::cos(t));
        const auto y = static_cast<float>(53 + 18 * std::sin(2 * t));
        if (i == 0)
            infinity.startNewSubPath(x, y);
        else
            infinity.lineTo(x, y);
    }
    g.setColour(red);
    g.strokePath(infinity, PathStrokeType(3.0f));
    if (wordmark)
        wordmark->drawWithin(g, Rectangle<float>(114, 40, 350, 23),
                             RectanglePlacement::xLeft | RectanglePlacement::yMid, 1.0f);
    g.setFont(FontOptions(12.0f));
    g.setColour(secondaryText);
    g.drawText("0.3.3  /  ALPHA  /  RAM ONLY", 116, 70, 390, 20, Justification::left);
    const StringArray modes{"STOPPED", "RECORDING", "PLAYING", "OVERDUBBING"};
    const auto mode = jlimit(0, 3, processor.state.load());
    g.setColour(mode == 1 || mode == 3 ? red : Colours::white);
    g.setFont(FontOptions(18.0f));
    String info = processor.loopSamples.load() == 0 && mode != 1 ? "EMPTY" : modes[mode];
    if (processor.queued.load())
        info += "  /  NEXT BAR";
    g.drawText(info, 28, 105, 370, 28, Justification::left);
    g.setFont(FontOptions(13.0f));
    g.drawText(String(processor.loopBeats.load(), 2) + " beats", 420, 105, 172, 28, Justification::right);
    g.setColour(border);
    g.fillRect(28, 145, 564, 3);
    g.setColour(mode == 1 || mode == 3 ? red : blue);
    g.fillRect(28.0f, 145.0f, 564 * processor.position.load(), 3.0f);
    const double rate = processor.calibrationSampleRate.load();
    const int manual = static_cast<int>(processor.parameters.getRawParameterValue("offset")->load());
    const int effective = rate > 0 ? jmin(manual, static_cast<int>(rate)) : manual;
    const auto describe = [rate](int samples)
    {
        return String(samples) + " samples" +
               (rate > 0 ? " / " + String(samples * 1000.0 / rate, 2) + " ms" : String());
    };
    g.setFont(FontOptions(12.0f));
    g.setColour(secondaryText);
    g.drawText("MANUAL CALIBRATION | RAM " + String(processor.memoryMB.load(), 1) + " MB", 28, 244, 564, 18,
               Justification::left);
    const StringArray errors{"",
                             "Capacity reached",
                             "Loop shorter than correction",
                             "Tempo changed during capture: stopped",
                             "Transport changed: stopped",
                             "Return to recorded tempo before overdubbing"};
    const int fault = jlimit(0, 5, processor.error.load());
    const bool hasTake = processor.loopSamples.load() > 0 || mode == 1;
    g.setColour(fault ? red : secondaryText);
    g.drawText(fault     ? errors[fault]
               : hasTake ? "Take correction: " + describe(processor.correction.load())
                         : "Next take: " + describe(effective),
               28, 267, 564, 18, Justification::left);
    if (expanded)
    {
        g.setColour(Colours::white);
        g.drawText("Next recording / overdub: " + describe(effective), 28, 420, 564, 18, Justification::left);
        g.setColour(secondaryText);
        g.drawText(rate > 0 ? "Sample rate: " + String(rate, 0) + " Hz" +
                                  (effective != manual ? " | Correction capped to one second" : String())
                            : "Sample rate available when audio starts",
                   28, 442, 564, 18, Justification::left);
        g.drawText("Late playback: increase correction. Early playback: decrease it.", 28, 466, 564, 18,
                   Justification::left);
        g.drawText("Clear and record again to test. Changing this does not move existing audio.", 28, 488,
                   564, 18, Justification::left);
        g.drawText("Saved per instance. Loop audio stays in RAM only.", 28, 510, 564, 18,
                   Justification::left);
    }
}
