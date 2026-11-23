#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Engine/ModEngine.h"
#include "Engine/ModMatrix.h"
#include "Core/RtSwap.h"
#include <cmath>

using namespace mm;

namespace
{

TransportInfo stopped()
{
    return {};
}

//==============================================================================
struct LfoTests final : juce::UnitTest
{
    LfoTests() : juce::UnitTest ("LFO", "ModdMudd") {}

    void runTest() override
    {
        beginTest ("Basic shapes");
        expectWithinAbsoluteError (Lfo::evaluate (LfoShape::sine, 0.25, nullptr), 1.0f, 1.0e-5f);
        expectWithinAbsoluteError (Lfo::evaluate (LfoShape::sine, 0.75, nullptr), -1.0f, 1.0e-5f);
        expectWithinAbsoluteError (Lfo::evaluate (LfoShape::triangle, 0.25, nullptr), 1.0f, 1.0e-5f);
        expectWithinAbsoluteError (Lfo::evaluate (LfoShape::triangle, 0.75, nullptr), -1.0f, 1.0e-5f);
        expectWithinAbsoluteError (Lfo::evaluate (LfoShape::triangle, 0.0, nullptr), 0.0f, 1.0e-5f);
        expectEquals (Lfo::evaluate (LfoShape::square, 0.1, nullptr), 1.0f);
        expectEquals (Lfo::evaluate (LfoShape::square, 0.6, nullptr), -1.0f);
        expectWithinAbsoluteError (Lfo::evaluate (LfoShape::saw, 0.0, nullptr), -1.0f, 1.0e-5f);
        expectWithinAbsoluteError (Lfo::evaluate (LfoShape::reverseSaw, 0.0, nullptr), 1.0f, 1.0e-5f);
        expectWithinAbsoluteError (Lfo::evaluate (LfoShape::sine, 1.25, nullptr), 1.0f, 1.0e-5f);   // wraps

        beginTest ("Free-running rate");
        {
            Lfo lfo;
            lfo.prepare (1000.0);
            LfoSettings s;
            s.rateHz = 1.0f;

            expectWithinAbsoluteError (lfo.process (s, stopped(), 250, 1.0f, 1.0f, 1.0f, 0.0f, nullptr), 0.0f, 1.0e-5f);
            expectWithinAbsoluteError (lfo.process (s, stopped(), 250, 1.0f, 1.0f, 1.0f, 0.0f, nullptr), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError (lfo.process (s, stopped(), 250, 1.0f, 1.0f, 1.0f, 0.0f, nullptr), 0.0f, 1.0e-4f);
            expectWithinAbsoluteError (lfo.process (s, stopped(), 250, 1.0f, 1.0f, 1.0f, 0.0f, nullptr), -1.0f, 1.0e-4f);
        }

        beginTest ("Tempo sync locks to song position");
        {
            Lfo lfo;
            lfo.prepare (48000.0);
            LfoSettings s;
            s.sync = true;
            s.division = 7;   // 1/4 note per cycle

            TransportInfo t;
            t.isPlaying = true;
            t.hasPpq = true;
            t.bpm = 120.0;

            t.ppq = 10.25;   // a quarter into the cycle
            expectWithinAbsoluteError (lfo.process (s, t, 32, 1.0f, 1.0f, 1.0f, 0.0f, nullptr), 1.0f, 1.0e-4f);

            t.ppq = 3.75;    // same result regardless of history
            expectWithinAbsoluteError (lfo.process (s, t, 32, 1.0f, 1.0f, 1.0f, 0.0f, nullptr), -1.0f, 1.0e-4f);
        }

        beginTest ("Depth, offset, invert and clamping");
        {
            Lfo lfo;
            lfo.prepare (1000.0);
            LfoSettings s;
            s.shape = LfoShape::square;
            s.offset = 0.5f;
            expectWithinAbsoluteError (lfo.process (s, stopped(), 1, 1.0f, 1.0f, 0.25f, 0.0f, nullptr), 0.75f, 1.0e-5f);

            lfo.reset();
            expectEquals (lfo.process (s, stopped(), 1, 1.0f, 1.0f, 1.0f, 0.0f, nullptr), 1.0f);   // 1.5 clamps to 1

            lfo.reset();
            s.offset = 0.0f;
            s.invert = true;
            expectEquals (lfo.process (s, stopped(), 1, 1.0f, 1.0f, 1.0f, 0.0f, nullptr), -1.0f);
        }

        beginTest ("Phase offset");
        {
            Lfo lfo;
            lfo.prepare (1000.0);
            LfoSettings s;
            expectWithinAbsoluteError (lfo.process (s, stopped(), 1, 1.0f, 1.0f, 1.0f, 0.25f, nullptr), 1.0f, 1.0e-5f);
        }

        beginTest ("Fade-in after retrigger");
        {
            Lfo lfo;
            lfo.prepare (1000.0);
            LfoSettings s;
            s.shape = LfoShape::square;
            s.retrigger = LfoRetrigger::transport;
            s.fadeInSeconds = 1.0f;

            TransportInfo t;
            t.isPlaying = true;
            t.playStarted = true;

            expectWithinAbsoluteError (lfo.process (s, t, 500, 0.1f, 1.0f, 1.0f, 0.0f, nullptr), 0.0f, 1.0e-5f);
            t.playStarted = false;
            expectWithinAbsoluteError (lfo.process (s, t, 500, 0.1f, 1.0f, 1.0f, 0.0f, nullptr), 0.5f, 1.0e-3f);
            expectWithinAbsoluteError (lfo.process (s, t, 500, 0.1f, 1.0f, 1.0f, 0.0f, nullptr), 1.0f, 1.0e-3f);
        }

        beginTest ("Custom waveform table");
        {
            std::vector<float> table ((size_t) kLfoTableSize);
            Lfo::buildCustomTable (Lfo::defaultCustomPoints(), table.data());

            expectWithinAbsoluteError (table[0], 0.0f, 1.0e-5f);
            expectWithinAbsoluteError (table[(size_t) kLfoTableSize / 4], 1.0f, 1.0e-5f);
            expectWithinAbsoluteError (table[(size_t) kLfoTableSize / 2], 0.0f, 1.0e-5f);
            expectWithinAbsoluteError (table[(size_t) kLfoTableSize * 3 / 4], -1.0f, 1.0e-5f);
            expect (table[(size_t) kLfoTableSize - 1] > -0.01f && table[(size_t) kLfoTableSize - 1] < 0.0f,
                    "last segment wraps back towards the first point");

            // Unsorted input and a curved segment.
            std::vector<WavePoint> pts { { 0.5f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.8f } };
            Lfo::buildCustomTable (pts, table.data());
            expectWithinAbsoluteError (table[0], 1.0f, 1.0e-5f);
            expectWithinAbsoluteError (table[(size_t) kLfoTableSize / 2], -1.0f, 1.0e-5f);
            expect (table[(size_t) kLfoTableSize / 4] > 0.0f, "positive curve keeps the value high for longer");
        }
    }
};

//==============================================================================
struct RandomTests final : juce::UnitTest
{
    RandomTests() : juce::UnitTest ("Randomizer", "ModdMudd") {}

