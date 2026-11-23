#include "Shaper.h"
#include <cmath>

namespace mm
{

void Shaper::prepare (double sr) noexcept
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    reset();
}

void Shaper::reset() noexcept
{
    phase = 1.0;
    running = false;
    audioTriggered = false;
    pendingTrigger.store (false);
    detector = 0.0f;
    above = false;
    holdoffSeconds = 0.0;
}

std::vector<WavePoint> Shaper::defaultPoints()
{
    // A plucky attack/decay: quick rise, curved fall back to zero.
    return { { 0.0f, -1.0f, 0.0f }, { 0.06f, 1.0f, -0.6f }, { 1.0f, -1.0f, 0.0f } };
}

float Shaper::valueAt (const float* table, double position) noexcept
{
    if (table == nullptr)
        return 0.0f;

    // The end of a one-shot curve is the table's last entry, not a wrap back
    // to the start.
    if (position >= 1.0)
        return juce::jlimit (0.0f, 1.0f, (table[kLfoTableSize - 1] + 1.0f) * 0.5f);

    return juce::jlimit (0.0f, 1.0f, (Lfo::evaluate (LfoShape::custom, juce::jmax (0.0, position), table) + 1.0f) * 0.5f);
}

void Shaper::feedAudio (const float* const* channels, int numChannels, int numSamples, float thresholdDb) noexcept
{
    if (numSamples <= 0)
        return;

    const float attack  = 1.0f - (float) std::exp (-1.0 / (sampleRate * 0.0005));
    const float release = 1.0f - (float) std::exp (-1.0 / (sampleRate * 0.080));
    const float on  = juce::Decibels::decibelsToGain (thresholdDb);
    const float off = juce::Decibels::decibelsToGain (thresholdDb - 3.0f);   // hysteresis

    for (int i = 0; i < numSamples; ++i)
    {
        float x = 0.0f;
        if (channels != nullptr)
            for (int c = 0; c < numChannels; ++c)
                x = juce::jmax (x, std::abs (channels[c][i]));

        detector += (x > detector ? attack : release) * (x - detector);

        if (! above && detector > on && holdoffSeconds <= 0.0)
        {
            above = true;
            audioTriggered = true;
            holdoffSeconds = 0.03;   // ignore re-triggers within 30 ms
        }
        else if (above && detector < off)
        {
            above = false;
        }
    }

    if (detector < 1.0e-9f)
        detector = 0.0f;

    holdoffSeconds -= numSamples / sampleRate;
}

float Shaper::process (const ShaperSettings& s, const TransportInfo& t, int numSamples, double lengthSeconds,
                       bool allowSongLock, float depth, const float* table) noexcept
{
    bool triggerNow = pendingTrigger.exchange (false, std::memory_order_relaxed);

    switch (s.trigger)
    {
        case ShaperTrigger::note:      triggerNow = triggerNow || t.noteOn; break;
        case ShaperTrigger::audio:     triggerNow = triggerNow || audioTriggered; break;
        case ShaperTrigger::transport: triggerNow = triggerNow || t.playStarted; break;
        case ShaperTrigger::loop:      break;
    }

    audioTriggered = false;
    lengthSeconds = juce::jmax (0.001, lengthSeconds);

    if (s.trigger == ShaperTrigger::loop)
    {
        running = true;

        if (triggerNow)
            phase = 0.0;

        // A synced loop locks to the song position, like a synced LFO.
        if (s.sync && allowSongLock && t.isPlaying && t.hasPpq)
        {
            const double quarters = getDivisions()[(size_t) juce::jlimit (0, (int) getDivisions().size() - 1, s.division)].quarters;
            phase = t.ppq / quarters - std::floor (t.ppq / quarters);
        }

        const float out = valueAt (table, phase >= 1.0 ? 0.0 : phase) * depth;
        phase += numSamples / sampleRate / lengthSeconds;
        phase -= std::floor (phase);
        return out;
    }

    if (triggerNow)
    {
        phase = 0.0;
        running = true;
    }

    const float out = valueAt (table, running ? phase : 1.0) * depth;

    if (running)
    {
        phase += numSamples / sampleRate / lengthSeconds;
        if (phase >= 1.0)
        {
            phase = 1.0;
            running = false;
        }
    }

    return out;
}

} // namespace mm
