#include "PluginProcessor.h"
#include "../UI/PluginEditor.h"
#include <cmath>

namespace mm
{

juce::AudioProcessor::BusesProperties ModdMuddProcessor::makeBuses()
{
    if constexpr (kIsInstrumentBuild)
        return juce::AudioProcessor::BusesProperties()
                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true);

    return juce::AudioProcessor::BusesProperties()
               .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
               .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
               .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false);
}

static juce::String snapshotKey (const juce::String& uid, const juce::String& paramId)   { return uid + ":" + paramId; }

//==============================================================================
struct ModdMuddProcessor::ProgramCapture final : private juce::Timer
{
    ProgramCapture (ModdMuddProcessor& o) : owner (o) {}
    ~ProgramCapture() override  { stopTimer(); }

    void start()                { startTimer (40); }
    void timerCallback() override { owner.pollProgramCapture(); }

    ModdMuddProcessor& owner;
    int snapshot = 0, program = 0;
    juce::String slotUid, programName;
    juce::MemoryBlock savedState;
    std::vector<float> savedBases, lastValues;
    int elapsedMs = 0, stablePolls = 0;
};

ModdMuddProcessor::ModdMuddProcessor()
    : AudioProcessor (makeBuses()),
      state (*this, nullptr, "PARAMS", createParameterLayout (slotParams))
{
    refs.bind (state);

    for (auto& c : ccToParam)
        c.store (-1);

    for (auto& w : customWaves)
        w = Lfo::defaultCustomPoints();

    for (auto& c : shaperCurves)
        c = Shaper::defaultPoints();

    for (auto& s : randomSeeds)
        s = (uint32_t) juce::Random::getSystemRandom().nextInt();

    state.addParameterListener (ids::blendOn, this);
    for (int i = 0; i < kNumRandoms; ++i)
        state.addParameterListener (ids::rnd (i, "indep"), this);

    rebuild();
    startTimerHz (30);
}

ModdMuddProcessor::~ModdMuddProcessor()
{
    programCapture.reset();
    stopTimer();
    state.removeParameterListener (ids::blendOn, this);
    for (int i = 0; i < kNumRandoms; ++i)
        state.removeParameterListener (ids::rnd (i, "indep"), this);

    // Hosted plug-ins are destroyed here, on the message thread.
    graveyard.clear();
    slots.clear();
}

//==============================================================================
int ModdMuddProcessor::mainChannels() const
{
    return juce::jmax (1, getMainBusNumOutputChannels());
}

bool ModdMuddProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if constexpr (! kIsInstrumentBuild)
    {
        if (layouts.getMainInputChannelSet() != out)
            return false;

        if (layouts.inputBuses.size() > 1)
        {
            const auto sc = layouts.getChannelSet (true, 1);
            if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
                return false;
        }
    }

    return true;
}

void ModdMuddProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = juce::jmax (1, samplesPerBlock);

    engine.prepare (sampleRate);
    for (int i = 0; i < kNumRandoms; ++i)
        engine.setRandomSeed (i, randomSeeds[(size_t) i]);

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (refs.outputGain->load()));

    midiFiltered.ensureSize (8192);
    midiSub.ensureSize (8192);
    midiSlot.ensureSize (8192);

    {
        const juce::ScopedLock sl (modelLock);
        for (auto& s : slots)
            s->prepare (sampleRate, currentBlockSize, mainChannels());
    }

    wasPlaying = false;
    audioActive.store (true);

    rebuild();        // smoothing coefficients depend on the sample rate
    updateLatency();
}

void ModdMuddProcessor::releaseResources()
{
    audioActive.store (false);

    const juce::ScopedLock sl (modelLock);
    for (auto& s : slots)
        s->release();
}

double ModdMuddProcessor::getTailLengthSeconds() const
{
    double tail = 0.0;
    const juce::ScopedLock sl (modelLock);

    for (auto& s : slots)
        if (auto* inst = s->getInstance())
            tail = juce::jmax (tail, inst->getTailLengthSeconds());

    return tail;
}

//==============================================================================
void ModdMuddProcessor::applyNewState (RtState& st) noexcept
{
    // Parameters that are no longer modulated go back to the user's value.
    for (auto [si, pi] : st.restore)
    {
        if (si < 0 || si >= st.numSlots || st.slots[(size_t) si] == nullptr)
            continue;

        auto* slot = st.slots[(size_t) si];
        if (auto* p = slot->getParam (pi))
        {
            auto& ps = slot->getState (pi);
            const float base = ps.base.load();
            p->setValue (base);
            ps.lastWritten = base;
            ps.display.store (base);
            ps.smoothedValid = false;
            ps.driven.store (false);
        }
    }

    for (size_t i = 0; i < st.plan.hosted.size(); ++i)
    {
        if (i < st.freshTargets.size() && st.freshTargets[i] != 0)
        {
            const auto& t = st.plan.hosted[i];
            if (auto* slot = st.slots[(size_t) t.slot])
                slot->getState (t.param).smoothedValid = false;
        }
    }

    for (int i = 0; i < kNumRandoms; ++i)
        if (engine.getRandomSeed (i) != st.seeds[(size_t) i])
            engine.setRandomSeed (i, st.seeds[(size_t) i]);

    for (int k = 0; k < kNumSlotParams; ++k)
        lastSlotValue[(size_t) k] = slotParams[(size_t) k]->get();

    appliedGeneration = st.generation;
}

void ModdMuddProcessor::applyTargets (RtState& st, int numSamples) noexcept
{
    juce::ignoreUnused (numSamples);

    const auto weights = engine.getBlendWeights();
    const bool blendOn = settings.blendEnabled;

    std::array<bool, kMaxHostedPlugins> resync {};
    for (int s = 0; s < st.numSlots; ++s)
        if (auto* slot = st.slots[(size_t) s])
            resync[(size_t) s] = slot->consumeResync();

    for (auto& t : st.plan.hosted)
    {
        auto* slot = st.slots[(size_t) t.slot];
        if (slot == nullptr || slot->isMissing() || slot->holdWrites.load (std::memory_order_relaxed))
            continue;

        if (resync[(size_t) t.slot])
            slot->getState (t.param).lastWritten = -1.0f;

        auto* param = slot->getParam (t.param);
        if (param == nullptr)
            continue;

        auto& ps = slot->getState (t.param);
        const float base = ps.base.load (std::memory_order_relaxed);

        if (ps.gesture.load (std::memory_order_relaxed))
        {
            // The user is moving this control: let it follow their hand.
            ps.smoothed = base;
            ps.smoothedValid = true;
            ps.lastWritten = base;
            ps.display.store (base, std::memory_order_relaxed);
            continue;
        }

        float b = base;
        if (blendOn && t.blendEntry >= 0)
        {
            float blended;
            if (ModEngine::resolveBlend (st.plan.blend[(size_t) t.blendEntry], weights, blended))
                b = blended;
        }

        const float desired = t.connections.empty() ? b : engine.evaluate (b, t);

        if (! ps.smoothedValid)
        {
            ps.smoothed = param->getValue();
            ps.smoothedValid = true;
        }

        ps.smoothed += (desired - ps.smoothed) * t.smoothCoeff;

        float out = ps.smoothed;
        if (ps.discrete && ps.numSteps > 1 && ps.numSteps < 100000)
        {
            const float steps = (float) (ps.numSteps - 1);
            out = std::round (out * steps) / steps;
        }

        if (std::abs (out - ps.lastWritten) > 1.0e-6f)
        {
            param->setValue (out);
            ps.lastWritten = out;
        }

        ps.display.store (out, std::memory_order_relaxed);
    }
}

