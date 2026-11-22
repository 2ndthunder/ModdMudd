#include "Randomizer.h"
#include <cmath>

namespace mm
{

void Randomizer::prepare (double sr) noexcept
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    reset();
}

void Randomizer::reset() noexcept
{
    phase = 0.0;
    cycleIndex = -1;
    from.fill (0.0f);
    to.fill (0.0f);
    current.fill (0.0f);
    output.fill (0.0f);
}

static uint64_t splitMix64 (uint64_t x) noexcept
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

float Randomizer::hashToUnit (uint32_t s, int64_t cycle, int lane, int salt) noexcept
{
    uint64_t h = splitMix64 ((uint64_t) s);
    h = splitMix64 (h ^ (uint64_t) cycle);
    h = splitMix64 (h ^ ((uint64_t) lane << 8) ^ (uint64_t) salt);
    return (float) ((h >> 40) * (1.0 / 16777216.0)); // 24 bits -> [0, 1)
}

float Randomizer::targetFor (int64_t cycle, int lane, float probability) const noexcept
{
    // The target of a cycle is a fresh random value if that cycle "passes" the
    // probability test, otherwise the target of the most recent cycle that
    // did. Looking back a bounded number of cycles keeps this a pure function
    // of (seed, cycle, lane), so a synced randomizer is repeatable.
    for (int back = 0; back < 64; ++back)
    {
        const auto c = cycle - back;
        if (hashToUnit (seed, c, lane, 1) < probability)
            return hashToUnit (seed, c, lane, 2) * 2.0f - 1.0f;
    }

    return 0.0f;
}

void Randomizer::startCycle (int64_t cycle, float probability) noexcept
{
    cycleIndex = cycle;

    for (int lane = 0; lane < kRandomLanes; ++lane)
    {
        const auto l = (size_t) lane;
        from[l] = targetFor (cycle - 1, lane, probability);
        to[l]   = targetFor (cycle, lane, probability);
    }
}

void Randomizer::computeOutputs (const RandomSettings& s, float depth) noexcept
{
    for (size_t l = 0; l < (size_t) kRandomLanes; ++l)
    {
        float t;

        if (s.smooth)
        {
            // Cosine interpolation across the whole cycle: continuous, no jumps.
            t = 0.5f - 0.5f * std::cos ((float) phase * juce::MathConstants<float>::pi);
        }
        else
        {
            const float g = juce::jmax (1.0e-4f, s.glide);
            t = juce::jmin (1.0f, (float) phase / g);
        }

        current[l] = from[l] + (to[l] - from[l]) * t;
        output[l]  = juce::jlimit (-1.0f, 1.0f, current[l] * depth + s.offset);
    }
}

void Randomizer::process (const RandomSettings& s, const TransportInfo& t, int numSamples,
                          float rateHz, float rateMultiplier, float depth) noexcept
{
    const double quarters = getDivisions()[(size_t) juce::jlimit (0, (int) getDivisions().size() - 1, s.division)].quarters;
    const bool locked = s.sync && t.isPlaying && t.hasPpq && std::abs (rateMultiplier - 1.0f) < 1.0e-6f;

    double cyclesPerSecond = s.sync ? (t.bpm / 60.0) / quarters * rateMultiplier : rateHz;

    if (locked)
    {
        const double pos = t.ppq / quarters;
        const auto cycle = (int64_t) std::floor (pos);

        if (cycle != cycleIndex)
            startCycle (cycle, s.probability);

        phase = pos - std::floor (pos);
    }
    else if (cycleIndex < 0)
    {
        startCycle (0, s.probability);
    }

    computeOutputs (s, depth);

    if (! locked)
    {
        phase += cyclesPerSecond * numSamples / sampleRate;

        while (phase >= 1.0)
        {
            phase -= 1.0;
            startCycle (cycleIndex + 1, s.probability);
        }
    }
}

} // namespace mm
