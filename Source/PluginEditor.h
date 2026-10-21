#pragma once
#include "PluginProcessor.h"

class AmphiLoopLookAndFeel final : public juce::LookAndFeel_V4
{
  public:
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawLinearSlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider::SliderStyle,
                          juce::Slider&) override;
};
class AmphiLoopEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    explicit AmphiLoopEditor(AmphiLoopProcessor&);
    ~AmphiLoopEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

  private:
    AmphiLoopLookAndFeel lookAndFeel;
    AmphiLoopProcessor& processor;
    std::unique_ptr<juce::Drawable> wordmark;
    std::array<juce::TextButton, 4> buttons;
    juce::TextButton setup{"SETUP"};
    juce::TextButton inputThru{"INPUT THRU"};
    std::unique_ptr<juce::ButtonParameterAttachment> inputThruAttachment;
    juce::Slider offset;
    juce::Label offsetLabel;
    bool expanded = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> offsetAttachment;
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmphiLoopEditor)
};