    static std::vector<float> run (uint32_t seed, RandomSettings s, int steps, int lane = 0)
    {
        Randomizer r;
        r.prepare (1000.0);
        r.setSeed (seed);
        std::vector<float> out;
        for (int i = 0; i < steps; ++i)
        {
            r.process (s, stopped(), 50, s.rateHz, 1.0f, s.depth);
            out.push_back (r.getValue (lane));
        }
        return out;
    }

    void runTest() override
    {
        RandomSettings s;
        s.rateHz = 4.0f;

        beginTest ("Seeded and deterministic");
        expect (run (42, s, 200) == run (42, s, 200));
        expect (run (42, s, 200) != run (43, s, 200));

        beginTest ("Lanes are independent");
        expect (run (42, s, 200, 0) != run (42, s, 200, 3));

        beginTest ("Values stay in range");
        for (float v : run (7, s, 1000))
            expect (v >= -1.0f && v <= 1.0f);

        beginTest ("Probability 0 never moves");
        {
            auto p = s;
            p.probability = 0.0f;
            for (float v : run (7, p, 200))
                expectEquals (v, 0.0f);
        }

        beginTest ("Stepped mode with glide limits jumps");
        {
            auto p = s;
            p.smooth = false;
            p.glide = 0.5f;
            p.rateHz = 1.0f;   // 1000 samples per cycle; 50 samples per step
            const auto v = run (9, p, 400);
            float maxStep = 0.0f;
            for (size_t i = 1; i < v.size(); ++i)
                maxStep = juce::jmax (maxStep, std::abs (v[i] - v[i - 1]));

            // Full swing (2.0) spread over half a cycle (10 steps) is 0.2 per step.
            expect (maxStep <= 0.2001f, "max step " + juce::String (maxStep));
        }

        beginTest ("Synced randomizer repeats with the song position");
        {
            auto p = s;
            p.sync = true;
            p.division = 7;

            TransportInfo t;
            t.isPlaying = true;
            t.hasPpq = true;
            t.bpm = 120.0;

            Randomizer a, b;
            a.prepare (48000.0); b.prepare (48000.0);
            a.setSeed (5); b.setSeed (5);

            // a plays from bar 1, b jumps straight to beat 9.5
            for (int i = 0; i < 20; ++i)
            {
                t.ppq = i * 0.5;
                a.process (p, t, 32, 1.0f, 1.0f, 1.0f);
            }

            t.ppq = 9.5;
            b.process (p, t, 32, 1.0f, 1.0f, 1.0f);
            expectWithinAbsoluteError (a.getValue (0), b.getValue (0), 1.0e-5f);
        }
    }
};

//==============================================================================
struct EnvelopeTests final : juce::UnitTest
{
    EnvelopeTests() : juce::UnitTest ("Envelope follower", "ModdMudd") {}