void ModdMuddProcessor::handleMidi (juce::MidiBuffer& midi) noexcept
{
    midiFiltered.clear();

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();

        if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            const int learn = midiLearnTarget.load (std::memory_order_relaxed);

            if (learn >= 0)
            {
                ccToParam[(size_t) cc].store (learn);
                midiLearnTarget.store (-1);
                midiLearnCompleted.store (true);
                continue;
            }

            const int target = ccToParam[(size_t) cc].load (std::memory_order_relaxed);
            if (target >= 0)
            {
                auto scope = ccFifo.write (1);
                const auto value = std::make_pair (target, msg.getControllerValue() / 127.0f);
                if (scope.blockSize1 > 0)      ccEvents[(size_t) scope.startIndex1] = value;
                else if (scope.blockSize2 > 0) ccEvents[(size_t) scope.startIndex2] = value;
                continue;   // learned CCs control ModdMudd, not the hosted plug-in
            }
        }

        midiFiltered.addEvent (msg, meta.samplePosition);
    }
}

void ModdMuddProcessor::handleSlotAutomation (RtState& st) noexcept
{
    for (int k = 0; k < kNumSlotParams; ++k)
    {
        const auto [si, pi] = st.slotMap[(size_t) k];
        if (si < 0)
            continue;

        const float v = slotParams[(size_t) k]->get();
        if (juce::exactlyEqual (v, lastSlotValue[(size_t) k]))
            continue;

        lastSlotValue[(size_t) k] = v;

        auto* slot = st.slots[(size_t) si];
        if (slot == nullptr || slot->isMissing() || slot->holdWrites.load (std::memory_order_relaxed))
            continue;

        auto& ps = slot->getState (pi);
        if (ps.gesture.load (std::memory_order_relaxed))
            continue;

        ps.base.store (v, std::memory_order_relaxed);

        if (! ps.driven.load (std::memory_order_relaxed))
        {
            if (auto* p = slot->getParam (pi))
                p->setValue (v);

            ps.lastWritten = v;
            ps.display.store (v, std::memory_order_relaxed);
        }
    }
}

void ModdMuddProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    auto mainBuffer = getBusBuffer (buffer, false, 0);
    const int numChannels = juce::jmin (mainBuffer.getNumChannels(), 8);

    if constexpr (kIsInstrumentBuild)
        mainBuffer.clear();

    juce::AudioBuffer<float> sidechain;
    bool hasSidechain = false;
    if constexpr (! kIsInstrumentBuild)
    {
        if (getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
        {
            sidechain = getBusBuffer (buffer, true, 1);
            hasSidechain = sidechain.getNumChannels() > 0;
        }
    }

    auto* st = rt.acquire();
    if (st != nullptr && st->generation != appliedGeneration)
        applyNewState (*st);

    refs.fill (settings);
    handleMidi (midi);
    midi.clear();

    if (st == nullptr)
        return;

    handleSlotAutomation (*st);

    subPlayHead.beginBlock (getPlayHead(), currentSampleRate);
    const auto& pos = subPlayHead.getBase();
    const bool playing = pos.hasValue() && pos->getIsPlaying();
    const double bpm = (pos.hasValue() && pos->getBpm().hasValue()) ? *pos->getBpm() : 120.0;
    const bool playStarted = playing && ! wasPlaying;
    wasPlaying = playing;

    std::array<const float*, kNumLfos> tables {};
    for (int i = 0; i < kNumLfos; ++i)
        tables[(size_t) i] = st->lfoTables[(size_t) i].size() == (size_t) kLfoTableSize ? st->lfoTables[(size_t) i].data() : nullptr;

    std::array<const float*, kNumShapers> shaperTables {};
    for (int i = 0; i < kNumShapers; ++i)
        shaperTables[(size_t) i] = st->shaperTables[(size_t) i].size() == (size_t) kLfoTableSize ? st->shaperTables[(size_t) i].data() : nullptr;

    const int sub = st->subBlock > 0 ? st->subBlock : numSamples;
    std::array<float*, 8> ptrs {};
    std::array<const float*, 8> scPtrs {};
    const bool offline = isNonRealtime();

    for (int start = 0; start < numSamples; start += sub)
    {
        const int len = juce::jmin (sub, numSamples - start);

        for (int c = 0; c < numChannels; ++c)
            ptrs[(size_t) c] = mainBuffer.getWritePointer (c) + start;

        TransportInfo t;
        t.isPlaying = playing;
        t.bpm = bpm;
        t.playStarted = playStarted && start == 0;
        t.hasPpq = SubBlockPlayHead::ppqAt (pos, start, currentSampleRate, t.ppq);

        midiSub.clear();
        for (const auto meta : midiFiltered)
        {
            if (meta.samplePosition >= start && meta.samplePosition < start + len)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn())
                    t.noteOn = true;
                midiSub.addEvent (msg, meta.samplePosition - start);
            }
        }

        // Envelope followers listening to the input (or sidechain) see this
        // sub-block before it is processed.
        for (int e = 0; e < kNumEnvs; ++e)
        {
            const auto& es = settings.env[(size_t) e];

            if (es.source == EnvSource::input)
            {
                engine.feedEnvelope (e, ptrs.data(), numChannels, len, es);
            }
            else if (es.source == EnvSource::sidechain)
            {
                if (hasSidechain)
                {
                    const int scCh = juce::jmin (sidechain.getNumChannels(), 8);
                    for (int c = 0; c < scCh; ++c)
                        scPtrs[(size_t) c] = sidechain.getReadPointer (c) + start;
                    engine.feedEnvelope (e, scPtrs.data(), scCh, len, es);
                }
                else
                {
                    engine.feedEnvelope (e, nullptr, 0, len, es);
                }
            }
        }

        // Shapers in audio-trigger mode listen to the input (the FX build) or
        // to what came out last (the Instrument build, which has no input).
        if constexpr (! kIsInstrumentBuild)
            for (int i = 0; i < kNumShapers; ++i)
                if (settings.shaper[(size_t) i].trigger == ShaperTrigger::audio)
                    engine.feedShaper (i, ptrs.data(), numChannels, len, settings.shaper[(size_t) i].thresholdDb);

        engine.computeSubBlock (settings, t, st->plan, len, tables, shaperTables);
        applyTargets (*st, len);

        subPlayHead.setOffset (start);

        for (int s = 0; s < st->numSlots; ++s)
        {
            if (auto* slot = st->slots[(size_t) s])
            {
                midiSlot.clear();
                midiSlot.addEvents (midiSub, 0, len, 0);
                slot->process (ptrs.data(), numChannels, len, midiSlot, &subPlayHead, offline);
            }
        }

        for (int e = 0; e < kNumEnvs; ++e)
            if (settings.env[(size_t) e].source == EnvSource::output)
                engine.feedEnvelope (e, ptrs.data(), numChannels, len, settings.env[(size_t) e]);

        if constexpr (kIsInstrumentBuild)
            for (int i = 0; i < kNumShapers; ++i)
                if (settings.shaper[(size_t) i].trigger == ShaperTrigger::audio)
                    engine.feedShaper (i, ptrs.data(), numChannels, len, settings.shaper[(size_t) i].thresholdDb);
    }

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (refs.outputGain->load (std::memory_order_relaxed)));
    outputGain.applyGain (mainBuffer, numSamples);
}

