#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace mm
{

/** Gives hosted plug-ins a play head whose position is correct for the
    sub-block being processed, rather than the start of the host's block. */
class SubBlockPlayHead final : public juce::AudioPlayHead
{
public:
    /** Audio thread, once per host block. */
    void beginBlock (juce::AudioPlayHead* hostPlayHead, double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        offsetSamples = 0;
        base = {};

        if (hostPlayHead != nullptr)
            if (auto pos = hostPlayHead->getPosition())
                base = *pos;

        hasBase = hostPlayHead != nullptr;
    }

    void setOffset (int samples) noexcept   { offsetSamples = samples; }

    const juce::Optional<PositionInfo>& getBase() const noexcept   { return base; }

    juce::Optional<PositionInfo> getPosition() const override
    {
        if (! hasBase || ! base.hasValue())
            return {};

        auto p = *base;

        if (offsetSamples != 0 && p.getIsPlaying())
        {
            const double seconds = offsetSamples / sampleRate;

            if (auto t = p.getTimeInSamples())  p.setTimeInSamples (*t + offsetSamples);
            if (auto s = p.getTimeInSeconds())  p.setTimeInSeconds (*s + seconds);

            if (auto ppq = p.getPpqPosition())
                if (auto bpm = p.getBpm())
                    p.setPpqPosition (*ppq + seconds * (*bpm / 60.0));
        }

        return p;
    }

    /** Song position in quarter notes at a sample offset within the block. */
    static bool ppqAt (const juce::Optional<PositionInfo>& pos, int offset, double sampleRate, double& ppqOut) noexcept
    {
        if (! pos.hasValue())
            return false;

        auto ppq = pos->getPpqPosition();
        auto bpm = pos->getBpm();

        if (! ppq.hasValue() || ! bpm.hasValue())
            return false;

        ppqOut = *ppq + (pos->getIsPlaying() ? (offset / sampleRate) * (*bpm / 60.0) : 0.0);
        return true;
    }

private:
    juce::Optional<PositionInfo> base;
    double sampleRate = 44100.0;
    int offsetSamples = 0;
    bool hasBase = false;
};

} // namespace mm
