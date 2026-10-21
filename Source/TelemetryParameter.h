#pragma once
#include <JuceHeader.h>
// Ableton Live only lists these meters when they are generic, automatable parameters.
// Ignore host writes and republish the engine value to restore the host's cached value.
class TelemetryParameter final : public juce::RangedAudioParameter
{
  public:
    TelemetryParameter(const char* id, const char* name, float maximum, float interval = 0, float minimum = 0)
        : RangedAudioParameter(
              juce::ParameterID{id, 1}, name,
              juce::AudioProcessorParameterWithIDAttributes().withAutomatable(true).withCategory(
                  genericParameter)),
          range(minimum, maximum, interval)
    {
    }
    float getValue() const override { return value.load(std::memory_order_relaxed); }
    void setValue(float attempted) override
    {
        if (attempted != value.load(std::memory_order_relaxed))
            refreshRequested.store(true);
    }
    float getDefaultValue() const override { return 0; }
    juce::String getText(float normalised, int) const override
    {
        return juce::String(convertFrom0to1(normalised), range.interval > 0 ? 0 : 6);
    }
    float getValueForText(const juce::String& text) const override
    {
        return convertTo0to1(text.getFloatValue());
    }
    const juce::NormalisableRange<float>& getNormalisableRange() const override { return range; }
    bool isDiscrete() const override { return range.interval > 0; }
    void publish(float physicalValue)
    {
        const auto next = convertTo0to1(physicalValue);
        const bool changed = next != value.exchange(next);
        const bool refresh = refreshRequested.exchange(false);
        if (changed || refresh)
            sendValueChangedMessageToListeners(next);
    }

  private:
    juce::NormalisableRange<float> range;
    std::atomic<float> value{0};
    std::atomic<bool> refreshRequested{false};
};
