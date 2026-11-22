#include "Parameters.h"

namespace mm
{

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;

static juce::ParameterID pid (const juce::String& id)   { return { id, 1 }; }

static juce::AudioParameterFloatAttributes withLabel (const char* label)
{
    return juce::AudioParameterFloatAttributes().withLabel (label);
}

static juce::AudioParameterFloatAttributes percent()
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; });
}

static juce::AudioParameterFloatAttributes bipolarPercent()
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; });
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (std::array<RemappableParameter*, kNumSlotParams>& slotParams)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Macros
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("macros", "Macros", "|");
        for (int i = 0; i < kNumMacros; ++i)
            group->addChild (std::make_unique<APF> (pid (ids::macro (i)), "Macro " + juce::String (i + 1),
                                                    juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent()));
        layout.add (std::move (group));
    }

    // XY pad
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("xy", "XY Pad", "|");
        group->addChild (std::make_unique<APF> (pid (ids::xyX), "XY X", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent()));
        group->addChild (std::make_unique<APF> (pid (ids::xyY), "XY Y", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent()));
        group->addChild (std::make_unique<APF> (pid (ids::xyCenterX), "XY Centre X", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent()));
        group->addChild (std::make_unique<APF> (pid (ids::xyCenterY), "XY Centre Y", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent()));
        group->addChild (std::make_unique<APF> (pid (ids::xyGlide), "XY Glide", ParamRanges::timeMs (0.0f, 2000.0f, 100.0f), 30.0f, withLabel ("ms")));
        group->addChild (std::make_unique<APB> (pid (ids::xyBipolar), "XY Bipolar", true));
        layout.add (std::move (group));
    }

    // LFOs
    const juce::StringArray shapes { "Sine", "Triangle", "Square", "Saw", "Reverse Saw", "Custom" };
    const juce::StringArray retrig { "Free", "Transport", "Note" };

    for (int i = 0; i < kNumLfos; ++i)
    {
        const auto n = "LFO " + juce::String (i + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("lfo" + juce::String (i + 1), "LFO " + juce::String (i + 1), "|");
        group->addChild (std::make_unique<APC> (pid (ids::lfo (i, "shape")), n + "Shape", shapes, 0));
        group->addChild (std::make_unique<APB> (pid (ids::lfo (i, "sync")), n + "Sync", false));
        group->addChild (std::make_unique<APC> (pid (ids::lfo (i, "div")), n + "Division", getDivisionNames(), kDefaultDivision));
        group->addChild (std::make_unique<APF> (pid (ids::lfo (i, "rate")), n + "Rate", ParamRanges::lfoRate(), 1.0f, withLabel ("Hz")));
        group->addChild (std::make_unique<APF> (pid (ids::lfo (i, "phase")), n + "Phase", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent()));
        group->addChild (std::make_unique<APF> (pid (ids::lfo (i, "depth")), n + "Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent()));
        group->addChild (std::make_unique<APF> (pid (ids::lfo (i, "offset")), n + "Offset", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bipolarPercent()));
        group->addChild (std::make_unique<APB> (pid (ids::lfo (i, "invert")), n + "Invert", false));
        group->addChild (std::make_unique<APC> (pid (ids::lfo (i, "retrig")), n + "Retrigger", retrig, 0));
        group->addChild (std::make_unique<APF> (pid (ids::lfo (i, "fade")), n + "Fade In", ParamRanges::timeMs (0.0f, 10.0f, 1.0f), 0.0f, withLabel ("s")));
        layout.add (std::move (group));
    }

    // Randomizers
    for (int i = 0; i < kNumRandoms; ++i)
    {
        const auto n = "Random " + juce::String (i + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("rnd" + juce::String (i + 1), "Random " + juce::String (i + 1), "|");
        group->addChild (std::make_unique<APB> (pid (ids::rnd (i, "sync")), n + "Sync", false));
        group->addChild (std::make_unique<APC> (pid (ids::rnd (i, "div")), n + "Division", getDivisionNames(), kDefaultDivision));
        group->addChild (std::make_unique<APF> (pid (ids::rnd (i, "rate")), n + "Rate", ParamRanges::randomRate(), 2.0f, withLabel ("Hz")));
        group->addChild (std::make_unique<APB> (pid (ids::rnd (i, "smooth")), n + "Smooth", true));
        group->addChild (std::make_unique<APF> (pid (ids::rnd (i, "prob")), n + "Probability", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent()));
        group->addChild (std::make_unique<APF> (pid (ids::rnd (i, "glide")), n + "Glide", juce::NormalisableRange<float> (0.0f, 1.0f), 0.1f, percent()));
        group->addChild (std::make_unique<APF> (pid (ids::rnd (i, "depth")), n + "Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent()));
        group->addChild (std::make_unique<APF> (pid (ids::rnd (i, "offset")), n + "Offset", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bipolarPercent()));
        group->addChild (std::make_unique<APB> (pid (ids::rnd (i, "indep")), n + "Independent", false));
        layout.add (std::move (group));
    }

    // Envelope followers
    const juce::StringArray envSources { "Input", "Output", "Sidechain" };
    const juce::StringArray detectors { "Peak", "RMS" };

    for (int i = 0; i < kNumEnvs; ++i)
    {
        const auto n = "Env " + juce::String (i + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("env" + juce::String (i + 1), "Envelope " + juce::String (i + 1), "|");
        group->addChild (std::make_unique<APC> (pid (ids::env (i, "source")), n + "Source", envSources, 0));
        group->addChild (std::make_unique<APC> (pid (ids::env (i, "detector")), n + "Detector", detectors, 0));
        group->addChild (std::make_unique<APF> (pid (ids::env (i, "attack")), n + "Attack", ParamRanges::timeMs (0.1f, 1000.0f, 20.0f), 10.0f, withLabel ("ms")));
        group->addChild (std::make_unique<APF> (pid (ids::env (i, "release")), n + "Release", ParamRanges::timeMs (1.0f, 5000.0f, 200.0f), 150.0f, withLabel ("ms")));
        group->addChild (std::make_unique<APF> (pid (ids::env (i, "gain")), n + "Gain", juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f, withLabel ("dB")));
        group->addChild (std::make_unique<APF> (pid (ids::env (i, "threshold")), n + "Threshold", juce::NormalisableRange<float> (-90.0f, 0.0f), -48.0f, withLabel ("dB")));
        group->addChild (std::make_unique<APF> (pid (ids::env (i, "sens")), n + "Sensitivity", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent()));
        group->addChild (std::make_unique<APF> (pid (ids::env (i, "curve")), n + "Curve", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bipolarPercent()));
        group->addChild (std::make_unique<APF> (pid (ids::env (i, "smooth")), n + "Smoothing", ParamRanges::timeMs (0.1f, 1000.0f, 30.0f), 10.0f, withLabel ("ms")));
        group->addChild (std::make_unique<APF> (pid (ids::env (i, "depth")), n + "Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent()));
        group->addChild (std::make_unique<APB> (pid (ids::env (i, "invert")), n + "Invert", false));
        layout.add (std::move (group));
    }

    // Preset blender
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("blend", "Blender", "|");
        group->addChild (std::make_unique<APB> (pid (ids::blendOn), "Blend On", false));
        group->addChild (std::make_unique<APF> (pid (ids::blendX), "Blend X", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent()));
        group->addChild (std::make_unique<APF> (pid (ids::blendY), "Blend Y", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent()));
        layout.add (std::move (group));
    }

    layout.add (std::make_unique<APF> (pid (ids::outputGain), "Output Gain", juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, withLabel ("dB")));

    // Remappable slots for hosted parameters
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("hosted", "Hosted", "|");
        for (int i = 0; i < kNumSlotParams; ++i)
        {
            auto p = std::make_unique<RemappableParameter> (i);
            slotParams[(size_t) i] = p.get();
            group->addChild (std::move (p));
        }
        layout.add (std::move (group));
    }

    return layout;
}

void ParameterRefs::bind (juce::AudioProcessorValueTreeState& s)
{
    auto get = [&s] (const juce::String& id)
    {
        auto* p = s.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    for (int i = 0; i < kNumLfos; ++i)
    {
        auto& l = lfo[(size_t) i];
        l.shape = get (ids::lfo (i, "shape")); l.sync = get (ids::lfo (i, "sync")); l.division = get (ids::lfo (i, "div"));
        l.rate = get (ids::lfo (i, "rate")); l.phase = get (ids::lfo (i, "phase")); l.depth = get (ids::lfo (i, "depth"));
        l.offset = get (ids::lfo (i, "offset")); l.invert = get (ids::lfo (i, "invert")); l.retrigger = get (ids::lfo (i, "retrig"));
        l.fade = get (ids::lfo (i, "fade"));
    }

    for (int i = 0; i < kNumRandoms; ++i)
    {
        auto& r = random[(size_t) i];
        r.sync = get (ids::rnd (i, "sync")); r.division = get (ids::rnd (i, "div")); r.rate = get (ids::rnd (i, "rate"));
        r.smooth = get (ids::rnd (i, "smooth")); r.probability = get (ids::rnd (i, "prob")); r.glide = get (ids::rnd (i, "glide"));
        r.depth = get (ids::rnd (i, "depth")); r.offset = get (ids::rnd (i, "offset")); r.independent = get (ids::rnd (i, "indep"));
    }

    for (int i = 0; i < kNumEnvs; ++i)
    {
        auto& e = env[(size_t) i];
        e.source = get (ids::env (i, "source")); e.detector = get (ids::env (i, "detector")); e.attack = get (ids::env (i, "attack"));
        e.release = get (ids::env (i, "release")); e.gain = get (ids::env (i, "gain")); e.threshold = get (ids::env (i, "threshold"));
        e.sensitivity = get (ids::env (i, "sens")); e.curve = get (ids::env (i, "curve")); e.smooth = get (ids::env (i, "smooth"));
        e.depth = get (ids::env (i, "depth")); e.invert = get (ids::env (i, "invert"));
    }

    for (int i = 0; i < kNumMacros; ++i)
        macro[(size_t) i] = get (ids::macro (i));

    xyX = get (ids::xyX); xyY = get (ids::xyY); xyCenterX = get (ids::xyCenterX); xyCenterY = get (ids::xyCenterY);
    xyGlide = get (ids::xyGlide); xyBipolar = get (ids::xyBipolar);
    blendOn = get (ids::blendOn); blendX = get (ids::blendX); blendY = get (ids::blendY);
    outputGain = get (ids::outputGain);
}

static float ld (const std::atomic<float>* p) noexcept   { return p->load (std::memory_order_relaxed); }
static bool  on (const std::atomic<float>* p) noexcept   { return ld (p) >= 0.5f; }
static int   ch (const std::atomic<float>* p) noexcept   { return juce::roundToInt (ld (p)); }

void ParameterRefs::fill (EngineSettings& s) const noexcept
{
    for (int i = 0; i < kNumLfos; ++i)
    {
        auto& r = lfo[(size_t) i];
        auto& o = s.lfo[(size_t) i];
        o.shape = (LfoShape) juce::jlimit (0, (int) LfoShape::numShapes - 1, ch (r.shape));
        o.sync = on (r.sync);
        o.division = ch (r.division);
        o.rateHz = ld (r.rate);
        o.phaseOffset = ld (r.phase);
        o.depth = ld (r.depth);
        o.offset = ld (r.offset);
        o.invert = on (r.invert);
        o.retrigger = (LfoRetrigger) juce::jlimit (0, 2, ch (r.retrigger));
        o.fadeInSeconds = ld (r.fade);
    }

    for (int i = 0; i < kNumRandoms; ++i)
    {
        auto& r = random[(size_t) i];
        auto& o = s.random[(size_t) i];
        o.sync = on (r.sync);
        o.division = ch (r.division);
        o.rateHz = ld (r.rate);
        o.smooth = on (r.smooth);
        o.probability = ld (r.probability);
        o.glide = ld (r.glide);
        o.depth = ld (r.depth);
        o.offset = ld (r.offset);
    }

    for (int i = 0; i < kNumEnvs; ++i)
    {
        auto& r = env[(size_t) i];
        auto& o = s.env[(size_t) i];
        o.source = (EnvSource) juce::jlimit (0, 2, ch (r.source));
        o.detector = (EnvDetector) juce::jlimit (0, 1, ch (r.detector));
        o.attackMs = ld (r.attack);
        o.releaseMs = ld (r.release);
        o.gainDb = ld (r.gain);
        o.thresholdDb = ld (r.threshold);
        o.sensitivity = ld (r.sensitivity);
        o.curve = ld (r.curve);
        o.smoothMs = ld (r.smooth);
        o.depth = ld (r.depth);
        o.invert = on (r.invert);
    }

    for (int i = 0; i < kNumMacros; ++i)
        s.macro[(size_t) i] = ld (macro[(size_t) i]);

    s.xy.x = ld (xyX); s.xy.y = ld (xyY);
    s.xy.centerX = ld (xyCenterX); s.xy.centerY = ld (xyCenterY);
    s.xy.glideMs = ld (xyGlide); s.xy.bipolar = on (xyBipolar);

    s.blendEnabled = on (blendOn);
    s.blendX = ld (blendX);
    s.blendY = ld (blendY);
}

} // namespace mm
