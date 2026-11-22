#pragma once

#include "ModTypes.h"

namespace mm
{

struct RandomSettings
{
    bool  sync        = false;
    int   division    = kDefaultDivision;
    float rateHz      = 2.0f;
    bool  smooth      = true;    // true: continuous curve, false: stepped (sample & hold)
    float probability = 1.0f;    // chance that a new value is chosen each cycle
    float glide       = 0.1f;    // stepped mode: fraction of a cycle spent moving to the new value
    float depth       = 1.0f;
    float offset      = 0.0f;
};

/** Seeded random modulation with several independent lanes.

    New values are derived from a hash of (seed, cycle number, lane), so a
    tempo-synced randomizer produces the same sequence every time playback
    starts from the same position. */
class Randomizer
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setSeed (uint32_t newSeed) noexcept   { seed = newSeed; }
    uint32_t getSeed() const noexcept          { return seed; }

    /** Advances by numSamples. Lane outputs are taken at the start of the sub-block. */
    void process (const RandomSettings& s, const TransportInfo& t, int numSamples,
                  float rateHz, float rateMultiplier, float depth) noexcept;

    float getValue (int lane) const noexcept   { return output[(size_t) juce::jlimit (0, kRandomLanes - 1, lane)]; }

    /** Deterministic 0..1 value for (seed, cycle, lane, salt). */
    static float hashToUnit (uint32_t seed, int64_t cycle, int lane, int salt) noexcept;

private:
    void startCycle (int64_t cycle, float probability) noexcept;
    float targetFor (int64_t cycle, int lane, float probability) const noexcept;
    void computeOutputs (const RandomSettings& s, float depth) noexcept;

    double sampleRate = 44100.0;
    uint32_t seed = 1;
    double phase = 0.0;           // position within the current cycle, 0..1
    int64_t cycleIndex = -1;      // -1 = not started

    std::array<float, kRandomLanes> from {}, to {}, current {}, output {};
};

} // namespace mm
