#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Engine/ModEngine.h"

namespace mm
{

/** A DAW-visible parameter whose name follows whichever hosted parameter it
    is mapped to. VST3 parameter lists are fixed when the plug-in loads, so
    ModdMudd exposes a pool of these and renames them. */
class RemappableParameter final : public juce::AudioParameterFloat
{
public:
    RemappableParameter (int slotIndex)
        : juce::AudioParameterFloat (juce::ParameterID { makeId (slotIndex), 1 }, makeDefaultName (slotIndex),
                                     juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f),
          index (slotIndex)
    {
    }

    static juce::String makeId (int i)           { return "slot" + juce::String (i + 1).paddedLeft ('0', 3); }
    static juce::String makeDefaultName (int i)  { return "Slot " + juce::String (i + 1).paddedLeft ('0', 3); }

    /** Message thread. */
    void setDisplayName (const juce::String& n)
    {
        const juce::SpinLock::ScopedLockType sl (nameLock);
        displayName = n;
    }

    juce::String getName (int maximumLength) const override
    {
        const juce::SpinLock::ScopedLockType sl (nameLock);
        const auto n = displayName.isEmpty() ? makeDefaultName (index) : displayName;
        return n.substring (0, maximumLength);
    }

    const int index;

private:
    mutable juce::SpinLock nameLock;
    juce::String displayName;
};

namespace ids
{
    inline juce::String macro (int i)                  { return "macro" + juce::String (i + 1); }
    inline juce::String lfo (int i, const char* what)  { return "lfo" + juce::String (i + 1) + "_" + what; }
    inline juce::String rnd (int i, const char* what)  { return "rnd" + juce::String (i + 1) + "_" + what; }
    inline juce::String env (int i, const char* what)  { return "env" + juce::String (i + 1) + "_" + what; }

    inline const juce::String xyX = "xyX", xyY = "xyY", xyCenterX = "xyCenterX", xyCenterY = "xyCenterY",
                              xyGlide = "xyGlide", xyBipolar = "xyBipolar",
                              blendOn = "blendOn", blendX = "blendX", blendY = "blendY",
                              outputGain = "outputGain";
}

/** Raw pointers to parameter values, read on the audio thread. */
struct ParameterRefs
{
    struct Lfo
    {
        std::atomic<float>* shape; std::atomic<float>* sync; std::atomic<float>* division; std::atomic<float>* rate;
        std::atomic<float>* phase; std::atomic<float>* depth; std::atomic<float>* offset; std::atomic<float>* invert;
        std::atomic<float>* retrigger; std::atomic<float>* fade;
    };

    struct Random
    {
        std::atomic<float>* sync; std::atomic<float>* division; std::atomic<float>* rate; std::atomic<float>* smooth;
        std::atomic<float>* probability; std::atomic<float>* glide; std::atomic<float>* depth; std::atomic<float>* offset;
        std::atomic<float>* independent;
    };

    struct Env
    {
        std::atomic<float>* source; std::atomic<float>* detector; std::atomic<float>* attack; std::atomic<float>* release;
        std::atomic<float>* gain; std::atomic<float>* threshold; std::atomic<float>* sensitivity; std::atomic<float>* curve;
        std::atomic<float>* smooth; std::atomic<float>* depth; std::atomic<float>* invert;
    };

    std::array<Lfo, kNumLfos> lfo {};
    std::array<Random, kNumRandoms> random {};
    std::array<Env, kNumEnvs> env {};
    std::array<std::atomic<float>*, kNumMacros> macro {};
    std::atomic<float>* xyX = nullptr; std::atomic<float>* xyY = nullptr;
    std::atomic<float>* xyCenterX = nullptr; std::atomic<float>* xyCenterY = nullptr;
    std::atomic<float>* xyGlide = nullptr; std::atomic<float>* xyBipolar = nullptr;
    std::atomic<float>* blendOn = nullptr; std::atomic<float>* blendX = nullptr; std::atomic<float>* blendY = nullptr;
    std::atomic<float>* outputGain = nullptr;

    void bind (juce::AudioProcessorValueTreeState& state);
    void fill (EngineSettings& s) const noexcept;
};

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (std::array<RemappableParameter*, kNumSlotParams>& slotParams);

} // namespace mm