    void runTest() override
    {
        constexpr double sr = 48000.0;

        beginTest ("Level mapping with threshold and sensitivity");
        {
            EnvelopeFollower env;
            env.prepare (sr);
            EnvSettings s;
            s.attackMs = 0.1f;
            s.releaseMs = 100.0f;
            s.thresholdDb = -12.0f;
            s.sensitivity = 0.0f;   // 60 dB range
            s.smoothMs = 0.1f;

            std::vector<float> buf (4800, 0.5f);   // DC at -6 dB
            const float* ch[] { buf.data() };
            env.processAudio (ch, 1, (int) buf.size(), s, 0.0f);

            // (-6 - -12) / 60 = 0.1
            expectWithinAbsoluteError (env.getLevel(), 0.1f, 0.005f);

            s.invert = true;
            env.processAudio (ch, 1, (int) buf.size(), s, 0.0f);
            expectWithinAbsoluteError (env.getLevel(), 0.9f, 0.005f);
        }

        beginTest ("Silence gives zero, loud signals saturate at one");
        {
            EnvelopeFollower env;
            env.prepare (sr);
            EnvSettings s;
            s.smoothMs = 0.1f;

            env.processAudio (nullptr, 0, 4800, s, 0.0f);
            expectEquals (env.getLevel(), 0.0f);

            std::vector<float> buf (4800, 1.0f);
            const float* ch[] { buf.data() };
            env.processAudio (ch, 1, (int) buf.size(), s, 0.0f);
            expectWithinAbsoluteError (env.getLevel(), 1.0f, 0.001f);
        }

        beginTest ("Attack time constant");
        {
            EnvelopeFollower env;
            env.prepare (sr);
            EnvSettings s;
            s.attackMs = 10.0f;
            s.thresholdDb = -60.0f;
            s.sensitivity = 0.0f;
            s.smoothMs = 0.1f;

            // Feed exactly one attack time of full-scale signal: the detector
            // should reach about 63% (-4 dB).
            std::vector<float> buf ((size_t) (sr * 0.010), 1.0f);
            const float* ch[] { buf.data() };
            env.processAudio (ch, 1, (int) buf.size(), s, 0.0f);

            const float expectedDb = juce::Decibels::gainToDecibels (1.0f - std::exp (-1.0f));
            expectWithinAbsoluteError (env.getLevel(), (expectedDb + 60.0f) / 60.0f, 0.01f);
        }

        beginTest ("RMS vs peak");
        {
            std::vector<float> sine (48000);
            for (size_t i = 0; i < sine.size(); ++i)
                sine[i] = std::sin ((float) i * 0.05f);
            const float* ch[] { sine.data() };

            EnvSettings s;
            s.attackMs = 50.0f;
            s.releaseMs = 50.0f;
            s.thresholdDb = -20.0f;
            s.sensitivity = 0.0f;
            s.smoothMs = 0.1f;

            EnvelopeFollower peak, rms;
            peak.prepare (sr);
            rms.prepare (sr);
            auto peakSettings = s;
            peakSettings.attackMs = 0.5f;      // a peak detector: fast attack, slow release
            peakSettings.releaseMs = 1000.0f;
            peak.processAudio (ch, 1, (int) sine.size(), peakSettings, 0.0f);
            s.detector = EnvDetector::rms;
            rms.processAudio (ch, 1, (int) sine.size(), s, 0.0f);

            expect (rms.getLevel() < peak.getLevel(), juce::String (rms.getLevel()) + " vs " + juce::String (peak.getLevel()));
            expectWithinAbsoluteError (peak.getLevel(), 20.0f / 60.0f, 0.01f);                   // sine peak is 0 dB
            expectWithinAbsoluteError (rms.getLevel(), (-3.01f + 20.0f) / 60.0f, 0.01f);   // sine RMS is -3 dB
        }
    }
};

//==============================================================================
struct MatrixTests final : juce::UnitTest
{
    MatrixTests() : juce::UnitTest ("Modulation matrix", "ModdMudd") {}