//==============================================================================
HostedSlot* ModdMuddProcessor::findSlot (const juce::String& uid) const
{
    for (auto& s : slots)
        if (s->getUid() == uid)
            return s.get();

    return nullptr;
}

int ModdMuddProcessor::getSlotIndex (const juce::String& uid) const
{
    for (size_t i = 0; i < slots.size(); ++i)
        if (slots[i]->getUid() == uid)
            return (int) i;

    return -1;
}

void ModdMuddProcessor::rebuild()
{
    const juce::ScopedLock sl (modelLock);

    auto st = std::make_unique<RtState>();
    st->numSlots = (int) slots.size();
    for (size_t i = 0; i < slots.size(); ++i)
        st->slots[i] = slots[i].get();

    if (! matrix.computeNodeOrder (st->plan.nodeOrder))
        jassertfalse;   // add() prevents cycles, so this shouldn't happen

    std::array<bool, kNumRandoms> independent {};
    for (int i = 0; i < kNumRandoms; ++i)
        independent[(size_t) i] = state.getRawParameterValue (ids::rnd (i, "indep"))->load() >= 0.5f;

    std::array<int, kNumSources> laneCounter {};

    auto toPlan = [&] (const Connection& c)
    {
        PlanConnection p;
        p.source = c.source;
        p.depth = c.depth;
        p.offset = c.offset;
        p.polarity = c.polarity;

        if (c.source >= srcRandom0 && c.source < srcMacro0 && independent[(size_t) (c.source - srcRandom0)])
            p.lane = laneCounter[(size_t) c.source]++ % kRandomLanes;

        return p;
    };

    const int sub = subBlockSize > 0 ? subBlockSize : currentBlockSize;
    std::map<juce::String, size_t> targetIndex;

    auto targetFor = [&] (int si, int pi, const juce::String& key) -> HostedTarget&
    {
        auto it = targetIndex.find (key);
        if (it != targetIndex.end())
            return st->plan.hosted[it->second];

        HostedTarget t;
        t.slot = si;
        t.param = pi;
        targetIndex[key] = st->plan.hosted.size();
        st->plan.hosted.push_back (t);
        return st->plan.hosted.back();
    };

    auto resolve = [this] (const juce::String& uid, const juce::String& paramId, int& si, int& pi)
    {
        si = getSlotIndex (uid);
        if (si < 0 || slots[(size_t) si]->isMissing())
            return false;

        pi = slots[(size_t) si]->findParamIndex (paramId);
        return pi >= 0 && slots[(size_t) si]->isModulatable (pi);
    };

    for (auto& c : matrix.getConnections())
    {
        if (! c.enabled)
            continue;

        if (c.dest.internal)
        {
            st->plan.internal[(size_t) c.dest.internalId].push_back (toPlan (c));
            continue;
        }

        int si, pi;
        if (! resolve (c.dest.slotUid, c.dest.paramId, si, pi))
            continue;

        auto& t = targetFor (si, pi, c.dest.key());
        t.connections.push_back (toPlan (c));

        const auto ds = matrix.getSettings (c.dest);
        t.rangeMin = ds.rangeMin;
        t.rangeMax = ds.rangeMax;
        t.smoothingMs = ds.smoothingMs;
    }

    // Preset blender: only parameters whose captured values actually differ
    // need to be driven.
    if (state.getRawParameterValue (ids::blendOn)->load() >= 0.5f)
    {
        for (auto& [key, e] : snapshots)
        {
            int count = 0;
            float lo = 2.0f, hi = -1.0f;

            for (int i = 0; i < kNumSnapshots; ++i)
                if ((e.mask & (1 << i)) != 0)
                {
                    ++count;
                    lo = juce::jmin (lo, e.values[(size_t) i]);
                    hi = juce::jmax (hi, e.values[(size_t) i]);
                }

            if (count < 2 || hi - lo < 1.0e-6f)
                continue;

            int si, pi;
            if (! resolve (e.slotUid, e.paramId, si, pi))
                continue;

            BlendEntry be;
            be.slot = si;
            be.param = pi;
            be.discrete = slots[(size_t) si]->getState (pi).discrete;
            be.values = e.values;
            be.mask = e.mask;

            auto& t = targetFor (si, pi, Destination::hosted (e.slotUid, e.paramId).key());
            t.blendEntry = (int) st->plan.blend.size();
            st->plan.blend.push_back (be);
        }
    }

    // Smoothing coefficients, freshness and the "driven" flags.
    std::map<juce::String, std::pair<juce::String, juce::String>> newTargets;
    st->freshTargets.resize (st->plan.hosted.size());

    for (size_t i = 0; i < st->plan.hosted.size(); ++i)
    {
        auto& t = st->plan.hosted[i];
        t.smoothCoeff = t.smoothingMs <= 0.0f ? 1.0f
                      : 1.0f - (float) std::exp (-(double) sub / (currentSampleRate * t.smoothingMs * 0.001));

        auto& slot = *slots[(size_t) t.slot];
        const auto paramId = slot.getParamId (t.param);
        const auto key = Destination::hosted (slot.getUid(), paramId).key();

        newTargets[key] = { slot.getUid(), paramId };
        st->freshTargets[i] = previousTargets.count (key) == 0 ? 1 : 0;
        slot.getState (t.param).driven.store (true);
    }

    for (auto& [key, ids] : previousTargets)
    {
        if (newTargets.count (key) != 0)
            continue;

        int si, pi;
        if (resolve (ids.first, ids.second, si, pi))
            st->restore.emplace_back (si, pi);
        else if (auto* slot = findSlot (ids.first))
            if (auto idx = slot->findParamIndex (ids.second); idx >= 0)
                slot->getState (idx).driven.store (false);
    }

    previousTargets = std::move (newTargets);

    for (int k = 0; k < kNumSlotParams; ++k)
    {
        st->slotMap[(size_t) k] = { -1, -1 };
        const auto& m = slotMap[(size_t) k];

        if (m.isEmpty())
            continue;

        const int si = getSlotIndex (m.slotUid);
        if (si < 0 || slots[(size_t) si]->isMissing())
            continue;

        const int pi = slots[(size_t) si]->findParamIndex (m.paramId);
        if (pi >= 0)
            st->slotMap[(size_t) k] = { si, pi };
    }

    for (int i = 0; i < kNumLfos; ++i)
    {
        st->lfoTables[(size_t) i].resize ((size_t) kLfoTableSize);
        Lfo::buildCustomTable (customWaves[(size_t) i], st->lfoTables[(size_t) i].data());
    }

    for (int i = 0; i < kNumShapers; ++i)
    {
        st->shaperTables[(size_t) i].resize ((size_t) kLfoTableSize);
        Lfo::buildCustomTable (shaperCurves[(size_t) i], st->shaperTables[(size_t) i].data());
    }

    st->seeds = randomSeeds;
    st->subBlock = subBlockSize;

    rt.publish (std::move (st));

    // With no audio running there's no audio thread to pick the state up.
    if (! audioActive.load())
        if (auto* current = rt.acquire())
            if (current->generation != appliedGeneration)
                applyNewState (*current);
}

