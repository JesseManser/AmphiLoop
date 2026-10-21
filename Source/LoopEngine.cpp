#include "LoopEngine.h"
#include <limits>
#include <stdexcept>

namespace amphi
{
static double wrap(double x, double n) noexcept
{
    if (n <= 0)
        return 0;
    x -= std::floor(x / n) * n;
    return x >= n ? 0 : x;
}

void Engine::prepare(double sampleRate, double seconds)
{
    if (!std::isfinite(sampleRate) || sampleRate < 1000 || sampleRate > 384000 || !std::isfinite(seconds) ||
        seconds <= 0 || seconds > 120)
        throw std::invalid_argument("Invalid looper capacity/sample rate");
    sr = sampleRate;
    capacity = static_cast<int>(std::ceil(sr * seconds));
    const auto pages = static_cast<std::size_t>((capacity + pageSize - 1) / pageSize);
    for (auto& bank : audio)
        bank.assign(pages * pageSize * 2, 0.0f);
    active.assign(pages, 0);
    snapshot.assign(pages, 0);
    touched.assign(pages, 0);
    reset();
}

void Engine::clear() noexcept
{
    length = 0;
    loopBeats = 0;
    captured = 0;
    captureLimit = -1;
    recordTail = dubTail = undoReady = false;
    scheduled = false;
    mode = State::stopped;
    freePhase = 0;
    error = Fault::none;
}

void Engine::reset() noexcept
{
    clear();
    now = 0;
    held = holdFired = false;
    lastPress = -1000000000;
    haveExpected = previousRunning = false;
}

void Engine::beginBlock(const Clock& c, int n, int correctionSamples) noexcept
{
    wantedCorrection = std::clamp(correctionSamples, 0, static_cast<int>(sr));
    clockValid = c.valid && std::isfinite(c.ppq) && std::isfinite(c.bpm) && c.bpm > 0;
    running = clockValid && c.running;
    currentBpm = clockValid ? c.bpm : 120.0;
    step = currentBpm / (60.0 * sr);
    const bool jumped = running && previousRunning && haveExpected &&
                        std::abs(c.ppq - expectedBeat) > std::max(step * 2.5, 1e-7);
    if (jumped || (previousRunning && !running))
    {
        if (mode == State::recording || recordTail)
            clear();
        if (mode == State::overdubbing || dubTail)
        {
            dubTail = false;
        }
        mode = State::stopped;
        scheduled = false;
        held = false;
        error = Fault::transportJump;
    }
    if (clockValid)
        beat = c.ppq;
    barSize = std::isfinite(c.beatsPerBar) && c.beatsPerBar > 0 ? c.beatsPerBar : 4.0;
    barAnchor = std::isfinite(c.barStart) ? c.barStart : 0.0;
    if ((mode == State::recording || recordTail || mode == State::overdubbing || dubTail) &&
        std::abs(currentBpm - captureBpm) > 1e-5)
    {
        if (mode == State::recording || recordTail)
            clear();
        mode = State::stopped;
        recordTail = dubTail = scheduled = false;
        error = Fault::captureTempoChanged;
    }
    expectedBeat = beat + n * step;
    haveExpected = clockValid;
    previousRunning = running;
}

void Engine::request(Action a) noexcept
{
    if (capacity == 0)
        return;
    if (a == Action::clear || a == Action::undo)
    {
        execute(a);
        return;
    }
    if (running)
    {
        queued = a;
        const auto bars = (beat - barAnchor) / barSize;
        // Within half a sample of a boundary is that boundary, never the following bar.
        targetBeat = barAnchor + std::ceil(bars - step * 0.5 / barSize) * barSize;
        scheduled = true;
    }
    else
        execute(a);
}

void Engine::pedal(bool down) noexcept
{
    if (down == held)
        return;
    held = down;
    if (!down)
    {
        if (deferredTap && !holdFired)
            request(Action::pedal);
        deferredTap = false;
        return;
    }
    downAt = now;
    downState = mode;
    holdFired = false;
    deferredTap = false;
    if (now - lastPress < static_cast<std::int64_t>(sr * 0.35) &&
        (mode == State::playing || mode == State::overdubbing))
    {
        request(Action::stop);
        lastPress = -1000000000;
        holdFired = true;
    }
    else
    {
        lastPress = now;
        if (mode == State::playing || (mode == State::stopped && hasAudio()))
            deferredTap = true;
        else
            request(Action::pedal);
    }
}

void Engine::beginRecording() noexcept
{
    clear();
    error = Fault::none;
    recordStart = now;
    originBeat = beat;
    captureBpm = currentBpm;
    latchedCorrection = wantedCorrection;
    mode = State::recording;
}

void Engine::finishRecording(State next) noexcept
{
    const auto duration = now - recordStart;
    if (duration <= latchedCorrection || duration > capacity)
    {
        clear();
        error = duration > capacity ? Fault::capacity : Fault::offsetTooLarge;
        return;
    }
    length = static_cast<int>(duration);
    captureLimit = length;
    loopBeats = running ? beat - originBeat : length * captureBpm / (60.0 * sr);
    freePhase = 0;
    recordTail = captured < length;
    mode = next;
}

void Engine::beginDub() noexcept
{
    if (length <= 0 || recordTail || dubTail)
        return;
    if (std::abs(currentBpm - captureBpm) > 1e-5)
    {
        error = Fault::dubTempoMismatch;
        return;
    }
    // Page table only: O(capacity/256) bytes, never copies the audio buffer here.
    std::copy(active.begin(), active.end(), snapshot.begin());
    ++epoch;
    undoReady = true;
    dubStart = now;
    dubEnd = -1;
    dubCorrection = wantedCorrection;
    captureBpm = currentBpm;
    mode = State::overdubbing;
}

void Engine::endDub() noexcept
{
    dubEnd = now;
    dubTail = dubCorrection > 0;
}

void Engine::execute(Action a) noexcept
{
    const double musicalBeat = scheduled ? targetBeat : beat;
    scheduled = false;
    if (a == Action::clear)
    {
        clear();
        return;
    }
    if (a == Action::undo)
    {
        if (!undoReady)
            return;
        dubTail = false;
        if (mode == State::overdubbing)
            mode = State::playing;
        active.swap(snapshot);
        return;
    }
    if (a == Action::pedal)
    {
        if (!hasAudio() && mode != State::recording)
        {
            beginRecording();
            originBeat = musicalBeat;
            return;
        }
        if (mode == State::playing)
        {
            beginDub();
            return;
        }
        a = Action::play;
    }
    if (mode == State::recording)
    {
        finishRecording(a == Action::stop ? State::stopped : State::playing);
        if (length > 0 && running)
            loopBeats = musicalBeat - originBeat;
        return;
    }
    if (mode == State::overdubbing)
        endDub();
    if (a == Action::stop)
    {
        mode = State::stopped;
        return;
    }
    if (a == Action::play && length > 0)
    {
        if (mode == State::stopped)
        {
            originBeat = musicalBeat;
            freePhase = 0;
        }
        mode = State::playing;
    }
}

void Engine::write(int i, float l, float r, bool add) noexcept
{
    const auto page = static_cast<std::size_t>(i / pageSize);
    if (add && touched[page] != epoch)
    {
        const auto base = page * pageSize * 2;
        auto& source = audio[active[page]];
        auto& dest = audio[1 - active[page]];
        std::copy_n(source.data() + base, pageSize * 2, dest.data() + base);
        active[page] = static_cast<std::uint8_t>(1 - active[page]);
        touched[page] = epoch;
    }
    auto& bank = audio[active[page]];
    const auto index = static_cast<std::size_t>(i) * 2;
    if (add)
    {
        bank[index] += l;
        bank[index + 1] += r;
    }
    else
    {
        bank[index] = l;
        bank[index + 1] = r;
    }
}

double Engine::phase() const noexcept
{
    if (length <= 0)
        return 0;
    if (running && loopBeats > 0)
        return wrap((beat - originBeat) * length / loopBeats, length);
    return wrap(freePhase, length);
}

std::array<float, 2> Engine::read(double position) const noexcept
{
    const int i = static_cast<int>(position);
    const int j = i + 1 == length ? 0 : i + 1;
    const auto f = static_cast<float>(position - i);
    const auto& a = audio[active[static_cast<std::size_t>(i / pageSize)]];
    const auto& b = audio[active[static_cast<std::size_t>(j / pageSize)]];
    // Never expose bytes left by a previous recording during compensated tail capture.
    std::array<float, 2> out{};
    for (int c = 0; c < 2; ++c)
    {
        const float x = recordTail && i >= captured ? 0.0f : a[static_cast<std::size_t>(i) * 2 + c];
        const float y = recordTail && j >= captured ? 0.0f : b[static_cast<std::size_t>(j) * 2 + c];
        out[static_cast<std::size_t>(c)] = x + f * (y - x);
    }
    return out;
}

std::array<float, 2> Engine::tick(float l, float r) noexcept
{
    if (!std::isfinite(l))
        l = 0;
    if (!std::isfinite(r))
        r = 0;
    if (held && deferredTap && !holdFired && now - downAt >= static_cast<std::int64_t>(2 * sr))
    {
        holdFired = true;
        if (downState == State::playing)
            execute(Action::undo);
        else if (downState == State::stopped)
            execute(Action::clear);
    }
    if (scheduled && beat + step * 0.5 >= targetBeat)
        execute(queued);
    if (mode == State::recording && now - recordStart >= capacity)
    {
        finishRecording(State::playing);
        error = Fault::capacity;
    }
    if (mode == State::recording || recordTail)
    {
        const auto index = now - recordStart - latchedCorrection;
        if (index >= 0 && index < capacity && (captureLimit < 0 || index < captureLimit))
        {
            write(static_cast<int>(index), l, r, false);
            captured = static_cast<int>(index) + 1;
        }
        if (recordTail && captured >= length)
            recordTail = false;
    }
    const auto pos = phase();
    std::array<float, 2> output{};
    if ((mode == State::playing || mode == State::overdubbing) && length > 0)
        output = read(pos);
    if ((mode == State::overdubbing || dubTail) && length > 0)
    {
        const auto logicalTime = now - dubCorrection;
        if (logicalTime >= dubStart && (dubEnd < 0 || logicalTime < dubEnd))
        {
            const auto rate = running && loopBeats > 0 ? step * length / loopBeats : 1.0;
            const int writeAt =
                static_cast<int>(std::llround(wrap(pos - dubCorrection * rate, length))) % length;
            write(writeAt, l, r, true);
        }
        if (dubTail && logicalTime >= dubEnd)
            dubTail = false;
    }
    // Monitoring is independent of capture: off means loop playback only.
    if (inputThru)
    {
        output[0] += l;
        output[1] += r;
    }
    if (!running && (mode == State::playing || mode == State::overdubbing || dubTail))
        freePhase += 1;
    beat += step;
    ++now;
    return output;
}

double Engine::progress() const noexcept
{
    return length > 0 ? phase() / length : 0;
}

std::size_t Engine::ramBytes() const noexcept
{
    return (audio[0].size() + audio[1].size()) * sizeof(float) + active.size() + snapshot.size() +
           touched.size() * sizeof(std::uint64_t);
}

}
