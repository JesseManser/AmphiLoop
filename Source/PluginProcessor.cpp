#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;
AudioProcessorValueTreeState::ParameterLayout AmphiLoopProcessor::layout()
{
    AudioProcessorValueTreeState::ParameterLayout p;
    for (int i = 0; i < 4; ++i)
        p.add(std::make_unique<AudioParameterBool>(ParameterID{gates[i], 1},
                                                   StringArray{"Pedal", "Play", "Stop", "Clear"}[i], false));
    p.add(std::make_unique<AudioParameterInt>(ParameterID{"offset", 1}, "Record offset (samples)", 0, 48000,
                                              0));
    return p;
}

AmphiLoopProcessor::AmphiLoopProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", AudioChannelSet::stereo(), true)
                         .withOutput("Output", AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "AmphiLoop", layout())
{
    for (int i = 0; i < 4; ++i)
        gateValues[static_cast<std::size_t>(i)] = parameters.getRawParameterValue(gates[i]);
    offsetValue = parameters.getRawParameterValue("offset");
    addParameter(inputThru = new AudioParameterBool(ParameterID{"inputThru", 1}, "Input Thru", false));
    // Append feedback so all six existing control indices and IDs remain stable.
    addParameter(stateMeter = new TelemetryParameter("amphiState", "AmphiLoop State", 3, 1));
    addParameter(beatsMeter = new TelemetryParameter("amphiBeats", "AmphiLoop Beats", 4096));
    addParameter(tempoMeter = new TelemetryParameter("amphiTempo", "AmphiLoop Tempo", 1000));
    startTimerHz(20);
}

AmphiLoopProcessor::~AmphiLoopProcessor()
{
    stopTimer();
}

void AmphiLoopProcessor::prepareToPlay(double rate, int)
{
    engine.prepare(rate, 60);
    calibrationSampleRate.store(rate);
    setLatencySamples(0);
    paramHeld.fill(false);
    telemetrySequence.fetch_add(1);
    loopTempo.store(0);
    state.store(0);
    error.store(0);
    loopSamples.store(0);
    loopBeats.store(0);
    position.store(0);
    queued.store(false);
    correction.store(0);
    telemetrySequence.fetch_add(1);
    memoryMB.store(static_cast<float>(engine.ramBytes()) / (1024 * 1024));
}

bool AmphiLoopProcessor::isBusesLayoutSupported(const BusesLayout& b) const
{
    const auto channels = b.getMainOutputChannelSet();
    return (channels == AudioChannelSet::mono() || channels == AudioChannelSet::stereo()) &&
           b.getMainInputChannelSet() == channels;
}

void AmphiLoopProcessor::handleGate(int index, bool down) noexcept
{
    auto i = static_cast<std::size_t>(index);
    if (paramHeld[i] == down)
        return;
    paramHeld[i] = down;
    if (index == 0)
        engine.pedal(down);
    else if (down)
        engine.request(index == 1   ? amphi::Action::play
                       : index == 2 ? amphi::Action::stop
                                    : amphi::Action::clear);
}

void AmphiLoopProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals guard;
    if (buffer.getNumChannels() < 1)
        return;
    if (resetRequested.exchange(false))
    {
        engine.reset();
        paramHeld.fill(false);
    }
    amphi::Clock clock;
    if (auto* head = getPlayHead())
        if (auto p = head->getPosition())
        {
            if (p->getPpqPosition() && p->getBpm())
            {
                clock.valid = true;
                clock.running = p->getIsPlaying();
                clock.ppq = *p->getPpqPosition();
                clock.bpm = *p->getBpm();
                if (auto sig = p->getTimeSignature())
                    clock.beatsPerBar = sig->numerator * 4.0 / sig->denominator;
                clock.barStart = p->getPpqPositionOfLastBarStart().orFallback(
                    std::floor(clock.ppq / clock.beatsPerBar) * clock.beatsPerBar);
            }
        }
    const int offset = static_cast<int>(offsetValue->load(std::memory_order_relaxed));
    engine.setInputThru(inputThru->get());
    engine.beginBlock(clock, buffer.getNumSamples(), offset);
    for (int i = 0; i < 4; ++i)
        handleGate(i, gateValues[static_cast<std::size_t>(i)]->load(std::memory_order_relaxed) >= 0.5f);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int s = 0; s < buffer.getNumSamples(); ++s)
    {
        auto out = engine.tick(left[s], right ? right[s] : left[s]);
        left[s] = out[0];
        if (right)
            right[s] = out[1];
    }
    midi.clear();
    telemetrySequence.fetch_add(1);
    state.store(static_cast<int>(engine.state()));
    error.store(static_cast<int>(engine.fault()));
    loopSamples.store(engine.samples());
    loopBeats.store(static_cast<float>(engine.beats()));
    position.store(static_cast<float>(engine.progress()));
    queued.store(engine.pending());
    correction.store(engine.correction());
    loopTempo.store(engine.hasAudio() || engine.state() == amphi::State::recording
                        ? static_cast<float>(engine.tempo())
                        : 0.0f);
    telemetrySequence.fetch_add(1);
}

void AmphiLoopProcessor::publishTelemetry()
{
    // Never wait for audio or send host notifications from the audio callback.
    const auto before = telemetrySequence.load();
    if (before & 1U)
        return;
    const auto currentState = state.load(), samples = loopSamples.load();
    const auto beats = loopBeats.load(), tempo = loopTempo.load();
    if (before != telemetrySequence.load())
        return;
    // Publish length/tempo before state, so state listeners can query a complete snapshot.
    beatsMeter->publish(samples > 0 ? beats : 0.0f);
    tempoMeter->publish(tempo);
    stateMeter->publish(static_cast<float>(currentState));
}

void AmphiLoopProcessor::uiGate(int i, bool down)
{
    if (auto* p = parameters.getParameter(gates[i]))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(down ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
}

void AmphiLoopProcessor::getStateInformation(MemoryBlock& out)
{
    // Save settings without transient button states or recorded audio.
    XmlElement xml("AmphiLoopSetup");
    xml.setAttribute("version", 3);
    xml.setAttribute("offset", static_cast<int>(offsetValue->load()));
    xml.setAttribute("inputThru", inputThru->get());
    copyXmlToBinary(xml, out);
}

void AmphiLoopProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size); xml && xml->hasTagName("AmphiLoopSetup"))
    {
        auto set = [&](const char* id, float value)
        {
            auto* p = parameters.getParameter(id);
            p->setValueNotifyingHost(p->convertTo0to1(value));
        };
        set("offset", static_cast<float>(jlimit(0, 48000, xml->getIntAttribute("offset"))));
        inputThru->setValueNotifyingHost(xml->getBoolAttribute("inputThru", false) ? 1.0f : 0.0f);
        for (auto id : gates)
            set(id, 0);
        resetRequested.store(true);
    }
}

AudioProcessorEditor* AmphiLoopProcessor::createEditor()
{
    return new AmphiLoopEditor(*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AmphiLoopProcessor();
}