    static Connection conn (int source, Destination d, float depth = 0.5f)
    {
        Connection c;
        c.source = source;
        c.dest = d;
        c.depth = depth;
        return c;
    }

    void runTest() override
    {
        beginTest ("Cross-modulation and cycle prevention");
        {
            ModMatrix m;
            expect (m.add (conn (srcLfo0, Destination::internalParam (lfoInternal (1, 0)))) == ModMatrix::AddResult::added);
            expect (m.add (conn (srcLfo0 + 1, Destination::internalParam (lfoInternal (0, 0)))) == ModMatrix::AddResult::cycle);
            expect (m.add (conn (srcLfo0, Destination::internalParam (lfoInternal (0, 1)))) == ModMatrix::AddResult::cycle, "self-modulation");

            // Longer loop: LFO2 -> Env1 gain, Env1 -> LFO1 depth closes LFO1 -> LFO2 -> Env1 -> LFO1.
            expect (m.add (conn (srcLfo0 + 1, Destination::internalParam (envInternal (0, 0)))) == ModMatrix::AddResult::added);
            expect (m.add (conn (srcEnv0, Destination::internalParam (lfoInternal (0, 1)))) == ModMatrix::AddResult::cycle);

            // XY X and XY Y are the same node.
            expect (m.add (conn (srcXyX, Destination::internalParam (ipXyY))) == ModMatrix::AddResult::cycle);

            // The blender has no outputs, so anything can drive it.
            expect (m.add (conn (srcXyX, Destination::internalParam (ipBlendX))) == ModMatrix::AddResult::added);

            std::array<int, kNumNodes> order {};
            expect (m.computeNodeOrder (order));
            auto posOf = [&] (int node) { return (int) (std::find (order.begin(), order.end(), node) - order.begin()); };
            expect (posOf (nodeLfo0) < posOf (nodeLfo0 + 1));
            expect (posOf (nodeLfo0 + 1) < posOf (nodeEnv0));
        }

        beginTest ("Duplicates and invalid connections");
        {
            ModMatrix m;
            const auto d = Destination::hosted ("slot", "42");
            expect (m.add (conn (srcMacro0, d)) == ModMatrix::AddResult::added);
            expect (m.add (conn (srcMacro0, d)) == ModMatrix::AddResult::duplicate);
            expect (m.add (conn (srcMacro0 + 1, d)) == ModMatrix::AddResult::added, "several sources per destination");
            expect (m.add (conn (99, d)) == ModMatrix::AddResult::invalid);
            expect (m.add (conn (srcMacro0, Destination::hosted ("", "1"))) == ModMatrix::AddResult::invalid);
            expectEquals ((int) m.getConnectionsTo (d).size(), 2);
        }

        beginTest ("Update can't introduce a cycle");
        {
            ModMatrix m;
            int a = 0, b = 0;
            m.add (conn (srcLfo0, Destination::internalParam (lfoInternal (1, 0))), &a);
            m.add (conn (srcLfo0 + 2, Destination::internalParam (lfoInternal (0, 0))), &b);

            auto c = *m.find (b);
            c.source = srcLfo0 + 1;   // LFO2 -> LFO1 rate would close the loop
            expect (! m.update (c));
            c.depth = -0.25f;
            c.source = srcLfo0 + 2;
            expect (m.update (c));
            expectEquals (m.find (b)->depth, -0.25f);
        }

        beginTest ("Serialisation round trip");
        {
            ModMatrix m;
            auto c = conn (srcRandom0, Destination::hosted ("abc", "7"), -0.3f);
            c.offset = 0.1f;
            c.polarity = Polarity::unipolar;
            c.enabled = false;
            m.add (c);
            m.add (conn (srcLfo0, Destination::internalParam (ipBlendY)));
            m.setSettings (Destination::hosted ("abc", "7"), { 0.2f, 0.8f, 40.0f });

            ModMatrix copy;
            copy.fromValueTree (m.toValueTree());
            expectEquals ((int) copy.getConnections().size(), 2);

            const auto& r = copy.getConnections()[0];
            expectEquals (r.source, (int) srcRandom0);
            expectEquals (r.dest.slotUid, juce::String ("abc"));
            expectWithinAbsoluteError (r.depth, -0.3f, 1.0e-6f);
            expectWithinAbsoluteError (r.offset, 0.1f, 1.0e-6f);
            expect (r.polarity == Polarity::unipolar);
            expect (! r.enabled);

            const auto st = copy.getSettings (Destination::hosted ("abc", "7"));
            expectWithinAbsoluteError (st.rangeMin, 0.2f, 1.0e-6f);
            expectWithinAbsoluteError (st.rangeMax, 0.8f, 1.0e-6f);
            expectWithinAbsoluteError (st.smoothingMs, 40.0f, 1.0e-6f);
        }
    }
};

//==============================================================================
struct EngineTests final : juce::UnitTest
{
    EngineTests() : juce::UnitTest ("Modulation engine", "ModdMudd") {}

