#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace mm
{

//==============================================================================
// Fixed sizes. Everything the audio thread touches is sized at compile time
// or at prepare time, so nothing is allocated while processing.
inline constexpr int kNumLfos           = 4;
inline constexpr int kNumRandoms        = 2;
inline constexpr int kNumMacros         = 8;
inline constexpr int kNumEnvs           = 2;
inline constexpr int kNumSlotParams     = 256;  // DAW-visible remappable parameters
inline constexpr int kMaxHostedPlugins  = 4;
inline constexpr int kRandomLanes       = 8;    // independent random streams per randomizer
inline constexpr int kLfoTableSize      = 2048;
inline constexpr int kMaxCustomPoints   = 64;
inline constexpr int kNumSnapshots      = 4;    // preset blender states A-D
inline constexpr int kDefaultSubBlock   = 32;

//==============================================================================
// Modulation sources. A source produces one value per sub-block.
enum SourceIndex : int
{
    srcLfo0    = 0,
    srcRandom0 = srcLfo0 + kNumLfos,
    srcMacro0  = srcRandom0 + kNumRandoms,
    srcXyX     = srcMacro0 + kNumMacros,
    srcXyY,
    srcEnv0,
    kNumSources = srcEnv0 + kNumEnvs
};

//==============================================================================
// Modulator "nodes" are the objects that own sources. The XY pad is one node
// with two sources. Cross-modulation dependencies are tracked between nodes.
enum NodeIndex : int
{
    nodeLfo0    = 0,
    nodeRandom0 = nodeLfo0 + kNumLfos,
    nodeMacro0  = nodeRandom0 + kNumRandoms,
    nodeXy      = nodeMacro0 + kNumMacros,
    nodeEnv0,
    kNumNodes = nodeEnv0 + kNumEnvs
};

constexpr int nodeForSource (int source) noexcept
{
    if (source < srcXyX)            return source;          // LFO, random and macro map 1:1
    if (source == srcXyX || source == srcXyY) return nodeXy;
    return nodeEnv0 + (source - srcEnv0);
}

//==============================================================================
// Internal (cross-modulation) targets: modulator settings that other
// modulators can drive.
enum InternalParam : int
{
    ipLfoRate0 = 0,                                // per LFO: rate, depth, phase
    ipRandomRate0 = ipLfoRate0 + kNumLfos * 3,     // per random: rate, depth
    ipEnvGain0 = ipRandomRate0 + kNumRandoms * 2,  // per env: gain, depth
    ipMacroAmount0 = ipEnvGain0 + kNumEnvs * 2,    // per macro: output amount
    ipXyX = ipMacroAmount0 + kNumMacros,
    ipXyY,
    ipBlendX,
    ipBlendY,
    kNumInternalParams
};

constexpr int lfoInternal (int lfo, int which) noexcept     { return ipLfoRate0 + lfo * 3 + which; }    // 0 rate, 1 depth, 2 phase
constexpr int randomInternal (int r, int which) noexcept    { return ipRandomRate0 + r * 2 + which; }   // 0 rate, 1 depth
constexpr int envInternal (int e, int which) noexcept       { return ipEnvGain0 + e * 2 + which; }      // 0 gain, 1 depth

/** The node whose behaviour an internal parameter changes, or -1 for the blender. */
constexpr int ownerNodeForInternal (int ip) noexcept
{
    if (ip < ipRandomRate0)  return nodeLfo0 + (ip - ipLfoRate0) / 3;
    if (ip < ipEnvGain0)     return nodeRandom0 + (ip - ipRandomRate0) / 2;
    if (ip < ipMacroAmount0) return nodeEnv0 + (ip - ipEnvGain0) / 2;
    if (ip < ipXyX)          return nodeMacro0 + (ip - ipMacroAmount0);
    if (ip == ipXyX || ip == ipXyY) return nodeXy;
    return -1;
}

//==============================================================================
// Modules are what the user adds to the modulation chain in the editor. Each
// owns some sources and/or some cross-modulation targets.
enum ModuleId : int
{
    modLfo0    = 0,
    modRandom0 = modLfo0 + kNumLfos,
    modMacros  = modRandom0 + kNumRandoms,
    modXy,
    modBlend,
    modEnv0,
    kNumModules = modEnv0 + kNumEnvs
};

inline juce::String getModuleName (int m)
{
    if (m >= modLfo0 && m < modRandom0)    return "LFO " + juce::String (m - modLfo0 + 1);
    if (m >= modRandom0 && m < modMacros)  return "Random " + juce::String (m - modRandom0 + 1);
    if (m == modMacros)                    return "Macros";
    if (m == modXy)                        return "XY Pad";
    if (m == modBlend)                     return "Preset Blender";
    if (m >= modEnv0 && m < kNumModules)   return "Envelope " + juce::String (m - modEnv0 + 1);
    return {};
}

inline juce::String getModuleTabName (int m)
{
    if (m >= modLfo0 && m < modRandom0)    return "LFO " + juce::String (m - modLfo0 + 1);
    if (m >= modRandom0 && m < modMacros)  return "RND " + juce::String (m - modRandom0 + 1);
    if (m == modMacros)                    return "MACRO";
    if (m == modXy)                        return "XY";
    if (m == modBlend)                     return "BLEND";
    if (m >= modEnv0 && m < kNumModules)   return "ENV " + juce::String (m - modEnv0 + 1);
    return {};
}

inline bool moduleOwnsSource (int m, int source)
{
    if (m >= modLfo0 && m < modRandom0)    return source == srcLfo0 + (m - modLfo0);
    if (m >= modRandom0 && m < modMacros)  return source == srcRandom0 + (m - modRandom0);
    if (m == modMacros)                    return source >= srcMacro0 && source < srcXyX;
    if (m == modXy)                        return source == srcXyX || source == srcXyY;
    if (m >= modEnv0 && m < kNumModules)   return source == srcEnv0 + (m - modEnv0);
    return false;
}

inline bool moduleOwnsInternal (int m, int ip)
{
    if (m == modBlend)
        return ip == ipBlendX || ip == ipBlendY;

    const int owner = ownerNodeForInternal (ip);
    for (int s = 0; s < kNumSources; ++s)
        if (moduleOwnsSource (m, s) && nodeForSource (s) == owner)
            return true;

    return false;
}

//==============================================================================
enum class Polarity : int
{
    natural = 0,   // use the source as it is
    unipolar,      // map to 0..1
    bipolar        // map to -1..1
};

enum class LfoShape : int { sine = 0, triangle, square, saw, reverseSaw, custom, numShapes };

enum class LfoRetrigger : int { free = 0, transport, note };

enum class EnvSource : int { input = 0, output, sidechain };

enum class EnvDetector : int { peak = 0, rms };

//==============================================================================
// Musical divisions for tempo sync, expressed in quarter notes per cycle.
struct Division { const char* name; double quarters; };

inline const std::array<Division, 19>& getDivisions()
{
    static const std::array<Division, 19> divisions {{
        { "8 bars", 32.0 }, { "4 bars", 16.0 }, { "2 bars", 8.0 }, { "1 bar", 4.0 },
        { "1/2", 2.0 },     { "1/2T", 4.0 / 3.0 }, { "1/2.", 3.0 },
        { "1/4", 1.0 },     { "1/4T", 2.0 / 3.0 }, { "1/4.", 1.5 },
        { "1/8", 0.5 },     { "1/8T", 1.0 / 3.0 }, { "1/8.", 0.75 },
        { "1/16", 0.25 },   { "1/16T", 1.0 / 6.0 }, { "1/16.", 0.375 },
        { "1/32", 0.125 },  { "1/32T", 1.0 / 12.0 }, { "1/64", 0.0625 }
    }};
    return divisions;
}

inline juce::StringArray getDivisionNames()
{
    juce::StringArray names;
    for (auto& d : getDivisions())
        names.add (d.name);
    return names;
}

inline constexpr int kDefaultDivision = 7; // 1/4

//==============================================================================
inline juce::String getSourceName (int source)
{
    if (source >= srcLfo0 && source < srcRandom0)    return "LFO " + juce::String (source - srcLfo0 + 1);
    if (source >= srcRandom0 && source < srcMacro0)  return "Random " + juce::String (source - srcRandom0 + 1);
    if (source >= srcMacro0 && source < srcXyX)      return "Macro " + juce::String (source - srcMacro0 + 1);
    if (source == srcXyX)                            return "XY X";
    if (source == srcXyY)                            return "XY Y";
    if (source >= srcEnv0 && source < kNumSources)   return "Env " + juce::String (source - srcEnv0 + 1);
    return {};
}

inline juce::String getInternalParamName (int ip)
{
    if (ip < ipRandomRate0)
    {
        const int lfo = (ip - ipLfoRate0) / 3, which = (ip - ipLfoRate0) % 3;
        static const char* names[] { "Rate", "Depth", "Phase" };
        return "LFO " + juce::String (lfo + 1) + " " + names[which];
    }
    if (ip < ipEnvGain0)
    {
        const int r = (ip - ipRandomRate0) / 2, which = (ip - ipRandomRate0) % 2;
        return "Random " + juce::String (r + 1) + (which == 0 ? " Rate" : " Depth");
    }
    if (ip < ipMacroAmount0)
    {
        const int e = (ip - ipEnvGain0) / 2, which = (ip - ipEnvGain0) % 2;
        return "Env " + juce::String (e + 1) + (which == 0 ? " Gain" : " Depth");
    }
    if (ip < ipXyX)    return "Macro " + juce::String (ip - ipMacroAmount0 + 1) + " Amount";
    if (ip == ipXyX)   return "XY X";
    if (ip == ipXyY)   return "XY Y";
    if (ip == ipBlendX) return "Blend X";
    if (ip == ipBlendY) return "Blend Y";
    return {};
}

} // namespace mm
