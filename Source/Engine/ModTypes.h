#pragma once

#include "../Core/Constants.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace mm
{

//==============================================================================
/** Transport state for one sub-block, computed by the processor from the
    host's play head. ppq is the position at the *start* of the sub-block. */
struct TransportInfo
{
    bool   isPlaying   = false;
    bool   playStarted = false;   // transport went from stopped to playing in this block
    bool   noteOn      = false;   // a note-on arrived in this sub-block
    bool   hasPpq      = false;
    double bpm         = 120.0;
    double ppq         = 0.0;
};

//==============================================================================
/** Ranges shared by the DAW parameters and the engine, so that internal
    (cross-)modulation moves values the same way the knobs do. */
struct ParamRanges
{
    static juce::NormalisableRange<float> lfoRate()
    {
        juce::NormalisableRange<float> r (0.01f, 40.0f);
        r.setSkewForCentre (1.0f);
        return r;
    }

    static juce::NormalisableRange<float> randomRate()
    {
        juce::NormalisableRange<float> r (0.01f, 40.0f);
        r.setSkewForCentre (2.0f);
        return r;
    }

    static juce::NormalisableRange<float> timeMs (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }
};

//==============================================================================
/** One modulation connection, resolved for the audio thread. */
struct PlanConnection
{
    int      source   = 0;
    int      lane     = 0;      // randomizer lane for independent randomization
    float    depth    = 0.0f;   // -1..1
    float    offset   = 0.0f;   // -1..1, added after depth
    Polarity polarity = Polarity::natural;
};

/** Real-time state for one hosted parameter. Lives as long as its hosted
    plug-in slot. Atomics are shared between threads; plain members are only
    touched by the audio thread. */
struct ParamState
{
    std::atomic<float> base     { 0.0f };   // the user's value, without modulation
    std::atomic<float> display  { 0.0f };   // the last value written, for the UI
    std::atomic<bool>  gesture  { false };  // the user is touching this control
    std::atomic<bool>  driven   { false };  // currently modulated or blended

    float smoothed      = 0.0f;
    float lastWritten   = -1.0f;
    bool  smoothedValid = false;

    bool  discrete = false;
    int   numSteps = 0;
};

/** A hosted parameter that is modulated and/or blended. */
struct HostedTarget
{
    int slot  = 0;
    int param = 0;
    std::vector<PlanConnection> connections;
    float rangeMin    = 0.0f;
    float rangeMax    = 1.0f;
    float smoothingMs = 5.0f;
    float smoothCoeff = 1.0f;   // per sub-block, precomputed from smoothingMs
    int   blendEntry  = -1;
};

/** A hosted parameter captured in the preset blender. */
struct BlendEntry
{
    int  slot     = 0;
    int  param    = 0;
    bool discrete = false;
    std::array<float, kNumSnapshots> values {};
    uint8_t mask = 0;   // bit i set when snapshot i holds a value
};

/** Everything the modulation engine needs to know about routing. Immutable
    once published. */
struct RoutingPlan
{
    std::array<int, kNumNodes> nodeOrder {};   // evaluation order (cross-mod safe)
    std::array<std::vector<PlanConnection>, kNumInternalParams> internal;
    std::vector<HostedTarget> hosted;
    std::vector<BlendEntry> blend;

    RoutingPlan()
    {
        for (int i = 0; i < kNumNodes; ++i)
            nodeOrder[(size_t) i] = i;
    }
};

} // namespace mm