    void runTest() override
    {
        std::array<const float*, kNumLfos> noTables {};

        beginTest ("Polarity mapping");
        {
            ModEngine e;
            e.prepare (48000.0);
            EngineSettings s;
            s.macro[0] = 1.0f;
            RoutingPlan plan;

            for (int i = 0; i < 50; ++i)   // let macro smoothing settle
                e.computeSubBlock (s, stopped(), plan, 512, noTables);

            PlanConnection c;
            c.source = srcMacro0;
            c.depth = 1.0f;

            expectWithinAbsoluteError (e.contribution (c), 1.0f, 1.0e-3f);
            c.polarity = Polarity::bipolar;
            expectWithinAbsoluteError (e.contribution (c), 1.0f, 1.0e-3f);
            c.depth = -0.5f;
            expectWithinAbsoluteError (e.contribution (c), -0.5f, 1.0e-3f);
            c.offset = 0.25f;
            expectWithinAbsoluteError (e.contribution (c), -0.25f, 1.0e-3f);
        }

        beginTest ("Range clamping and summing sources");
        {
            ModEngine e;
            e.prepare (48000.0);
            EngineSettings s;
            s.macro[0] = 1.0f;
            s.macro[1] = 1.0f;
            RoutingPlan plan;
            for (int i = 0; i < 50; ++i)
                e.computeSubBlock (s, stopped(), plan, 512, noTables);

            HostedTarget t;
            t.rangeMin = 0.1f;
            t.rangeMax = 0.7f;
            t.connections.push_back ({ srcMacro0, 0, 0.2f, 0.0f, Polarity::natural });
            t.connections.push_back ({ srcMacro0 + 1, 0, 0.2f, 0.0f, Polarity::natural });

            expectWithinAbsoluteError (e.evaluate (0.2f, t), 0.6f, 1.0e-3f);
            expectWithinAbsoluteError (e.evaluate (0.5f, t), 0.7f, 1.0e-6f);
            t.connections[0].depth = -1.0f;
            t.connections[1].depth = -1.0f;
            expectWithinAbsoluteError (e.evaluate (0.5f, t), 0.1f, 1.0e-6f);
        }

        beginTest ("Cross-modulation: one LFO controls another's depth");
        {
            ModEngine e;
            e.prepare (1000.0);
            EngineSettings s;
            s.lfo[0].shape = LfoShape::square;   // +1 for the first half cycle
            s.lfo[0].rateHz = 0.1f;
            s.lfo[1].shape = LfoShape::square;
            s.lfo[1].rateHz = 0.1f;

            ModMatrix m;
            Connection c;
            c.source = srcLfo0;
            c.dest = Destination::internalParam (lfoInternal (1, 1));
            c.depth = -1.0f;
            m.add (c);

            RoutingPlan plan;
            expect (m.computeNodeOrder (plan.nodeOrder));
            plan.internal[(size_t) lfoInternal (1, 1)].push_back ({ srcLfo0, 0, -1.0f, 0.0f, Polarity::natural });

            e.computeSubBlock (s, stopped(), plan, 10, noTables);
            expectEquals (e.getSourceValue (srcLfo0), 1.0f);
            expectEquals (e.getSourceValue (srcLfo0 + 1), 0.0f);   // depth 1 + (-1)

            // Run LFO 2 before LFO 1 to show the ordering matters: with the
            // wrong order it would see last sub-block's value.
            expect (plan.nodeOrder[0] == nodeLfo0);
        }

        beginTest ("XY pad centre and bipolar output");
        {
            ModEngine e;
            e.prepare (48000.0);
            EngineSettings s;
            s.xy.glideMs = 0.0f;
            s.xy.x = 0.75f;
            s.xy.y = 0.25f;
            s.xy.centerX = 0.5f;
            s.xy.centerY = 0.5f;
            RoutingPlan plan;
            e.computeSubBlock (s, stopped(), plan, 32, noTables);
            expectWithinAbsoluteError (e.getSourceValue (srcXyX), 0.5f, 1.0e-5f);
            expectWithinAbsoluteError (e.getSourceValue (srcXyY), -0.5f, 1.0e-5f);

            s.xy.centerX = 0.25f;   // asymmetric centre: right side spans 0.75
            s.xy.x = 1.0f;
            e.computeSubBlock (s, stopped(), plan, 32, noTables);
            expectWithinAbsoluteError (e.getSourceValue (srcXyX), 1.0f, 1.0e-5f);

            s.xy.bipolar = false;
            s.xy.x = 0.3f;
            e.computeSubBlock (s, stopped(), plan, 32, noTables);
            expectWithinAbsoluteError (e.getSourceValue (srcXyX), 0.3f, 1.0e-5f);
            expect (! e.isSourceBipolar (srcXyX));
        }

        beginTest ("Preset blender weights and interpolation");
        {
            auto w = ModEngine::blendWeights (0.0f, 0.0f);
            expectEquals (w[0], 1.0f);
            w = ModEngine::blendWeights (0.5f, 0.5f);
            for (float x : w)
                expectWithinAbsoluteError (x, 0.25f, 1.0e-6f);

            BlendEntry e;
            e.values = { 0.0f, 1.0f, 0.0f, 1.0f };
            e.mask = 0b1111;
            float out = -1.0f;
            expect (ModEngine::resolveBlend (e, ModEngine::blendWeights (0.25f, 0.6f), out));
            expectWithinAbsoluteError (out, 0.25f, 1.0e-6f);

            e.discrete = true;
            expect (ModEngine::resolveBlend (e, ModEngine::blendWeights (0.4f, 0.0f), out));
            expectEquals (out, 0.0f);
            expect (ModEngine::resolveBlend (e, ModEngine::blendWeights (0.6f, 0.0f), out));
            expectEquals (out, 1.0f);

            // Only A and D captured.
            BlendEntry partial;
            partial.values = { 0.2f, 0.0f, 0.0f, 0.8f };
            partial.mask = 0b1001;
            expect (ModEngine::resolveBlend (partial, ModEngine::blendWeights (0.5f, 0.5f), out));
            expectWithinAbsoluteError (out, 0.5f, 1.0e-6f);
            expect (ModEngine::resolveBlend (partial, ModEngine::blendWeights (1.0f, 0.0f), out));   // entirely on B
            expectWithinAbsoluteError (out, 0.5f, 1.0e-6f);

            BlendEntry none;
            expect (! ModEngine::resolveBlend (none, w, out));
        }
    }
};

//==============================================================================
struct RtSwapTests final : juce::UnitTest
{
    RtSwapTests() : juce::UnitTest ("RtSwap", "ModdMudd") {}

