#pragma once

#include "ModTypes.h"

namespace mm
{

struct EnvSettings
{
    EnvSource   source      = EnvSource::input;
    EnvDetector detector    = EnvDetector::peak;
    float attackMs    = 10.0f;
    float releaseMs   = 150.0f;
    float gainDb      = 0.0f;
    float thresholdDb = -48.0f;   // levels below this give 0
    float sensitivity = 0.5f;     // 0: 60 dB range above threshold, 1: 6 dB range
    float curve       = 0.0f;     // -1..1 response curve
    float smoothMs    = 10.0f;
    float depth       = 1.0f;
    bool  invert      = false;
};

/** Converts audio level into a 0..1 modulation signal. */
class EnvelopeFollower
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Feeds audio. extraGainDb comes from cross-modulation of the gain. */
    void processAudio (const float* const* channels, int numChannels, int numSamples,
                       const EnvSettings& s, float extraGainDb) noexcept;

    /** The shaped 0..1 level before depth is applied. */
    float getLevel() const noexcept   { return smoothed; }

    static float shape (float x, float curve) noexcept;

private:
    double sampleRate = 44100.0;
    float detector = 0.0f;   // peak or mean-square follower state
    float smoothed = 0.0f;
};

} // namespace mm