//==============================================================================
void ModdMuddProcessor::retireSlot (std::unique_ptr<HostedSlot> slot)
{
    if (slot != nullptr)
        graveyard.emplace_back (rt.getPublishedGeneration(), std::move (slot));
}

juce::String ModdMuddProcessor::loadPlugin (const juce::PluginDescription& d, int replaceIndex)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (replaceIndex < 0 && (int) slots.size() >= kMaxHostedPlugins)
        return lastError = "ModdMudd can host up to " + juce::String (kMaxHostedPlugins) + " plug-ins";

    juce::String error;
    auto instance = catalog->createInstance (d, currentSampleRate, currentBlockSize, error);

    if (instance == nullptr)
        return lastError = (error.isNotEmpty() ? error : "Couldn't load " + d.name);

    auto slot = std::make_unique<HostedSlot> (std::move (instance), d, juce::Uuid().toString());
    slot->prepare (currentSampleRate, currentBlockSize, mainChannels());

    std::unique_ptr<HostedSlot> old;
    slotListeners.call ([] (Listener& l) { l.slotsAboutToChange(); });

    {
        const juce::ScopedLock sl (modelLock);

        if (juce::isPositiveAndBelow (replaceIndex, (int) slots.size()))
        {
            old = std::move (slots[(size_t) replaceIndex]);
            const auto oldUid = old->getUid();
            matrix.removeIf ([&] (const Connection& c) { return ! c.dest.internal && c.dest.slotUid == oldUid; });
            unmapSlot (oldUid);
            slots[(size_t) replaceIndex] = std::move (slot);
            autoMapSlot (*slots[(size_t) replaceIndex]);
        }
        else
        {
            slots.push_back (std::move (slot));
            autoMapSlot (*slots.back());
        }
    }

    rebuild();
    retireSlot (std::move (old));
    refreshSlotParamNames();
    updateLatency();
    lastError = {};
    sendChangeMessage();
    return {};
}

void ModdMuddProcessor::removePlugin (int index)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (! juce::isPositiveAndBelow (index, (int) slots.size()))
        return;

    slotListeners.call ([] (Listener& l) { l.slotsAboutToChange(); });

    std::unique_ptr<HostedSlot> old;
    {
        const juce::ScopedLock sl (modelLock);
        old = std::move (slots[(size_t) index]);
        slots.erase (slots.begin() + index);

        const auto uid = old->getUid();
        matrix.removeIf ([&] (const Connection& c) { return ! c.dest.internal && c.dest.slotUid == uid; });
        unmapSlot (uid);

        for (auto it = snapshots.begin(); it != snapshots.end();)
            it = it->second.slotUid == uid ? snapshots.erase (it) : std::next (it);
    }

    rebuild();
    retireSlot (std::move (old));
    refreshSlotParamNames();
    updateLatency();
    sendChangeMessage();
}

void ModdMuddProcessor::moveSlot (int from, int to)
{
    if (! juce::isPositiveAndBelow (from, (int) slots.size()) || ! juce::isPositiveAndBelow (to, (int) slots.size()) || from == to)
        return;

    slotListeners.call ([] (Listener& l) { l.slotsAboutToChange(); });

    {
        const juce::ScopedLock sl (modelLock);
        auto s = std::move (slots[(size_t) from]);
        slots.erase (slots.begin() + from);
        slots.insert (slots.begin() + to, std::move (s));
    }

    rebuild();
    refreshSlotParamNames();
    sendChangeMessage();
}

void ModdMuddProcessor::setHostedBase (int slotIndex, int paramIndex, float value)
{
    if (! juce::isPositiveAndBelow (slotIndex, (int) slots.size()))
        return;

    auto& slot = *slots[(size_t) slotIndex];
    slot.setBaseValue (paramIndex, value, true);

    auto it = slotParamLookup.find (snapshotKey (slot.getUid(), slot.getParamId (paramIndex)));
    if (it != slotParamLookup.end())
        slotParams[(size_t) it->second]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
}

//==============================================================================
ModMatrix::AddResult ModdMuddProcessor::addConnection (Connection c)
{
    ModMatrix::AddResult result;
    {
        const juce::ScopedLock sl (modelLock);
        result = matrix.add (c);
    }

    if (result == ModMatrix::AddResult::added)
    {
        // Connecting a module that isn't in the chain (e.g. "Assign to
        // macro" from the parameter list) adds it, so it can be seen.
        for (int m = 0; m < kNumModules; ++m)
            if (! isModuleActive (m)
                 && (moduleOwnsSource (m, c.source) || (c.dest.internal && moduleOwnsInternal (m, c.dest.internalId))))
                modules.push_back (m);

        rebuild();
        sendChangeMessage();
    }

    return result;
}

void ModdMuddProcessor::removeConnection (int id)
{
    bool removed;
    {
        const juce::ScopedLock sl (modelLock);
        removed = matrix.remove (id);
    }

    if (removed)
    {
        rebuild();
        sendChangeMessage();
    }
}

bool ModdMuddProcessor::updateConnection (const Connection& c)
{
    bool ok;
    {
        const juce::ScopedLock sl (modelLock);
        ok = matrix.update (c);
    }

    if (ok)
        rebuild();

    return ok;
}

void ModdMuddProcessor::setDestinationSettings (const Destination& d, const DestinationSettings& s)
{
    {
        const juce::ScopedLock sl (modelLock);
        matrix.setSettings (d, s);
    }
    rebuild();
}

void ModdMuddProcessor::removeConnectionsTo (const Destination& d)
{
    {
        const juce::ScopedLock sl (modelLock);
        const auto key = d.key();
        matrix.removeIf ([&] (const Connection& c) { return c.dest.key() == key; });
    }
    rebuild();
    sendChangeMessage();
}

bool ModdMuddProcessor::isModuleActive (int module) const
{
    return std::find (modules.begin(), modules.end(), module) != modules.end();
}

void ModdMuddProcessor::addModule (int module)
{
    if (! juce::isPositiveAndBelow (module, kNumModules) || isModuleActive (module))
        return;

    modules.push_back (module);
    sendChangeMessage();
}

