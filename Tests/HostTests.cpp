#include <juce_audio_processors/juce_audio_processors.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <iostream>
#include <stdexcept>

using namespace juce;
static void check(bool value, const char* text)
{
    if (!value)
        throw std::runtime_error(text);
}

static AudioProcessorParameter* parameter(AudioPluginInstance& p, const String& name)
{
    for (auto* a : p.getParameters())
        if (a->getName(128) == name)
            return a;
    throw std::runtime_error(("Parameter missing: " + name).toStdString());
}

struct TailVisitor : ExtensionsVisitor
{
    void visitVST3Client(const VST3Client& client) override
    {
        void* ptr = nullptr;
        check(client.getIComponentPtr()->queryInterface(Steinberg::Vst::IAudioProcessor_iid, &ptr) ==
                  Steinberg::kResultOk,
              "VST3 processor interface");
        auto* processor = static_cast<Steinberg::Vst::IAudioProcessor*>(ptr);
        check(processor->getTailSamples() == Steinberg::Vst::kInfiniteTail, "infinite looping tail");
        processor->release();
    }
};
struct Playhead : AudioPlayHead
{
    int64_t sample = 0;
    double sampleRate = 48000, bpm = 123;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setTimeInSamples(sample);
        p.setTimeInSeconds(sample / sampleRate);
        p.setPpqPosition(sample * bpm / (60 * sampleRate));
        p.setBpm(bpm);
        p.setTimeSignature(TimeSignature{4, 4});
        p.setPpqPositionOfLastBarStart(std::floor(*p.getPpqPosition() / 4) * 4);
        p.setIsPlaying(true);
        return p;
    }
};
int main(int argc, char** argv)
{
    ScopedJuceInitialiser_GUI initialise;
    try
    {
        check(argc == 2, "Supply VST3 path");
        VST3PluginFormat format;
        OwnedArray<PluginDescription> descriptions;
        format.findAllTypesForFile(descriptions, String::fromUTF8(argv[1]));
        check(descriptions.size() == 1, "scan exactly one plugin");
        String error;
        auto plugin = format.createInstanceFromDescription(*descriptions[0], 48000, 64, error);
        if (!plugin)
            throw std::runtime_error(error.toStdString());
        plugin->setRateAndBufferSizeDetails(48000, 64);
        plugin->prepareToPlay(48000, 64);
        check(plugin->getLatencySamples() == 0, "VST3 latency report");
        for (auto* control : plugin->getParameters())
        {
            const auto name = control->getName(128);
            check(StringArray{"Pedal", "Play", "Stop", "Clear", "Record offset (samples)", "Input Thru",
                              "AmphiLoop State", "AmphiLoop Beats", "AmphiLoop Tempo", "Bypass"}
                      .contains(name),
                  "unexpected exported parameter");
        }
        check(!plugin->acceptsMidi(), "no plugin MIDI input");
        for (const auto* name : {"Pedal", "Play", "Stop", "Clear", "Record offset (samples)", "Input Thru"})
            check(parameter(*plugin, name)->isAutomatable(), "public control host mappable");
        for (const auto* name : {"AmphiLoop State", "AmphiLoop Beats", "AmphiLoop Tempo"})
        {
            auto* feedback = parameter(*plugin, name);
            check(feedback->isAutomatable() &&
                      feedback->getCategory() == AudioProcessorParameter::genericParameter,
                  "host-visible feedback metadata");
        }
        TailVisitor tail;
        plugin->getExtensions(tail);
        parameter(*plugin, "Record offset (samples)")->setValueNotifyingHost(137.0f / 48000.0f);
        MemoryBlock preset;
        plugin->getStateInformation(preset);
        check(preset.getSize() < 4096, "preset excludes audio");
        plugin->releaseResources();
        plugin.reset();
        auto reload = format.createInstanceFromDescription(*descriptions[0], 48000, 64, error);
        check(reload != nullptr, "reload");
        reload->setStateInformation(preset.getData(), static_cast<int>(preset.getSize()));
        check(parameter(*reload, "Record offset (samples)")->getCurrentValueAsText().getIntValue() == 137,
              "VST3 calibration persistence");
        reload->releaseResources();
        reload.reset();
        // Check host notifications and recovery from writes to feedback parameters.
        {
            auto p = format.createInstanceFromDescription(*descriptions[0], 48000, 480, error);
            check(p != nullptr, "feedback instance load");
            p->prepareToPlay(48000, 480);
            auto* state = parameter(*p, "AmphiLoop State");
            auto* beats = parameter(*p, "AmphiLoop Beats");
            auto* tempo = parameter(*p, "AmphiLoop Tempo");
            AudioBuffer<float> b(2, 480);
            MidiBuffer m;
            const auto render = [&]
            {
                b.clear();
                p->processBlock(b, m);
            };
            const auto expect = [&](int expectedState, float expectedBeats, float expectedTempo)
            {
                MessageManager::getInstance()->runDispatchLoopUntil(100);
                check(state->getCurrentValueAsText().getIntValue() == expectedState,
                      "host state notification");
                check(std::abs(beats->getCurrentValueAsText().getFloatValue() - expectedBeats) < 0.00001f,
                      "host physical beat length");
                check(std::abs(tempo->getCurrentValueAsText().getFloatValue() - expectedTempo) < 0.001f,
                      "host captured tempo");
            };
            expect(0, 0, 0);
            parameter(*p, "Pedal")->setValueNotifyingHost(1);
            render();
            expect(1, 0, 120);
            parameter(*p, "Pedal")->setValueNotifyingHost(0);
            for (int i = 1; i < 200; ++i)
                render();
            parameter(*p, "Play")->setValueNotifyingHost(1);
            render();
            expect(2, 4, 120);
            parameter(*p, "Play")->setValueNotifyingHost(0);
            for (auto* feedback : {state, beats, tempo})
                feedback->setValueNotifyingHost(0);
            expect(2, 4, 120); // Cache repairs without any new audio/state transition.
            parameter(*p, "Pedal")->setValueNotifyingHost(1);
            render();
            parameter(*p, "Pedal")->setValueNotifyingHost(0);
            render();
            expect(3, 4, 120);
            parameter(*p, "Play")->setValueNotifyingHost(1);
            render();
            expect(2, 4, 120);
            parameter(*p, "Play")->setValueNotifyingHost(0);
            parameter(*p, "Stop")->setValueNotifyingHost(1);
            render();
            expect(0, 4, 120);
            parameter(*p, "Stop")->setValueNotifyingHost(0);
            parameter(*p, "Play")->setValueNotifyingHost(1);
            render();
            expect(2, 4, 120);
            parameter(*p, "Clear")->setValueNotifyingHost(1);
            render();
            expect(0, 0, 0);
            p->releaseResources();
            std::cout
                << "PASS host feedback: empty, record, play, overdub, stop, restart, clear, physical units and rejected-write recovery\n";
        }
        // Check Input Thru through the VST3 parameter interface.
        {
            auto monitor = format.createInstanceFromDescription(*descriptions[0], 48000, 64, error);
            check(monitor != nullptr, "monitor test load");
            monitor->prepareToPlay(48000, 64);
            auto* thru = parameter(*monitor, "Input Thru");
            check(thru->getValue() == 0, "VST3 input thru default off");
            AudioBuffer<float> b(2, 64);
            MidiBuffer m;
            for (bool enabled : {false, true, false})
            {
                thru->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
                for (int i = 0; i < 64; ++i)
                {
                    b.setSample(0, i, 0.25f);
                    b.setSample(1, i, -0.5f);
                }
                monitor->processBlock(b, m);
                check(std::abs(b.getSample(0, 0) - (enabled ? 0.25f : 0)) < 0.001f, "VST3 thru left");
                check(std::abs(b.getSample(1, 0) - (enabled ? -0.5f : 0)) < 0.001f, "VST3 thru right");
            }
            monitor->releaseResources();
        }
        // Compare corrected and uncorrected playback of a delayed impulse.
        for (double sampleRate : {44100.0, 48000.0, 96000.0})
            for (int correction : {0, 2048})
            {
                auto p = format.createInstanceFromDescription(*descriptions[0], sampleRate, 1024, error);
                check(p != nullptr, "2048 test load");
                Playhead head;
                head.sampleRate = sampleRate;
                head.bpm = 120;
                p->setPlayHead(&head);
                p->prepareToPlay(sampleRate, 1024);
                parameter(*p, "Record offset (samples)")->setValueNotifyingHost(correction / 48000.0f);
                check(parameter(*p, "Record offset (samples)")->getCurrentValueAsText().getIntValue() ==
                          correction,
                      "manual2048 host value");
                const int period = static_cast<int>(sampleRate * 2), delay = 2048;
                AudioBuffer<float> b(2, 1024);
                MidiBuffer m;
                int first = -1, second = -1;
                for (int t = 0; t < 2 * period + delay + 1024; t += 1024)
                {
                    head.sample = t;
                    b.clear();
                    m.clear();
                    parameter(*p, "Pedal")->setValueNotifyingHost(t == 0 ? 1.0f : 0.0f);
                    if (delay >= t && delay < t + 1024)
                        b.setSample(0, delay - t, 1);
                    const int close = period - 4096;
                    parameter(*p, "Play")
                        ->setValueNotifyingHost(close >= t && close < t + 1024 ? 1.0f : 0.0f);
                    p->processBlock(b, m);
                    for (int i = 0; i < 1024; ++i)
                        if (b.getSample(0, i) > 0.99f)
                        {
                            if (first < 0)
                                first = t + i;
                            else if (second < 0)
                                second = t + i;
                        }
                }
                check(first == period + delay - correction, "manual2048 first replay sample");
                check(second == 2 * period + delay - correction, "manual2048 repeat sample");
                std::cout << "PASS manual correction: rate=" << sampleRate
                          << ", block=1024, offset=" << correction << ", first residual=" << first - period
                          << ", repeat residual=" << second - 2 * period << " samples\n";
                p->releaseResources();
                p->setPlayHead(nullptr);
            }
        // Check bar timing at a fractional tempo across buffer sizes.
        for (int blockSize : {32, 64, 128, 256, 512})
        {
            auto p = format.createInstanceFromDescription(*descriptions[0], 48000, blockSize, error);
            check(p != nullptr, "reload VST3");
            Playhead head;
            p->setPlayHead(&head);
            p->prepareToPlay(48000, blockSize);
            AudioBuffer<float> b(2, blockSize);
            MidiBuffer m;
            int end = static_cast<int>(std::llround(48000 * 240.0 / 123));
            float peak = 0;
            bool started = false;
            for (int t = 0; t < end + 1024; t += blockSize)
            {
                head.sample = t;
                b.clear();
                m.clear();
                parameter(*p, "Pedal")->setValueNotifyingHost(t == 0 ? 1.0f : 0.0f);
                if (t == 0)
                {
                    b.setSample(0, 0, 1);
                    started = true;
                }
                parameter(*p, "Play")
                    ->setValueNotifyingHost(end - 2048 >= t && end - 2048 < t + blockSize ? 1.0f : 0.0f);
                p->processBlock(b, m);
                for (int i = 0; i < blockSize; ++i)
                    if (std::abs(t + i - end) <= 1)
                        peak = std::max(peak, b.getSample(0, i));
            }
            check(started && peak > 0.49f, "VST3 host-bar timing across block sizes");
            p->releaseResources();
            p->setPlayHead(nullptr);
        }
        std::cout << "PASS VST3 host tests\n";
    }
    catch (const std::exception& ex)
    {
        std::cerr << "FAIL VST3 host tests: " << ex.what() << '\n';
        return 1;
    }
}
