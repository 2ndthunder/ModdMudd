#include "ModEngine.h"
#include <cmath>

namespace mm
{

void ModEngine::prepare (double sr) noexcept
{
    sampleRate = sr > 0.0 ? sr : 44100.0;

    for (auto& l : lfos)    l.prepare (sampleRate);
    for (auto& r : randoms) r.prepare (sampleRate);
    for (auto& e : envs)    e.prepare (sampleRate);

    reset();
}

void ModEngine::reset() noexcept
{
    for (auto& l : lfos)    l.reset();
    for (auto& r : randoms) r.reset();
    for (auto& e : envs)    e.reset();

    values.fill (0.0f);
    internalMod.fill (0.0f);
    macroSmoothed.fill (0.0f);
    envGainModDb.fill (0.0f);
    xyInitialised = false;
}

void ModEngine::setRandomSeed (int index, uint32_t seed) noexcept
{
    if (juce::isPositiveAndBelow (index, kNumRandoms))
        randoms[(size_t) index].setSeed (seed);
}

uint32_t ModEngine::getRandomSeed (int index) const noexcept
{
    return juce::isPositiveAndBelow (index, kNumRandoms) ? randoms[(size_t) index].getSeed() : 0u;
}

void ModEngine::feedEnvelope (int index, const float* const* channels, int numChannels, int numSamples,
                              const EnvSettings& s) noexcept
{
    if (! juce::isPositiveAndBelow (index, kNumEnvs))
        return;

    envs[(size_t) index].processAudio (channels, numChannels, numSamples, s, envGainModDb[(size_t) index]);
}

//==============================================================================
bool ModEngine::isSourceBipolar (int source) const noexcept
{
    if (source < srcMacro0)               return true;    // LFOs and randomizers
    if (source < srcXyX)                  return false;   // macros
    if (source == srcXyX || source == srcXyY) return xyBipolar;
    return false;                                         // envelope followers
}

float ModEngine::getSourceValue (int source, int lane) const noexcept
{
    if (source >= srcRandom0 && source < srcMacro0)
        return randoms[(size_t) (source - srcRandom0)].getValue (lane);

    return juce::isPositiveAndBelow (source, kNumSources) ? values[(size_t) source] : 0.0f;
}

float ModEngine::contribution (const PlanConnection& c) const noexcept
{
    float v = getSourceValue (c.source, c.lane);
    const bool bipolar = isSourceBipolar (c.source);

    if (c.polarity == Polarity::unipolar && bipolar)
        v = (v + 1.0f) * 0.5f;
    else if (c.polarity == Polarity::bipolar && ! bipolar)
        v = v * 2.0f - 1.0f;

    return c.depth * v + c.offset;
}

float ModEngine::sumContributions (const std::vector<PlanConnection>& conns) const noexcept
{
    float sum = 0.0f;
    for (auto& c : conns)
        sum += contribution (c);
    return sum;
}

float ModEngine::gatherInternal (int ip, const RoutingPlan& plan) noexcept
{
    const float m = sumContributions (plan.internal[(size_t) ip]);
    internalMod[(size_t) ip] = m;
    return m;
}

//==============================================================================
void ModEngine::runNode (int node, const EngineSettings& s, const TransportInfo& t, const RoutingPlan& plan,
                         int numSamples, const std::array<const float*, kNumLfos>& tables) noexcept
{
    static const auto lfoRange    = ParamRanges::lfoRate();
    static const auto randomRange = ParamRanges::randomRate();

    if (node >= nodeLfo0 && node < nodeRandom0)
    {
        const int i = node - nodeLfo0;
        const auto& ls = s.lfo[(size_t) i];

        const float rateMod  = gatherInternal (lfoInternal (i, 0), plan);
        const float depthMod = gatherInternal (lfoInternal (i, 1), plan);
        const float phaseMod = gatherInternal (lfoInternal (i, 2), plan);

        const float rateHz = lfoRange.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, lfoRange.convertTo0to1 (ls.rateHz) + rateMod));
        const float multiplier = ls.sync ? std::pow (2.0f, rateMod * 4.0f) : 1.0f;
        const float depth = juce::jlimit (0.0f, 1.0f, ls.depth + depthMod);