void ModdMuddProcessor::removeModule (int module)
{
    auto it = std::find (modules.begin(), modules.end(), module);
    if (it == modules.end())
        return;

    modules.erase (it);

    if (armedSource >= 0 && moduleOwnsSource (module, armedSource))
        armedSource = -1;

    {
        const juce::ScopedLock sl (modelLock);
        matrix.removeIf ([module] (const Connection& c)
        {
            return moduleOwnsSource (module, c.source) || (c.dest.internal && moduleOwnsInternal (module, c.dest.internalId));
        });
    }

    // A removed blender stops blending (its snapshots are kept).
    if (module == modBlend)
        if (auto* p = state.getParameter (ids::blendOn))
            p->setValueNotifyingHost (0.0f);

    rebuild();
    sendChangeMessage();
}

void ModdMuddProcessor::moveModule (int module, int newIndex)
{
    auto it = std::find (modules.begin(), modules.end(), module);
    if (it == modules.end())
        return;

    modules.erase (it);
    newIndex = juce::jlimit (0, (int) modules.size(), newIndex);
    modules.insert (modules.begin() + newIndex, module);
    sendChangeMessage();
}

void ModdMuddProcessor::removeConnectionsFrom (int source)
{
    {
        const juce::ScopedLock sl (modelLock);
        matrix.removeIf ([source] (const Connection& c) { return c.source == source; });
    }
    rebuild();
    sendChangeMessage();
}

int ModdMuddProcessor::findParameterIndex (const juce::String& paramId) const
{
    const auto& params = getParameters();
    for (int i = 0; i < params.size(); ++i)
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[i]))
            if (withId->getParameterID() == paramId)
                return i;

    return -1;
}

//==============================================================================
void ModdMuddProcessor::captureSnapshot (int index)
{
    if (! juce::isPositiveAndBelow (index, kNumSnapshots))
        return;

    {
        const juce::ScopedLock sl (modelLock);

        for (auto& slot : slots)
        {
            if (slot->isMissing())
                continue;

            for (int pi = 0; pi < slot->getNumParams(); ++pi)
            {
                if (! slot->isModulatable (pi))
                    continue;

                const auto id = slot->getParamId (pi);
                auto& e = snapshots[snapshotKey (slot->getUid(), id)];
                e.slotUid = slot->getUid();
                e.paramId = id;
                e.values[(size_t) index] = slot->getState (pi).base.load();
                e.mask = (uint8_t) (e.mask | (1 << index));
            }
        }

        snapshotFilled[(size_t) index] = true;
    }

    rebuild();
    sendChangeMessage();
}

void ModdMuddProcessor::captureSnapshotFromProgram (int snapshot, int slotIndex, int program)
{
    if (programCapture != nullptr || ! juce::isPositiveAndBelow (snapshot, kNumSnapshots)
         || ! juce::isPositiveAndBelow (slotIndex, (int) slots.size()))
        return;

    auto& slot = *slots[(size_t) slotIndex];
    auto* inst = slot.getInstance();
    if (inst == nullptr || ! juce::isPositiveAndBelow (program, inst->getNumPrograms()))
        return;

    auto capture = std::make_unique<ProgramCapture> (*this);
    capture->snapshot = snapshot;
    capture->program = program;
    capture->slotUid = slot.getUid();
    capture->programName = inst->getProgramName (program);
    capture->savedState = slot.getPluginState();

    for (int i = 0; i < slot.getNumParams(); ++i)
        capture->savedBases.push_back (slot.getState (i).base.load());

    // Stop writing modulation to this plug-in so the preset's own values show.
    slot.holdWrites.store (true);
    inst->setCurrentProgram (program);

    programCapture = std::move (capture);
    programCapture->start();
    setStatus ("Reading preset \"" + programCapture->programName + "\"...");
}

void ModdMuddProcessor::pollProgramCapture()
{
    auto& c = *programCapture;
    auto* slot = findSlot (c.slotUid);

    if (slot == nullptr || slot->isMissing())
    {
        finishProgramCapture (false);
        return;
    }

    std::vector<float> values;
    for (int i = 0; i < slot->getNumParams(); ++i)
        values.push_back (slot->getParam (i)->getValue());

    c.elapsedMs += 40;
    c.stablePolls = (values == c.lastValues) ? c.stablePolls + 1 : 0;
    c.lastValues = values;

    // The new values arrive through the plug-in's controller, a little after
    // the preset change. Wait until they stop changing (or give up waiting).
    if ((c.elapsedMs >= 120 && c.stablePolls >= 2) || c.elapsedMs >= 2000)
        finishProgramCapture (true);
}

void ModdMuddProcessor::finishProgramCapture (bool captured)
{
    auto c = std::move (programCapture);
    juce::String message;

    if (auto* slot = findSlot (c->slotUid))
    {
        if (captured)
        {
            const juce::ScopedLock sl (modelLock);

            for (int pi = 0; pi < slot->getNumParams(); ++pi)
            {
                if (! slot->isModulatable (pi))
                    continue;

                const auto id = slot->getParamId (pi);
                auto& e = snapshots[snapshotKey (slot->getUid(), id)];
                e.slotUid = slot->getUid();
                e.paramId = id;
                e.values[(size_t) c->snapshot] = c->lastValues[(size_t) pi];
                e.mask = (uint8_t) (e.mask | (1 << c->snapshot));
            }

            snapshotFilled[(size_t) c->snapshot] = true;
            message = "Captured preset \"" + c->programName + "\" into snapshot " + juce::String::charToString ((juce::juce_wchar) ('A' + c->snapshot)) + ".";
        }
        else
        {
            message = "Couldn't read that preset.";
        }

        // Put the plug-in back exactly as it was, including the user's values
        // for modulated parameters.
        slot->setPluginState (c->savedState);
        for (int i = 0; i < slot->getNumParams() && i < (int) c->savedBases.size(); ++i)
            slot->getState (i).base.store (c->savedBases[(size_t) i]);

        slot->requestResync();
        slot->holdWrites.store (false);
    }

    rebuild();
    refreshSlotParamNames();
    setStatus (message);
}

void ModdMuddProcessor::recallSnapshot (int index)
{
    if (! hasSnapshot (index))
        return;

    for (auto& [key, e] : snapshots)
    {
        if ((e.mask & (1 << index)) == 0)
            continue;

        const int si = getSlotIndex (e.slotUid);
        if (si < 0)
            continue;

        const int pi = slots[(size_t) si]->findParamIndex (e.paramId);
        if (pi >= 0)
            setHostedBase (si, pi, e.values[(size_t) index]);
    }

    sendChangeMessage();
}

void ModdMuddProcessor::clearSnapshot (int index)
{
    if (! juce::isPositiveAndBelow (index, kNumSnapshots))
        return;

    {
        const juce::ScopedLock sl (modelLock);

        for (auto it = snapshots.begin(); it != snapshots.end();)
        {
            it->second.mask = (uint8_t) (it->second.mask & ~(1 << index));
            it = it->second.mask == 0 ? snapshots.erase (it) : std::next (it);
        }

        snapshotFilled[(size_t) index] = false;
    }

    rebuild();
    sendChangeMessage();
}