    struct Tracked
    {
        explicit Tracked (int& c, int v) : counter (c), value (v) { ++counter; }
        ~Tracked() { --counter; }
        int& counter;
        int value;
        uint64_t generation = 0;
    };

    void runTest() override
    {
        beginTest ("Publish, acquire and garbage collection");
        int alive = 0;
        {
            RtSwap<Tracked> swap;
            expect (swap.acquire() == nullptr);

            swap.publish (std::make_unique<Tracked> (alive, 1));
            swap.publish (std::make_unique<Tracked> (alive, 2));   // replaces the unconsumed one
            expectEquals (alive, 1);

            auto* t = swap.acquire();
            expectEquals (t->value, 2);
            expectEquals ((int) swap.getAcknowledgedGeneration(), 2);

            swap.publish (std::make_unique<Tracked> (alive, 3));
            expectEquals (swap.acquire()->value, 3);
            expectEquals (alive, 2);   // old one waits in the retire queue

            swap.collectGarbage();
            expectEquals (alive, 1);
            expect (swap.acquire() == swap.get());
        }
        expectEquals (alive, 0);

        beginTest ("Full retire queue holds the current object instead of blocking");
        {
            RtSwap<Tracked> swap;
            for (int i = 0; i < 40; ++i)
            {
                swap.publish (std::make_unique<Tracked> (alive, i));
                swap.acquire();
            }
            expect (swap.get()->value < 39, "queue filled up, newest not yet taken");
            swap.collectGarbage();
            expectEquals (swap.acquire()->value, 39);
        }
        expectEquals (alive, 0);
    }
};

//==============================================================================
struct ShaperTests final : juce::UnitTest
{
    ShaperTests() : juce::UnitTest ("Shaper", "ModdMudd") {}

