#pragma once
#include <JuceHeader.h>
#include "LoopEngine.h"
#include "TelemetryParameter.h"
#include <atomic>
#include <limits>

class AmphiLoopProcessor final : public juce::AudioProcessor, private juce::Timer
{
  public:
    AmphiLoopProcessor();
    ~AmphiLoopProcessor() override;
    void publishTelemetry(); // Message thread only.
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "AmphiLoop"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return std::numeric_limits<double>::infinity(); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorValueTreeState parameters;
    juce::AudioParameterBool* inputThru = nullptr;
    std::atomic<int> state{0}, error{0}, loopSamples{0}, correction{0};
    std::atomic<double> calibrationSampleRate{0};
    std::atomic<bool> queued{false};
    std::atomic<float> loopBeats{0}, position{0}, memoryMB{0};
    void uiGate(int index, bool down);
    static constexpr const char* gates[] = {"pedal", "play", "stop", "clear"};

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    amphi::Engine engine;
    TelemetryParameter *stateMeter = nullptr, *beatsMeter = nullptr, *tempoMeter = nullptr;
    std::atomic<unsigned> telemetrySequence{0};
    std::atomic<float> loopTempo{0};
    void timerCallback() override { publishTelemetry(); }
    std::array<std::atomic<float>*, 4> gateValues{};
    std::atomic<float>* offsetValue = nullptr;
    std::array<bool, 4> paramHeld{};
    std::atomic<bool> resetRequested{false};
    void handleGate(int, bool) noexcept;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmphiLoopProcessor)
};