bool ModdMuddProcessor::hasSnapshot (int index) const
{
    return juce::isPositiveAndBelow (index, kNumSnapshots) && snapshotFilled[(size_t) index];
}

std::vector<WavePoint> ModdMuddProcessor::getCustomWave (int lfo) const
{
    const juce::ScopedLock sl (modelLock);
    return juce::isPositiveAndBelow (lfo, kNumLfos) ? customWaves[(size_t) lfo] : std::vector<WavePoint> {};
}

void ModdMuddProcessor::setCustomWave (int lfo, std::vector<WavePoint> points)
{
    if (! juce::isPositiveAndBelow (lfo, kNumLfos))
        return;

    if (points.size() > (size_t) kMaxCustomPoints)
        points.resize ((size_t) kMaxCustomPoints);

    {
        const juce::ScopedLock sl (modelLock);
        customWaves[(size_t) lfo] = std::move (points);
    }
    rebuild();
}

std::vector<WavePoint> ModdMuddProcessor::getShaperCurve (int shaper) const
{
    const juce::ScopedLock sl (modelLock);
    return juce::isPositiveAndBelow (shaper, kNumShapers) ? shaperCurves[(size_t) shaper] : std::vector<WavePoint> {};
}

void ModdMuddProcessor::setShaperCurve (int shaper, std::vector<WavePoint> points)
{
    if (! juce::isPositiveAndBelow (shaper, kNumShapers) || points.size() < 2)
        return;

    if (points.size() > (size_t) kMaxCustomPoints)
        points.resize ((size_t) kMaxCustomPoints);

    {
        const juce::ScopedLock sl (modelLock);
        shaperCurves[(size_t) shaper] = std::move (points);
    }
    rebuild();
}

uint32_t ModdMuddProcessor::getRandomSeed (int index) const
{
    return juce::isPositiveAndBelow (index, kNumRandoms) ? randomSeeds[(size_t) index] : 0u;
}

void ModdMuddProcessor::setRandomSeed (int index, uint32_t seed)
{
    if (! juce::isPositiveAndBelow (index, kNumRandoms))
        return;

    {
        const juce::ScopedLock sl (modelLock);
        randomSeeds[(size_t) index] = seed;
    }
    rebuild();
}

int ModdMuddProcessor::getCcForParameter (int parameterIndex) const
{
    for (int cc = 0; cc < 128; ++cc)
        if (ccToParam[(size_t) cc].load() == parameterIndex)
            return cc;

    return -1;
}

void ModdMuddProcessor::clearMidiForParameter (int parameterIndex)
{
    for (auto& c : ccToParam)
        if (c.load() == parameterIndex)
            c.store (-1);
}

void ModdMuddProcessor::setSubBlockSize (int samples)
{
    subBlockSize = juce::jlimit (0, 1024, samples);
    rebuild();
}

//==============================================================================
void ModdMuddProcessor::autoMapSlot (const HostedSlot& slot)
{
    if (slot.isMissing())
        return;

    size_t k = 0;

    for (int pi = 0; pi < slot.getNumParams(); ++pi)
    {
        if (! slot.isModulatable (pi))
            continue;

        while (k < slotMap.size() && ! slotMap[k].isEmpty())
            ++k;

        if (k >= slotMap.size())
            break;

        slotMap[k] = { slot.getUid(), slot.getParamId (pi) };
    }
}

void ModdMuddProcessor::unmapSlot (const juce::String& uid)
{
    for (auto& m : slotMap)
        if (m.slotUid == uid)
            m = {};
}

void ModdMuddProcessor::refreshSlotParamNames()
{
    slotParamLookup.clear();
    const bool prefix = slots.size() > 1;

    for (int k = 0; k < kNumSlotParams; ++k)
    {
        auto* p = slotParams[(size_t) k];
        const auto& m = slotMap[(size_t) k];
        juce::String name;

        if (! m.isEmpty())
        {
            const int si = getSlotIndex (m.slotUid);
            if (si >= 0)
            {
                auto& slot = *slots[(size_t) si];
                const int pi = slot.findParamIndex (m.paramId);

                if (pi >= 0)
                {
                    name = (prefix ? juce::String (si + 1) + ": " : juce::String()) + slot.getParamName (pi);
                    slotParamLookup[snapshotKey (m.slotUid, m.paramId)] = k;

                    const float base = slot.getState (pi).base.load();
                    if (std::abs (p->get() - base) > 1.0e-6f)
                        p->setValueNotifyingHost (base);
                }
                else
                {
                    name = slot.getDisplayName() + " (missing parameter)";
                }
            }
        }

        p->setDisplayName (name);
    }

    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
}

void ModdMuddProcessor::updateLatency()
{
    int total = 0;
    {
        const juce::ScopedLock sl (modelLock);
        for (auto& s : slots)
            total += s->getLatencySamples();
    }

    if (total != getLatencySamples())
        setLatencySamples (total);
}

//==============================================================================
void ModdMuddProcessor::processSlotEvents()
{
    for (size_t si = 0; si < slots.size(); ++si)
    {
        auto* slot = slots[si].get();
        bool latencyDirty = slot->consumeLatencyChanged();

        HostedSlot::Event e;
        const bool capturing = programCapture != nullptr && programCapture->slotUid == slot->getUid();

        while (slot->popEvent (e))
        {
            // Preset changes made while reading a preset aren't the user's.
            if (capturing)
                continue;

            const auto paramId = slot->getParamId (e.param);
            auto it = slotParamLookup.find (snapshotKey (slot->getUid(), paramId));

            if (it != slotParamLookup.end())
            {
                auto* p = slotParams[(size_t) it->second];

                if (e.type == HostedSlot::Event::Type::gestureStart)      p->beginChangeGesture();
                else if (e.type == HostedSlot::Event::Type::gestureEnd)   p->endChangeGesture();
                else if (std::abs (p->get() - e.value) > 1.0e-6f)         p->setValueNotifyingHost (e.value);
            }

            // Assign-by-touch: the first control the user moves gets the armed source.
            if (armedSource >= 0 && e.fromUser && e.type != HostedSlot::Event::Type::gestureEnd
                 && slot->isModulatable (e.param))
            {
                Connection c;
                c.source = armedSource;
                c.dest = Destination::hosted (slot->getUid(), paramId);
                c.depth = 0.5f;
                armedSource = -1;
                addConnection (c);
                sendChangeMessage();
                return;   // slots may have been rebuilt
            }
        }

        if (latencyDirty)
            updateLatency();
    }
}

void ModdMuddProcessor::timerCallback()
{
    rt.collectGarbage();

    if (! audioActive.load())
        if (auto* current = rt.acquire())
            if (current->generation != appliedGeneration)
                applyNewState (*current);

    const auto acked = rt.getAcknowledgedGeneration();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [acked] (auto& g) { return g.first <= acked; }),
                     graveyard.end());

    processSlotEvents();

    // MIDI CC values for learned parameters.
    {
        auto params = getParameters();
        auto scope = ccFifo.read (ccFifo.getNumReady());

        auto apply = [&] (int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto [idx, value] = ccEvents[(size_t) (start + i)];
                if (juce::isPositiveAndBelow (idx, params.size()))
                    params[idx]->setValueNotifyingHost (value);
            }
        };

        apply (scope.startIndex1, scope.blockSize1);
        apply (scope.startIndex2, scope.blockSize2);
    }

    if (midiLearnCompleted.exchange (false))
        sendChangeMessage();

    if (rebuildPending.exchange (false))
        rebuild();
}

