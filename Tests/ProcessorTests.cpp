#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>
#include <stdexcept>

using namespace juce;
static void require(bool yes, const char* message)
{
    if (!yes)
        throw std::runtime_error(message);
}

static void set(AmphiLoopProcessor& p, const char* id, float value)
{
    auto* a = p.parameters.getParameter(id);
    a->setValueNotifyingHost(a->convertTo0to1(value));
}

int main(int argc, char** argv)
{
    ScopedJuceInitialiser_GUI initialise;
    try
    {
        AmphiLoopProcessor p;
        p.prepareToPlay(48000, 64);
        require(p.getParameters().size() == 9, "six controls plus three feedback parameters");
        require(p.getParameters()[5] == p.inputThru, "existing control indices preserved");
        for (int i = 6; i < 9; ++i)
            require(p.getParameters()[i]->isAutomatable() &&
                        p.getParameters()[i]->getCategory() == AudioProcessorParameter::genericParameter,
                    "host-visible generic feedback");
        require(!p.acceptsMidi(), "no direct MIDI routing");
        require(!p.inputThru->get(), "input thru defaults off");
        require(p.getLatencySamples() == 0, "zero added latency");
        AudioBuffer<float> b(2, 64);
        MidiBuffer m;
        set(p, "offset", 17);
        set(p, "pedal", 1);
        b.clear();
        b.setSample(0, 17, 1);
        b.setSample(1, 17, 0.5f);
        p.processBlock(b, m);
        require(b.getMagnitude(0, 64) == 0, "recording with thru off is silent");
        set(p, "pedal", 0);
        set(p, "offset", 99);
        for (int i = 0; i < 3; ++i)
        {
            b.clear();
            p.processBlock(b, m);
        }
        set(p, "play", 1);
        b.clear();
        p.processBlock(b, m);
        require(p.loopSamples.load() == 256, "recording closes at block boundary");
        require(p.correction.load() == 17, "correction latched despite next-take edit");
        require(std::abs(b.getSample(0, 0) - 1) < 0.001f && std::abs(b.getSample(1, 0) - 0.5f) < 0.001f,
                "manual correction and stereo playback");
        p.publishTelemetry();
        require(p.getParameters()[6]->getCurrentValueAsText().getIntValue() == 2, "playing feedback");
        require(std::abs(p.getParameters()[7]->getCurrentValueAsText().getFloatValue() - 256.0f / 24000) <
                    0.00001f,
                "physical beat feedback");
        require(p.getParameters()[8]->getCurrentValueAsText().getFloatValue() == 120,
                "captured tempo feedback");
        p.getParameters()[6]->setValue(0);
        p.publishTelemetry();
        require(p.getParameters()[6]->getCurrentValueAsText().getIntValue() == 2,
                "feedback cannot change recording state");
        p.inputThru->setValueNotifyingHost(1);
        MemoryBlock saved;
        p.getStateInformation(saved);
        require(saved.getSize() < 1024, "preset contains setup only");
        AmphiLoopProcessor restored;
        restored.prepareToPlay(48000, 64);
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        b.clear();
        restored.processBlock(b, m);
        require(restored.loopSamples.load() == 0, "restored audio empty");
        require(restored.parameters.getRawParameterValue("offset")->load() == 99,
                "offset saved per instance");
        require(restored.inputThru->get(), "thru saved");
        AmphiLoopProcessor independent;
        require(independent.parameters.getRawParameterValue("offset")->load() == 0,
                "instance offsets independent");
        XmlElement old("AmphiLoopSetup");
        old.setAttribute("role", 3);
        old.setAttribute("autoOffset", true);
        old.setAttribute("offset", 2048);
        MemoryBlock legacy;
        AudioProcessor::copyXmlToBinary(old, legacy);
        restored.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
        require(restored.parameters.getRawParameterValue("offset")->load() == 2048,
                "legacy manual value retained");
        require(!restored.inputThru->get(), "missing thru defaults off");
        AudioBuffer<float> mono(1, 64);
        for (bool enabled : {false, true, false})
        {
            restored.inputThru->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
            for (int i = 0; i < 64; ++i)
                mono.setSample(0, i, 0.25f);
            restored.processBlock(mono, m);
            require(mono.getSample(0, 0) == (enabled ? 0.25f : 0), "mono monitoring toggle");
        }
        p.inputThru->setValueNotifyingHost(0);
        set(p, "clear", 1);
        b.clear();
        p.processBlock(b, m);
        require(p.loopSamples.load() == 0 && b.getMagnitude(0, 64) == 0, "clear immediate silence");
        p.publishTelemetry();
        for (int i = 6; i < 9; ++i)
            require(p.getParameters()[i]->getCurrentValueAsText().getFloatValue() == 0,
                    "clear resets feedback");
        // MIDI CC messages do not trigger the host-controlled buttons.
        m.addEvent(MidiMessage::controllerEvent(7, 1, 127), 0);
        b.clear();
        p.processBlock(b, m);
        require(p.state.load() == 0 && p.loopSamples.load() == 0, "unmapped MIDI ignored");
        if (argc > 1)
        {
            AmphiLoopProcessor preview;
            preview.prepareToPlay(48000, 64);
            set(preview, "offset", 2048);
            std::unique_ptr<AudioProcessorEditor> editor(preview.createEditor());
            if (argc > 2)
                for (int i = 0; i < editor->getNumChildComponents(); ++i)
                    if (auto* button = dynamic_cast<TextButton*>(editor->getChildComponent(i));
                        button && button->getButtonText() == "SETUP")
                        button->onClick();
            auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
            File file(String::fromUTF8(argv[1]));
            FileOutputStream stream(file);
            PNGImageFormat png;
            require(stream.openedOk() && stream.setPosition(0) && stream.truncate().wasOk(),
                    "preview stream");
            require(png.writeImageToStream(image, stream), "editor preview");
        }
        std::cout << "PASS processor tests\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL processor: " << e.what() << '\n';
        return 1;
    }
}
