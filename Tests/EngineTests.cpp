#include "LoopEngine.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

static bool watchAllocations = false;
static std::size_t allocations = 0;
void* operator new(std::size_t n)
{
    if (watchAllocations)
        ++allocations;
    if (auto* p = std::malloc(n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

void* operator new[](std::size_t n)
{
    return ::operator new(n);
}

void operator delete[](void* p) noexcept
{
    ::operator delete(p);
}

void operator delete[](void* p, std::size_t) noexcept
{
    ::operator delete(p);
}

using namespace amphi;
static void check(bool ok, const char* what)
{
    if (!ok)
        throw std::runtime_error(what);
}

static void near(float x, float y, const char* what)
{
    check(std::abs(x - y) < 0.001f, what);
}

static void ticks(Engine& e, int n, float value = 0)
{
    for (int i = 0; i < n; ++i)
        e.tick(value, value);
}

static Clock clockAt(int sample, double rate = 48000, double bpm = 120)
{
    Clock c;
    c.valid = c.running = true;
    c.ppq = sample * bpm / (60 * rate);
    c.bpm = bpm;
    c.barStart = std::floor(c.ppq / 4) * 4;
    return c;
}

static void basicLoop(Engine& e, int n = 1024)
{
    e.prepare(48000, 1);
    e.beginBlock({}, n, 0);
    e.request(Action::pedal);
    ticks(e, n, 0.25f);
    e.request(Action::play);
}

int main()
{
    int groups = 0;
    try
    {
        {
            Engine e;
            basicLoop(e);
            check(e.samples() == 1024, "length");
            for (int i = 0; i < 8192; ++i)
                near(e.tick(0, 0)[0], 0.25f, "basic replay");
            e.request(Action::clear);
            check(!e.hasAudio(), "clear");
            near(e.tick(0, 0)[0], 0, "stale clear");
            ++groups;
        }
        // Incoming impulse delayed by d must play on the intended phase, including across blocks.
        for (int d : {0, 1, 17, 64, 511, 1200})
        {
            Engine e;
            e.prepare(48000, 1);
            const int len = 4096;
            e.beginBlock({}, 64, d);
            e.request(Action::pedal);
            for (int t = 0; t <= len + d; ++t)
            {
                if (t == len)
                    e.request(Action::play);
                if (t % 64 == 0)
                    e.beginBlock({}, 64, d);
                float in = t == d ? 1.0f : (t == len - 1 + d ? 0.5f : 0.0f);
                const auto out = e.tick(in, 0);
                if (t == len)
                    near(out[0], 1.0f, "latency-compensated first playback");
            }
            while (e.progress() > 0.0000001)
            {
                e.tick(0, 0);
                if (e.progress() < 0.0000001)
                    break;
            }
            near(e.tick(0, 0)[0], 1.0f, "compensated impulse at loop origin");
            ticks(e, len - 2);
            near(e.tick(0, 0)[0], 0.5f, "compensated final sample retained");
            ++groups;
        }
        {
            Engine e;
            basicLoop(e);
            e.beginBlock({}, 64, 17);
            e.request(Action::pedal);
            for (int t = 0; t < 1024; ++t)
                e.tick(t == 17 ? 0.5f : 0, 0);
            e.request(Action::play);
            ticks(e, 17);
            ticks(e, 1024 - 17);
            near(e.tick(0, 0)[0], 0.75f, "overdub placement");
            e.request(Action::undo);
            ticks(e, 1023);
            near(e.tick(0, 0)[0], 0.25f, "undo");
            e.request(Action::undo);
            ticks(e, 1023);
            near(e.tick(0, 0)[0], 0.75f, "redo");
            ++groups;
        }
        {
            Engine e;
            basicLoop(e);
            e.request(Action::pedal);
            ticks(e, 1024, 0.25f);
            e.request(Action::play);
            e.request(Action::undo);
            e.request(Action::pedal);
            ticks(e, 1024, 0.125f);
            e.request(Action::play);
            near(e.tick(0, 0)[0], 0.375f, "new dub after undo");
            e.request(Action::undo);
            near(e.tick(0, 0)[0], 0.25f, "undo new layer only");
            ++groups;
        }
        {
            Engine e;
            e.prepare(48000, 1);
            e.beginBlock({}, 64, 400);
            e.request(Action::pedal);
            ticks(e, 200);
            e.request(Action::play);
            check(!e.hasAudio() && e.fault() == Fault::offsetTooLarge, "reject impossible offset");
            ++groups;
        }
        {
            Engine e;
            e.prepare(48000, 0.01);
            e.beginBlock({}, 64, 0);
            e.request(Action::pedal);
            ticks(e, 481, 0.1f);
            check(e.samples() == 480 && e.fault() == Fault::capacity, "capacity bound");
            ++groups;
        }
        // Host phase, not rounded integer duration, prevents accumulation at fractional tempos.
        for (double bpm : {120.0, 123.0, 137.5})
        {
            Engine e;
            e.prepare(48000, 4);
            const double samplesPerBar = 48000 * 240 / bpm;
            int end = static_cast<int>(std::llround(samplesPerBar));
            for (int t = 0; t < end * 12; ++t)
            {
                e.beginBlock(clockAt(t, 48000, bpm), 1, 0);
                if (t == 0)
                    e.request(Action::pedal);
                if (t == end - 100)
                    e.request(Action::play);
                const auto out = e.tick(t == 0 ? 1.0f : 0, 0);
                if (t == end)
                    check(out[0] > 0.49f, "host boundary initial impulse");
            }
            check(e.samples() == end, "fractional bar length");
            check(std::abs(e.beats() - 4.0) < 1e-9, "exact musical period");
            // Position follows PPQ even after many wraps, within a source sample.
            const auto expected = std::fmod(end * 12.0 / samplesPerBar, 1.0);
            check(std::abs(e.progress() - expected) < 0.001, "no accumulated phase drift");
            ++groups;
        }
        {
            Engine e;
            basicLoop(e);
            e.beginBlock(clockAt(0), 64, 0);
            ticks(e, 64);
            e.beginBlock(clockAt(100000), 64, 0);
            check(e.state() == State::stopped, "seek cancels playback");
            ++groups;
        }
        {
            Engine e;
            e.prepare(48000, 4);
            e.beginBlock(clockAt(0), 64, 0);
            e.request(Action::pedal);
            ticks(e, 64);
            auto changed = clockAt(64);
            changed.bpm = 130;
            e.beginBlock(changed, 64, 0);
            check(e.fault() == Fault::captureTempoChanged && !e.hasAudio(), "tempo during capture rejected");
            ++groups;
        }
        {
            Engine e;
            basicLoop(e);
            e.request(Action::stop);
            e.pedal(true);
            ticks(e, 96001);
            check(!e.hasAudio(), "hold stopped clears");
            e.pedal(false);
            check(e.state() == State::stopped, "hold release no action");
            ++groups;
        }
        {
            Engine e;
            basicLoop(e);
            e.pedal(true);
            e.pedal(false);
            check(e.state() == State::overdubbing, "tap enters dub");
            ticks(e, 100);
            e.pedal(true);
            e.pedal(false);
            check(e.state() == State::stopped, "double tap stops");
            ++groups;
        }
        {
            Engine e;
            basicLoop(e);
            e.request(Action::clear);
            e.request(Action::pedal);
            ticks(e, 37, 0.5f);
            e.request(Action::play);
            for (int i = 0; i < 1000; ++i)
                near(e.tick(0, 0)[0], 0.5f, "short re-record no stale audio");
            ++groups;
        }
        {
            Engine e;
            basicLoop(e);
            near(e.tick(std::nanf(""), 0)[0], 0.25f, "NaN input sanitized");
            ++groups;
        }
        {
            Engine e;
            e.prepare(48000, 60);
            e.beginBlock({}, 64, 128);
            watchAllocations = true;
            e.request(Action::pedal);
            ticks(e, 48000);
            e.request(Action::play);
            ticks(e, 1000);
            e.request(Action::pedal);
            ticks(e, 96000);
            e.request(Action::play);
            e.request(Action::undo);
            e.request(Action::undo);
            e.request(Action::stop);
            e.request(Action::clear);
            watchAllocations = false;
            check(allocations == 0, "audio-thread allocation");
            ++groups;
        }
        for (double sampleRate : {44100.0, 48000.0, 96000.0})
            for (int block : {32, 127, 512})
            {
                Engine e;
                e.prepare(sampleRate, 1);
                const int len = 4096, d = 137;
                for (int t = 0; t <= len; ++t)
                {
                    if (t % block == 0)
                        e.beginBlock({}, block, d);
                    if (t == 0)
                        e.request(Action::pedal);
                    if (t == len)
                        e.request(Action::play);
                    auto out = e.tick(t == d ? 1.0f : 0, 0);
                    if (t == len)
                        near(out[0], 1, "sample-rate/block-independent correction");
                }
                ++groups;
            }
        {
            Engine e;
            basicLoop(e);
            e.beginBlock(clockAt(0, 48000, 130), 64, 0);
            e.request(Action::pedal);
            e.tick(0, 0);
            check(e.state() == State::playing && e.fault() == Fault::dubTempoMismatch,
                  "no aliasing overdub at changed tempo");
            ++groups;
        }
        {
            Engine e;
            basicLoop(e);
            e.request(Action::pedal);
            ticks(e, 1024, 0.125f);
            e.request(Action::play);
            e.pedal(true);
            ticks(e, 96001);
            e.pedal(false);
            near(e.tick(0, 0)[0], 0.25f, "hold undoes previous layer");
            ++groups;
        }
        // Monitoring toggles only the dry mix, never recording or loop playback.
        for (bool thru : {false, true})
        {
            Engine e;
            e.prepare(48000, 1);
            e.beginBlock({}, 64, 0);
            e.setInputThru(thru);
            auto out = e.tick(0.25f, -0.5f);
            near(out[0], thru ? 0.25f : 0, "empty dry left");
            near(out[1], thru ? -0.5f : 0, "empty dry right");
            e.request(Action::pedal);
            for (int i = 0; i < 64; ++i)
            {
                out = e.tick(0.25f, -0.5f);
                near(out[0], thru ? 0.25f : 0, "record monitor left");
                near(out[1], thru ? -0.5f : 0, "record monitor right");
            }
            e.request(Action::play);
            out = e.tick(0.125f, 0.125f);
            near(out[0], 0.25f + (thru ? 0.125f : 0), "playback independent of monitoring");
            near(out[1], -0.5f + (thru ? 0.125f : 0), "stereo playback independent of monitoring");
            e.request(Action::pedal);
            for (int i = 0; i < 64; ++i)
            {
                out = e.tick(0.125f, 0.125f);
                near(out[0], 0.25f + (thru ? 0.125f : 0), "dub playback before write");
            }
            e.request(Action::play);
            near(e.tick(0, 0)[0], 0.375f, "dub recorded regardless of monitor");
            e.request(Action::stop);
            near(e.tick(0.5f, 0.5f)[0], thru ? 0.5f : 0, "stopped monitor");
            e.setInputThru(!thru);
            near(e.tick(0.5f, 0.5f)[0], thru ? 0 : 0.5f, "live monitor toggle");
            e.request(Action::clear);
            near(e.tick(0.5f, 0.5f)[0], thru ? 0 : 0.5f, "clear respects monitor toggle");
            ++groups;
        }
        Engine bench;
        basicLoop(bench, 48000);
        bench.request(Action::pedal);
        auto start = std::chrono::steady_clock::now();
        ticks(bench, 48000 * 10, 0.00001f);
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout << "PASS " << groups << " engine groups; audio allocations=" << allocations
                  << "; 10s stereo overdub CPU time=" << ms << "ms; RAM=" << bench.ramBytes()
                  << " bytes (1s test capacity)\n";
    }
    catch (const std::exception& ex)
    {
        watchAllocations = false;
        std::cerr << "FAIL after " << groups << " groups: " << ex.what() << '\n';
        return 1;
    }
}