void ModdMuddProcessor::parameterChanged (const juce::String&, float)
{
    // Blender on/off and independent randomization change the routing plan.
    // This can be called on any thread, so defer the rebuild to the timer.
    rebuildPending.store (true);
}

//==============================================================================
juce::ValueTree ModdMuddProcessor::createStateTree()
{
    const juce::ScopedLock sl (modelLock);

    juce::ValueTree root ("ModdMudd");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("build", kIsInstrumentBuild ? "instrument" : "fx", nullptr);

    root.appendChild (state.copyState(), nullptr);

    juce::ValueTree chain ("Chain");
    for (auto& slot : slots)
    {
        juce::ValueTree s ("Slot");
        s.setProperty ("uid", slot->getUid(), nullptr);

        if (auto xml = slot->getDescription().createXml())
            s.appendChild (juce::ValueTree::fromXml (*xml), nullptr);

        s.setProperty ("state", slot->getPluginState().toBase64Encoding(), nullptr);

        // Modulated parameters: the plug-in's own state holds whatever value
        // was last written, so store the user's values separately.
        if (slot->isMissing())
        {
            for (auto& [id, v] : slot->pendingBases)
            {
                juce::ValueTree b ("Base");
                b.setProperty ("id", id, nullptr);
                b.setProperty ("v", v, nullptr);
                s.appendChild (b, nullptr);
            }
        }
        else
        {
            for (int pi = 0; pi < slot->getNumParams(); ++pi)
            {
                auto& ps = slot->getState (pi);
                if (! ps.driven.load())
                    continue;

                juce::ValueTree b ("Base");
                b.setProperty ("id", slot->getParamId (pi), nullptr);
                b.setProperty ("v", ps.base.load(), nullptr);
                s.appendChild (b, nullptr);
            }
        }

        chain.appendChild (s, nullptr);
    }
    root.appendChild (chain, nullptr);

    root.appendChild (matrix.toValueTree(), nullptr);

    juce::ValueTree blend ("Blend");
    for (int i = 0; i < kNumSnapshots; ++i)
        blend.setProperty ("filled" + juce::String (i), snapshotFilled[(size_t) i], nullptr);

    for (auto& [key, e] : snapshots)
    {
        juce::ValueTree t ("Snap");
        t.setProperty ("slot", e.slotUid, nullptr);
        t.setProperty ("param", e.paramId, nullptr);
        t.setProperty ("mask", (int) e.mask, nullptr);
        for (int i = 0; i < kNumSnapshots; ++i)
            t.setProperty ("v" + juce::String (i), e.values[(size_t) i], nullptr);
        blend.appendChild (t, nullptr);
    }
    root.appendChild (blend, nullptr);

    juce::ValueTree waves ("Waves");
    for (int i = 0; i < kNumLfos; ++i)
    {
        juce::ValueTree w ("Wave");
        w.setProperty ("index", i, nullptr);
        for (auto& p : customWaves[(size_t) i])
        {
            juce::ValueTree pt ("Pt");
            pt.setProperty ("x", p.x, nullptr);
            pt.setProperty ("y", p.y, nullptr);
            pt.setProperty ("c", p.curve, nullptr);
            w.appendChild (pt, nullptr);
        }
        waves.appendChild (w, nullptr);
    }
    root.appendChild (waves, nullptr);

    juce::ValueTree shapes ("Shapes");
    for (int i = 0; i < kNumShapers; ++i)
    {
        juce::ValueTree w ("Shape");
        w.setProperty ("index", i, nullptr);
        for (auto& p : shaperCurves[(size_t) i])
        {
            juce::ValueTree pt ("Pt");
            pt.setProperty ("x", p.x, nullptr);
            pt.setProperty ("y", p.y, nullptr);
            pt.setProperty ("c", p.curve, nullptr);
            w.appendChild (pt, nullptr);
        }
        shapes.appendChild (w, nullptr);
    }
    root.appendChild (shapes, nullptr);

    juce::ValueTree seeds ("Seeds");
    for (int i = 0; i < kNumRandoms; ++i)
        seeds.setProperty ("s" + juce::String (i), (juce::int64) randomSeeds[(size_t) i], nullptr);
    root.appendChild (seeds, nullptr);

    juce::ValueTree map ("SlotMap");
    for (int k = 0; k < kNumSlotParams; ++k)
    {
        const auto& m = slotMap[(size_t) k];
        if (m.isEmpty())
            continue;

        juce::ValueTree t ("Map");
        t.setProperty ("k", k, nullptr);
        t.setProperty ("slot", m.slotUid, nullptr);
        t.setProperty ("param", m.paramId, nullptr);
        map.appendChild (t, nullptr);
    }
    root.appendChild (map, nullptr);

    juce::ValueTree midiTree ("Midi");
    const auto params = getParameters();
    for (int cc = 0; cc < 128; ++cc)
    {
        const int idx = ccToParam[(size_t) cc].load();
        if (! juce::isPositiveAndBelow (idx, params.size()))
            continue;

        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[idx]))
        {
            juce::ValueTree t ("CC");
            t.setProperty ("cc", cc, nullptr);
            t.setProperty ("param", withId->getParameterID(), nullptr);
            midiTree.appendChild (t, nullptr);
        }
    }
    root.appendChild (midiTree, nullptr);

    juce::ValueTree s ("Settings");
    s.setProperty ("subBlock", subBlockSize, nullptr);
    s.setProperty ("theme", themeIndex, nullptr);

    juce::StringArray moduleList;
    for (int m : modules)
        moduleList.add (juce::String (m));
    s.setProperty ("modules", moduleList.joinIntoString (","), nullptr);

    root.appendChild (s, nullptr);

    return root;
}

