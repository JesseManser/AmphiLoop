#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace amphi
{
enum class State
{
    stopped,
    recording,
    playing,
    overdubbing
};
enum class Action
{
    pedal,
    play,
    stop,
    clear,
    undo
};
enum class Fault
{
    none,
    capacity,
    offsetTooLarge,
    captureTempoChanged,
    transportJump,
    dubTempoMismatch
};
struct Clock
{
    double ppq = 0.0, bpm = 120.0, barStart = 0.0, beatsPerBar = 4.0;
    bool running = false, valid = false;
};

// One audio-thread owner. prepare() is the only allocation path.
// Audio is never serialised. Two physical copies of each page support lazy undo.
class Engine
{
  public:
    static constexpr int pageSize = 256;
    void prepare(double sampleRate, double seconds = 60.0);
    void beginBlock(const Clock&, int numSamples, int correctionSamples) noexcept;
    void setInputThru(bool enabled) noexcept { inputThru = enabled; }
    void request(Action) noexcept;
    void pedal(bool down) noexcept;
    std::array<float, 2> tick(float left, float right) noexcept;
    State state() const noexcept { return mode; }
    Fault fault() const noexcept { return error; }
    bool pending() const noexcept { return scheduled; }
    bool hasAudio() const noexcept { return length > 0; }
    int samples() const noexcept { return length; }
    double beats() const noexcept { return loopBeats; }
    double tempo() const noexcept { return captureBpm; }
    double progress() const noexcept;
    int correction() const noexcept { return latchedCorrection; }
    std::size_t ramBytes() const noexcept;
    void reset() noexcept;

  private:
    std::array<std::vector<float>, 2> audio;
    std::vector<std::uint8_t> active, snapshot;
    std::vector<std::uint64_t> touched;
    double sr = 48000.0, beat = 0, step = 0, barAnchor = 0, barSize = 4;
    double originBeat = 0, loopBeats = 0, captureBpm = 120, currentBpm = 120;
    double freePhase = 0, targetBeat = 0, expectedBeat = 0;
    std::int64_t now = 0, recordStart = 0, dubStart = 0, dubEnd = -1;
    std::int64_t lastPress = -1000000000, downAt = 0;
    int capacity = 0, length = 0, captureLimit = -1, captured = 0;
    int wantedCorrection = 0, latchedCorrection = 0, dubCorrection = 0;
    bool running = false, clockValid = false, previousRunning = false, haveExpected = false;
    bool inputThru = false;
    bool recordTail = false, dubTail = false, undoReady = false;
    bool held = false, holdFired = false, deferredTap = false, scheduled = false;
    State mode = State::stopped, downState = State::stopped;
    Fault error = Fault::none;
    Action queued = Action::stop;
    std::uint64_t epoch = 1;
    void execute(Action) noexcept;
    void beginRecording() noexcept;
    void finishRecording(State next) noexcept;
    void beginDub() noexcept;
    void endDub() noexcept;
    void clear() noexcept;
    void write(int index, float l, float r, bool add) noexcept;
    std::array<float, 2> read(double position) const noexcept;
    double phase() const noexcept;
};
}