    void runTest() override
    {
        // A triangle from 0 up to 1 (half way) and back to 0.
        std::vector<float> table ((size_t) kLfoTableSize);
        Lfo::buildCustomTable ({ { 0.0f, -1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, -1.0f, 0.0f } }, table.data());

        ShaperSettings s;
        TransportInfo t;

        beginTest ("One-shot on note-on, holds the end value afterwards");
        {
            Shaper sh;
            sh.prepare (1000.0);
            expectWithinAbsoluteError (sh.process (s, t, 100, 1.0, true, 1.0f, table.data()), 0.0f, 1.0e-3f);   // idle
            expect (! sh.isRunning());

            t.noteOn = true;
            expectWithinAbsoluteError (sh.process (s, t, 100, 1.0, true, 1.0f, table.data()), 0.0f, 1.0e-3f);   // start
            t.noteOn = false;
            expect (sh.isRunning());

            for (int i = 0; i < 4; ++i)
                sh.process (s, t, 100, 1.0, true, 1.0f, table.data());
            expectWithinAbsoluteError (sh.process (s, t, 100, 1.0, true, 1.0f, table.data()), 1.0f, 1.0e-2f);    // peak at 0.5 s

            for (int i = 0; i < 6; ++i)
                sh.process (s, t, 100, 1.0, true, 1.0f, table.data());
            expect (! sh.isRunning());
            expectWithinAbsoluteError (sh.process (s, t, 100, 1.0, true, 1.0f, table.data()), 0.0f, 1.0e-2f);
        }

        beginTest ("Depth scales the output");
        {
            Shaper sh;
            sh.prepare (1000.0);
            sh.trigger();
            sh.process (s, t, 500, 1.0, true, 0.5f, table.data());
            expectWithinAbsoluteError (sh.process (s, t, 1, 1.0, true, 0.5f, table.data()), 0.5f, 1.0e-2f);
        }

        beginTest ("Note-on only starts note-triggered shapers");
        {
            auto ts = s;
            ts.trigger = ShaperTrigger::transport;
            Shaper sh;
            sh.prepare (1000.0);
            t.noteOn = true;
            sh.process (ts, t, 10, 1.0, true, 1.0f, table.data());
            t.noteOn = false;
            expect (! sh.isRunning());

            t.playStarted = true;
            sh.process (ts, t, 10, 1.0, true, 1.0f, table.data());
            t.playStarted = false;
            expect (sh.isRunning());
        }

        beginTest ("Audio transients trigger, with hysteresis and hold-off");
        {
            auto as = s;
            as.trigger = ShaperTrigger::audio;
            as.thresholdDb = -12.0f;

            Shaper sh;
            sh.prepare (48000.0);

            std::vector<float> quiet (480, 0.01f), loud (480, 0.9f);
            const float* q[] { quiet.data() };
            const float* l[] { loud.data() };

            sh.feedAudio (q, 1, 480, as.thresholdDb);
            sh.process (as, t, 480, 0.2, true, 1.0f, table.data());
            expect (! sh.isRunning(), "quiet input doesn't trigger");

            sh.feedAudio (l, 1, 480, as.thresholdDb);
            sh.process (as, t, 480, 0.2, true, 1.0f, table.data());
            expect (sh.isRunning(), "a transient triggers");

            // Staying loud doesn't re-trigger: the envelope keeps running.
            const double before = sh.getPhase();
            sh.feedAudio (l, 1, 480, as.thresholdDb);
            sh.process (as, t, 480, 0.2, true, 1.0f, table.data());
            expect (sh.getPhase() > before);
        }

        beginTest ("Loop mode repeats and locks to the song position when synced");
        {
            auto ls = s;
            ls.trigger = ShaperTrigger::loop;
            Shaper sh;
            sh.prepare (1000.0);

            float maxV = 0.0f;
            for (int i = 0; i < 30; ++i)
                maxV = juce::jmax (maxV, sh.process (ls, t, 100, 1.0, true, 1.0f, table.data()));
            expect (sh.isRunning());
            expect (maxV > 0.95f, "reaches the peak repeatedly");

            ls.sync = true;
            ls.division = 7;   // 1/4 note
            TransportInfo playing;
            playing.isPlaying = true;
            playing.hasPpq = true;
            playing.ppq = 12.5;   // half way through a beat -> peak
            expectWithinAbsoluteError (sh.process (ls, playing, 32, 0.5, true, 1.0f, table.data()), 1.0f, 1.0e-2f);
        }

        beginTest ("Default curve starts and ends at zero");
        {
            Lfo::buildCustomTable (Shaper::defaultPoints(), table.data());
            expectWithinAbsoluteError (Shaper::valueAt (table.data(), 0.0), 0.0f, 1.0e-3f);
            expectWithinAbsoluteError (Shaper::valueAt (table.data(), 1.0), 0.0f, 1.0e-2f);
            expect (Shaper::valueAt (table.data(), 0.06) > 0.95f);
        }
    }
};

static ShaperTests shaperTests;

//==============================================================================
struct ModuleTests final : juce::UnitTest
{
    ModuleTests() : juce::UnitTest ("Modules", "ModdMudd") {}

