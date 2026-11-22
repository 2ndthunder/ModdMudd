#pragma once

#include "ModTypes.h"

namespace mm
{

struct LfoSettings
{
    LfoShape     shape        = LfoShape::sine;
    bool         sync         = false;
    int          division     = kDefaultDivision;
    float        rateHz       = 1.0f;
    float        phaseOffset  = 0.0f;   // 0..1
    float        depth        = 1.0f;   // 0..1
    float        offset       = 0.0f;   // -1..1
    bool         invert       = false;
    LfoRetrigger retrigger    = LfoRetrigger::free;
    float        fadeInSeconds = 0.0f;
};

/** A custom LFO waveform point. x and y are 0..1 and -1..1; curve bends the
    segment that starts at this point (-1..1, 0 = straight). */
struct WavePoint
{
    float x = 0.0f, y = 0.0f, curve = 0.0f;
};

class Lfo
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Computes the output for the start of the sub-block, then advances.

        rateMultiplier scales the rate (cross-modulation of rate in sync mode);
        when it is 1 and the transport is running, a synced LFO locks its phase
        to the song position. */
    float process (const LfoSettings& s, const TransportInfo& t, int numSamples,
                   float rateHz, float rateMultiplier, float depth, float phaseOffset,
                   const float* customTable) noexcept;

    double getPhase() const noexcept   { return phase; }

    static float evaluate (LfoShape shape, double phase, const float* customTable) noexcept;

    /** Renders a custom waveform into a table of kLfoTableSize values. */
    static void buildCustomTable (std::vector<WavePoint> points, float* table);

    static std::vector<WavePoint> defaultCustomPoints();

private:
    double sampleRate  = 44100.0;
    double phase       = 0.0;
    double fadeElapsed = 1.0e9;
};

} // namespace mm