        values[(size_t) (srcLfo0 + i)] = lfos[(size_t) i].process (ls, t, numSamples, rateHz, multiplier, depth,
                                                                  ls.phaseOffset + phaseMod, tables[(size_t) i]);
        lfoPhaseDisplay[(size_t) i].store ((float) lfos[(size_t) i].getPhase(), std::memory_order_relaxed);
    }
    else if (node >= nodeRandom0 && node < nodeMacro0)
    {
        const int i = node - nodeRandom0;
        const auto& rs = s.random[(size_t) i];

        const float rateMod  = gatherInternal (randomInternal (i, 0), plan);
        const float depthMod = gatherInternal (randomInternal (i, 1), plan);

        const float rateHz = randomRange.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, randomRange.convertTo0to1 (rs.rateHz) + rateMod));
        const float multiplier = rs.sync ? std::pow (2.0f, rateMod * 4.0f) : 1.0f;
        const float depth = juce::jlimit (0.0f, 1.0f, rs.depth + depthMod);

        randoms[(size_t) i].process (rs, t, numSamples, rateHz, multiplier, depth);
        values[(size_t) (srcRandom0 + i)] = randoms[(size_t) i].getValue (0);
    }
    else if (node >= nodeMacro0 && node < nodeXy)
    {
        const int i = node - nodeMacro0;
        const float amount = juce::jlimit (0.0f, 1.0f, 1.0f + gatherInternal (ipMacroAmount0 + i, plan));

        // Light smoothing so stepped automation and MIDI CC don't zipper.
        const float coeff = 1.0f - (float) std::exp (-(double) numSamples / (sampleRate * 0.015));
        auto& m = macroSmoothed[(size_t) i];
        m += (juce::jlimit (0.0f, 1.0f, s.macro[(size_t) i]) - m) * coeff;

        values[(size_t) (srcMacro0 + i)] = m * amount;
    }
    else if (node == nodeXy)
    {
        const auto& xs = s.xy;
        const float x = juce::jlimit (0.0f, 1.0f, xs.x + gatherInternal (ipXyX, plan));
        const float y = juce::jlimit (0.0f, 1.0f, xs.y + gatherInternal (ipXyY, plan));

        if (! xyInitialised)
        {
            xySmoothX = x;
            xySmoothY = y;
            xyInitialised = true;
        }

        const float coeff = xs.glideMs <= 0.0f ? 1.0f
                          : 1.0f - (float) std::exp (-(double) numSamples / (sampleRate * xs.glideMs * 0.001));
        xySmoothX += (x - xySmoothX) * coeff;
        xySmoothY += (y - xySmoothY) * coeff;

        xyBipolar = xs.bipolar;

        auto relative = [] (float v, float centre)
        {
            centre = juce::jlimit (0.01f, 0.99f, centre);
            return v >= centre ? (v - centre) / (1.0f - centre) : (v - centre) / centre;
        };

        values[(size_t) srcXyX] = xs.bipolar ? relative (xySmoothX, xs.centerX) : xySmoothX;
        values[(size_t) srcXyY] = xs.bipolar ? relative (xySmoothY, xs.centerY) : xySmoothY;
        xyXDisplay.store (xySmoothX, std::memory_order_relaxed);
        xyYDisplay.store (xySmoothY, std::memory_order_relaxed);
    }
    else if (node >= nodeEnv0 && node < kNumNodes)
    {
        const int i = node - nodeEnv0;
        const auto& es = s.env[(size_t) i];

        envGainModDb[(size_t) i] = gatherInternal (envInternal (i, 0), plan) * 24.0f;
        const float depth = juce::jlimit (0.0f, 1.0f, es.depth + gatherInternal (envInternal (i, 1), plan));

        const float level = envs[(size_t) i].getLevel();
        values[(size_t) (srcEnv0 + i)] = level * depth;
        envLevelDisplay[(size_t) i].store (level, std::memory_order_relaxed);
    }
}

void ModEngine::computeSubBlock (const EngineSettings& s, const TransportInfo& t, const RoutingPlan& plan,
                                 int numSamples, const std::array<const float*, kNumLfos>& tables) noexcept
{
    for (int i = 0; i < kNumNodes; ++i)
        runNode (plan.nodeOrder[(size_t) i], s, t, plan, numSamples, tables);

    blendX = juce::jlimit (0.0f, 1.0f, s.blendX + gatherInternal (ipBlendX, plan));
    blendY = juce::jlimit (0.0f, 1.0f, s.blendY + gatherInternal (ipBlendY, plan));
    blendXDisplay.store (blendX, std::memory_order_relaxed);
    blendYDisplay.store (blendY, std::memory_order_relaxed);

    for (int i = 0; i < kNumSources; ++i)
        sourceDisplay[(size_t) i].store (values[(size_t) i], std::memory_order_relaxed);
}

//==============================================================================
std::array<float, kNumSnapshots> ModEngine::blendWeights (float x, float y) noexcept
{
    x = juce::jlimit (0.0f, 1.0f, x);
    y = juce::jlimit (0.0f, 1.0f, y);

    // A top-left, B top-right, C bottom-left, D bottom-right.
    return { (1.0f - x) * (1.0f - y), x * (1.0f - y), (1.0f - x) * y, x * y };
}

std::array<float, kNumSnapshots> ModEngine::getBlendWeights() const noexcept
{
    return blendWeights (blendX, blendY);
}

bool ModEngine::resolveBlend (const BlendEntry& e, const std::array<float, kNumSnapshots>& w, float& out) noexcept
{
    if (e.mask == 0)
        return false;

    if (e.discrete)
    {
        // Values that can't be interpolated switch to the nearest snapshot.
        int best = -1;
        float bestWeight = -1.0f;

        for (int i = 0; i < kNumSnapshots; ++i)
            if ((e.mask & (1 << i)) != 0 && w[(size_t) i] > bestWeight)
            {
                bestWeight = w[(size_t) i];
                best = i;
            }

        out = e.values[(size_t) best];
        return true;
    }

    float sum = 0.0f, total = 0.0f;

    for (int i = 0; i < kNumSnapshots; ++i)
        if ((e.mask & (1 << i)) != 0)
        {
            sum += w[(size_t) i] * e.values[(size_t) i];
            total += w[(size_t) i];
        }

    if (total <= 1.0e-6f)
    {
        // The position is entirely on snapshots that don't hold this
        // parameter: fall back to the average of the ones that do.
        int n = 0;
        sum = 0.0f;
        for (int i = 0; i < kNumSnapshots; ++i)
            if ((e.mask & (1 << i)) != 0) { sum += e.values[(size_t) i]; ++n; }

        out = sum / (float) n;
        return true;
    }

    out = sum / total;
    return true;
}

} // namespace mm