    void runTest() override
    {
        beginTest ("Which sources and targets each module owns");
        expect (moduleOwnsSource (modLfo0 + 2, srcLfo0 + 2));
        expect (! moduleOwnsSource (modLfo0 + 2, srcLfo0 + 1));
        expect (moduleOwnsSource (modMacros, srcMacro0 + 7));
        expect (moduleOwnsSource (modXy, srcXyY));
        expect (! moduleOwnsSource (modBlend, srcXyX));
        expect (moduleOwnsSource (modEnv0 + 1, srcEnv0 + 1));

        expect (moduleOwnsInternal (modLfo0 + 1, lfoInternal (1, 2)));
        expect (! moduleOwnsInternal (modLfo0, lfoInternal (1, 0)));
        expect (moduleOwnsInternal (modRandom0 + 1, randomInternal (1, 1)));
        expect (moduleOwnsInternal (modMacros, ipMacroAmount0 + 5));
        expect (moduleOwnsInternal (modXy, ipXyX));
        expect (moduleOwnsInternal (modBlend, ipBlendY));
        expect (moduleOwnsInternal (modEnv0, envInternal (0, 0)));

        beginTest ("Every source and target belongs to exactly one module");
        for (int src = 0; src < kNumSources; ++src)
        {
            int owners = 0;
            for (int m = 0; m < kNumModules; ++m)
                owners += moduleOwnsSource (m, src) ? 1 : 0;
            expectEquals (owners, 1, getSourceName (src));
        }

        for (int ip = 0; ip < kNumInternalParams; ++ip)
        {
            int owners = 0;
            for (int m = 0; m < kNumModules; ++m)
                owners += moduleOwnsInternal (m, ip) ? 1 : 0;
            expectEquals (owners, 1, getInternalParamName (ip));
        }
    }
};

static ModuleTests moduleTests;
static LfoTests lfoTests;
static RandomTests randomTests;
static EnvelopeTests envelopeTests;
static MatrixTests matrixTests;
static EngineTests engineTests;
static RtSwapTests rtSwapTests;

} // namespace
