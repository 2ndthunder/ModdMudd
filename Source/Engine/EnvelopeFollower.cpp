#include "EnvelopeFollower.h"
#include <cmath>

namespace mm
{

void EnvelopeFollower::prepare (double sr) noexcept
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    reset();
}

void EnvelopeFollower::reset() noexcept
{
    detector = 0.0f;
    smoothed = 0.0f;
}

float EnvelopeFollower::shape (float x, float curve) noexcept
{
    x = juce::jlimit (0.0f, 1.0f, x);

    if (std::abs (curve) < 1.0e-4f)
        return x;

    const float k = curve * 6.0f;
    return (std::exp (k * x) - 1.0f) / (std::exp (k) - 1.0f);
}

static float coefficientFor (float ms, double sampleRate) noexcept
{
    if (ms <= 0.0f)
        return 1.0f;

    return 1.0f - (float) std::exp (-1.0 / (sampleRate * ms * 0.001));
}

void EnvelopeFollower::processAudio (const float* const* channels, int numChannels, int numSamples,
                                     const EnvSettings& s, float extraGainDb) noexcept
{
    if (numSamples <= 0)
        return;

    const float attack  = coefficientFor (s.attackMs, sampleRate);
    const float release = coefficientFor (s.releaseMs, sampleRate);
    const float gain    = juce::Decibels::decibelsToGain (s.gainDb + extraGainDb);
    const bool  rms     = s.detector == EnvDetector::rms;

    for (int i = 0; i < numSamples; ++i)
    {
        float x = 0.0f;

        if (channels != nullptr && numChannels > 0)
        {
            if (rms)
            {
                for (int c = 0; c < numChannels; ++c)
                    x += channels[c][i] * channels[c][i];
                x = x / (float) numChannels * gain * gain;
            }
            else
            {
                for (int c = 0; c < numChannels; ++c)
                    x = juce::jmax (x, std::abs (channels[c][i]));
                x *= gain;
            }
        }

        detector += (x > detector ? attack : release) * (x - detector);
    }

    // Guard against denormals and silent input.
    if (detector < 1.0e-12f)
        detector = 0.0f;

    const float level = rms ? std::sqrt (detector) : detector;
    const float db    = juce::Decibels::gainToDecibels (level, -150.0f);
    const float range = juce::jmap (juce::jlimit (0.0f, 1.0f, s.sensitivity), 60.0f, 6.0f);

    float norm = shape ((db - s.thresholdDb) / range, s.curve);
    if (s.invert)
        norm = 1.0f - norm;

    const float smoothCoeff = 1.0f - (float) std::exp (-(double) numSamples / (sampleRate * juce::jmax (0.1f, s.smoothMs) * 0.001));
    smoothed += (norm - smoothed) * smoothCoeff;
}

} // namespace mm