void ModdMuddProcessor::restoreStateTree (const juce::ValueTree& root)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (! root.hasType ("ModdMudd"))
        return;

    slotListeners.call ([] (Listener& l) { l.slotsAboutToChange(); });

    std::vector<std::unique_ptr<HostedSlot>> old;
    juce::StringArray missing;

    {
        const juce::ScopedLock sl (modelLock);

        if (auto params = root.getChildWithName (state.state.getType()); params.isValid())
            state.replaceState (params);

        old = std::move (slots);
        slots.clear();

        for (auto s : root.getChildWithName ("Chain"))
        {
            if (! s.hasType ("Slot"))
                continue;

            const auto uid = s.getProperty ("uid").toString();
            juce::PluginDescription desc;

            for (auto child : s)
                if (auto xml = child.createXml(); xml != nullptr && desc.loadFromXml (*xml))
                    break;

            juce::MemoryBlock pluginState;
            pluginState.fromBase64Encoding (s.getProperty ("state").toString());

            std::map<juce::String, float> bases;
            for (auto b : s)
                if (b.hasType ("Base"))
                    bases[b.getProperty ("id").toString()] = (float) b.getProperty ("v");

            std::unique_ptr<HostedSlot> slot;

            if (auto found = catalog->findMatching (desc))
            {
                juce::String error;
                if (auto inst = catalog->createInstance (*found, currentSampleRate, currentBlockSize, error))
                {
                    slot = std::make_unique<HostedSlot> (std::move (inst), *found, uid);
                    slot->setPluginState (pluginState);
                    slot->prepare (currentSampleRate, currentBlockSize, mainChannels());

                    for (auto& [id, v] : bases)
                        if (auto pi = slot->findParamIndex (id); pi >= 0)
                            slot->setBaseValue (pi, v, true);
                }
            }

            if (slot == nullptr)
            {
                missing.add (desc.name);
                slot = std::make_unique<HostedSlot> (desc, pluginState, uid);
                slot->pendingBases = bases;
            }

            if ((int) slots.size() < kMaxHostedPlugins)
                slots.push_back (std::move (slot));
        }

        matrix.fromValueTree (root.getChildWithName ("Matrix"));

        snapshots.clear();
        const auto blend = root.getChildWithName ("Blend");
        for (int i = 0; i < kNumSnapshots; ++i)
            snapshotFilled[(size_t) i] = blend.getProperty ("filled" + juce::String (i), false);

        for (auto t : blend)
        {
            SnapshotEntry e;
            e.slotUid = t.getProperty ("slot").toString();
            e.paramId = t.getProperty ("param").toString();
            e.mask = (uint8_t) (int) t.getProperty ("mask", 0);
            for (int i = 0; i < kNumSnapshots; ++i)
                e.values[(size_t) i] = t.getProperty ("v" + juce::String (i), 0.0f);
            snapshots[snapshotKey (e.slotUid, e.paramId)] = e;
        }

        // Sessions saved before a feature existed get its defaults.
        for (auto& w : customWaves)   w = Lfo::defaultCustomPoints();
        for (auto& c : shaperCurves)  c = Shaper::defaultPoints();

        for (auto w : root.getChildWithName ("Waves"))
        {
            const int i = w.getProperty ("index", -1);
            if (! juce::isPositiveAndBelow (i, kNumLfos))
                continue;

            std::vector<WavePoint> pts;
            for (auto pt : w)
                pts.push_back ({ (float) pt.getProperty ("x"), (float) pt.getProperty ("y"), (float) pt.getProperty ("c") });

            if (pts.size() >= 2)
                customWaves[(size_t) i] = pts;
        }

        for (auto w : root.getChildWithName ("Shapes"))
        {
            const int i = w.getProperty ("index", -1);
            if (! juce::isPositiveAndBelow (i, kNumShapers))
                continue;

            std::vector<WavePoint> pts;
            for (auto pt : w)
                pts.push_back ({ (float) pt.getProperty ("x"), (float) pt.getProperty ("y"), (float) pt.getProperty ("c") });

            if (pts.size() >= 2)
                shaperCurves[(size_t) i] = pts;
        }

        const auto seeds = root.getChildWithName ("Seeds");
        for (int i = 0; i < kNumRandoms; ++i)
            if (seeds.hasProperty ("s" + juce::String (i)))
                randomSeeds[(size_t) i] = (uint32_t) (juce::int64) seeds.getProperty ("s" + juce::String (i));

        for (auto& m : slotMap)
            m = {};

        const auto map = root.getChildWithName ("SlotMap");
        if (map.isValid() && map.getNumChildren() > 0)
        {
            for (auto t : map)
            {
                const int k = t.getProperty ("k", -1);
                if (juce::isPositiveAndBelow (k, kNumSlotParams))
                    slotMap[(size_t) k] = { t.getProperty ("slot").toString(), t.getProperty ("param").toString() };
            }
        }
        else
        {
            for (auto& slot : slots)
                autoMapSlot (*slot);
        }

        for (auto& c : ccToParam)
            c.store (-1);

        const auto params = getParameters();
        for (auto t : root.getChildWithName ("Midi"))
        {
            const int cc = t.getProperty ("cc", -1);
            const auto id = t.getProperty ("param").toString();

            if (! juce::isPositiveAndBelow (cc, 128))
                continue;

            for (int i = 0; i < params.size(); ++i)
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[i]))
                    if (withId->getParameterID() == id)
                        ccToParam[(size_t) cc].store (i);
        }

        const auto s = root.getChildWithName ("Settings");
        subBlockSize = juce::jlimit (0, 1024, (int) s.getProperty ("subBlock", kDefaultSubBlock));
        themeIndex = s.getProperty ("theme", 0);

        modules.clear();
        if (s.hasProperty ("modules"))
        {
            for (auto& m : juce::StringArray::fromTokens (s.getProperty ("modules").toString(), ",", ""))
                if (const int id = m.getIntValue(); juce::isPositiveAndBelow (id, kNumModules) && ! isModuleActive (id))
                    modules.push_back (id);
        }
        else
        {
            // Older sessions: show every module that is in use.
            for (int m = 0; m < kNumModules; ++m)
            {
                bool used = (m == modBlend && (state.getRawParameterValue (ids::blendOn)->load() >= 0.5f || ! snapshots.empty()));
                for (auto& c : matrix.getConnections())
                    used = used || moduleOwnsSource (m, c.source) || (c.dest.internal && moduleOwnsInternal (m, c.dest.internalId));

                if (used)
                    modules.push_back (m);
            }

            if (modules.empty())
                modules.push_back (modLfo0);
        }

        previousTargets.clear();
    }

    rebuild();
    for (auto& o : old)
        retireSlot (std::move (o));

    refreshSlotParamNames();
    updateLatency();

    lastError = missing.isEmpty() ? juce::String()
                                  : "Not installed on this computer: " + missing.joinIntoString (", ");
    sendChangeMessage();
}

void ModdMuddProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createStateTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void ModdMuddProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    // Hosted plug-ins must be created on the message thread.
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        restoreStateTree (tree);
    }
    else
    {
        juce::WeakReference<ModdMuddProcessor> weak (this);
        juce::MessageManager::callAsync ([weak, tree]
        {
            if (auto* p = weak.get())
                p->restoreStateTree (tree);
        });
    }
}

//==============================================================================
juce::File ModdMuddProcessor::getPresetDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("ModdMudd").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

bool ModdMuddProcessor::savePresetFile (const juce::File& f)
{
    if (auto xml = createStateTree().createXml())
        return xml->writeTo (f);

    return false;
}

bool ModdMuddProcessor::loadPresetFile (const juce::File& f)
{
    if (auto xml = juce::XmlDocument::parse (f))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.hasType ("ModdMudd"))
        {
            restoreStateTree (tree);
            return true;
        }
    }

    return false;
}

juce::AudioProcessorEditor* ModdMuddProcessor::createEditor()
{
    return new ModdMuddEditor (*this);
}

} // namespace mm

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mm::ModdMuddProcessor();
}
