#pragma once

#include "Lfo.h"
#include "Randomizer.h"
#include "EnvelopeFollower.h"

namespace mm
{

struct XYSettings
{
    float x = 0.5f, y = 0.5f;
    float centerX = 0.5f, centerY = 0.5f;
    float glideMs = 30.0f;
    bool  bipolar = true;   // output relative to the centre (-1..1) instead of 0..1
};

/** All modulator settings for one block, read from the DAW parameters. */
struct EngineSettings
{
    std::array<LfoSettings, kNumLfos>       lfo;
    std::array<RandomSettings, kNumRandoms> random;
    std::array<float, kNumMacros>           macro {};
    XYSettings                              xy;
    std::array<EnvSettings, kNumEnvs>       env;
    bool  blendEnabled = false;
    float blendX = 0.0f, blendY = 0.0f;
};

/** Runs the modulators once per sub-block in dependency order and evaluates
    modulation for destinations. Audio thread only, except the display atomics. */
class ModEngine
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setRandomSeed (int index, uint32_t seed) noexcept;
    uint32_t getRandomSeed (int index) const noexcept;

    /** Feeds audio to an envelope follower. Call before computeSubBlock. */
    void feedEnvelope (int index, const float* const* channels, int numChannels, int numSamples,
                       const EnvSettings& s) noexcept;

    /** Computes every source value for the start of this sub-block and
        advances the modulators by numSamples. */
    void computeSubBlock (const EngineSettings& s, const TransportInfo& t, const RoutingPlan& plan,
                          int numSamples, const std::array<const float*, kNumLfos>& customTables) noexcept;

    float getSourceValue (int source, int lane = 0) const noexcept;
    bool  isSourceBipolar (int source) const noexcept;

    float contribution (const PlanConnection& c) const noexcept;
    float sumContributions (const std::vector<PlanConnection>& conns) const noexcept;

    /** Destination value before range clamping and smoothing. */
    float evaluate (float base, const HostedTarget& target) const noexcept
    {
        return juce::jlimit (target.rangeMin, target.rangeMax, base + sumContributions (target.connections));
    }

    std::array<float, kNumSnapshots> getBlendWeights() const noexcept;
    static std::array<float, kNumSnapshots> blendWeights (float x, float y) noexcept;

    /** Value of a blended parameter, or false if no snapshot holds it. */
    static bool resolveBlend (const BlendEntry& e, const std::array<float, kNumSnapshots>& weights, float& out) noexcept;

    float getInternalMod (int ip) const noexcept   { return internalMod[(size_t) ip]; }

    //==============================================================================
    // For the UI. Written by the audio thread, read by the message thread.
    std::array<std::atomic<float>, kNumSources> sourceDisplay {};
    std::array<std::atomic<float>, kNumLfos>    lfoPhaseDisplay {};
    std::array<std::atomic<float>, kNumEnvs>    envLevelDisplay {};
    std::atomic<float> blendXDisplay { 0.0f }, blendYDisplay { 0.0f };
    std::atomic<float> xyXDisplay { 0.5f }, xyYDisplay { 0.5f };

private:
    void runNode (int node, const EngineSettings& s, const TransportInfo& t, const RoutingPlan& plan,
                  int numSamples, const std::array<const float*, kNumLfos>& tables) noexcept;
    float gatherInternal (int ip, const RoutingPlan& plan) noexcept;

    double sampleRate = 44100.0;

    std::array<Lfo, kNumLfos> lfos;
    std::array<Randomizer, kNumRandoms> randoms;
    std::array<EnvelopeFollower, kNumEnvs> envs;

    std::array<float, kNumSources> values {};
    std::array<float, kNumInternalParams> internalMod {};
    std::array<float, kNumMacros> macroSmoothed {};
    std::array<float, kNumEnvs> envGainModDb {};
    float xySmoothX = 0.5f, xySmoothY = 0.5f;
    bool  xyInitialised = false;
    bool  xyBipolar = true;
    float blendX = 0.0f, blendY = 0.0f;
};

} // namespace mm
