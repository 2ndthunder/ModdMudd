#include "HostedSlot.h"

namespace mm
{

HostedSlot::HostedSlot (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::PluginDescription& loadedFrom,
                        const juce::String& slotUid)
    : instance (std::move (inst)), description (loadedFrom), uid (slotUid)
{
    jassert (instance != nullptr);
    instance->addListener (this);
    initialiseParameters();
}

HostedSlot::HostedSlot (const juce::PluginDescription& d, const juce::MemoryBlock& savedState, const juce::String& slotUid)
    : description (d), missingState (savedState), uid (slotUid)
{
    states = std::make_unique<ParamState[]> (1);
}

HostedSlot::~HostedSlot()
{
    if (instance != nullptr)
    {
        for (auto* p : params)
            p->removeListener (this);

        instance->removeListener (this);

        if (prepared)
            instance->releaseResources();
    }
}

void HostedSlot::initialiseParameters()
{
    params = instance->getParameters();
    numParams = params.size();
    states = std::make_unique<ParamState[]> ((size_t) juce::jmax (1, numParams));
    modulatable.assign ((size_t) numParams, false);

    auto* bypass = instance->getBypassParameter();

    for (int i = 0; i < numParams; ++i)
    {
        auto* p = params[i];
        auto& s = states[(size_t) i];

        const float v = p->getValue();
        s.base.store (v);
        s.display.store (v);
        s.discrete = p->isDiscrete() || p->isBoolean();
        s.numSteps = p->getNumSteps();

        modulatable[(size_t) i] = p != bypass && p->isAutomatable() && p->getName (64).trim().isNotEmpty();

        p->addListener (this);
    }
}

juce::AudioProcessorParameter* HostedSlot::getParam (int index) const noexcept
{
    return juce::isPositiveAndBelow (index, numParams) ? params[index] : nullptr;
}

juce::String HostedSlot::getParamId (int index) const
{
    if (auto* p = getParam (index))
    {
        if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (p))
            return hosted->getParameterID();

        return juce::String (index);
    }

    return {};
}

juce::String HostedSlot::getParamName (int index) const
{
    if (auto* p = getParam (index))
        return p->getName (64);

    return {};
}

int HostedSlot::findParamIndex (const juce::String& paramId) const
{
    for (int i = 0; i < numParams; ++i)
        if (getParamId (i) == paramId)
            return i;

    return -1;
}

bool HostedSlot::isModulatable (int index) const noexcept
{
    return juce::isPositiveAndBelow (index, numParams) && modulatable[(size_t) index];
}

void HostedSlot::setBaseValue (int index, float value, bool writeToPlugin)
{
    if (! juce::isPositiveAndBelow (index, numParams))
        return;

    value = juce::jlimit (0.0f, 1.0f, value);
    auto& s = states[(size_t) index];
    s.base.store (value);

    // Driven parameters are written by the audio thread on the next sub-block.
    if (writeToPlugin && ! s.driven.load())
    {
        params[index]->setValue (value);
        s.display.store (value);
    }
}

//==============================================================================
void HostedSlot::configureBuses (int numChannels)
{
    const auto wanted = numChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    auto layout = instance->getBusesLayout();

    if (! layout.inputBuses.isEmpty())  layout.inputBuses.getReference (0) = wanted;
    if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = wanted;

    // Sidechains and extra outputs of the hosted plug-in are not used.
    for (int i = 1; i < layout.inputBuses.size(); ++i)  layout.inputBuses.getReference (i) = juce::AudioChannelSet::disabled();
    for (int i = 1; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference (i) = juce::AudioChannelSet::disabled();

    if (! instance->setBusesLayout (layout))
    {
        // Try only the main buses, leaving any others as the plug-in wants.
        auto mainOnly = instance->getBusesLayout();
        if (! mainOnly.inputBuses.isEmpty())  mainOnly.inputBuses.getReference (0) = wanted;
        if (! mainOnly.outputBuses.isEmpty()) mainOnly.outputBuses.getReference (0) = wanted;
        instance->setBusesLayout (mainOnly);   // if this fails too, the default layout is kept
    }
}

void HostedSlot::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    if (instance == nullptr)
        return;

    if (prepared)
        instance->releaseResources();

    configureBuses (numChannels);

    instance->prepareToPlay (sampleRate, maxBlockSize);
    hostedIns  = instance->getTotalNumInputChannels();
    hostedOuts = instance->getTotalNumOutputChannels();

    scratch.setSize (juce::jmax (hostedIns, hostedOuts, numChannels, 1), juce::jmax (1, maxBlockSize), false, true, false);

    for (int i = 0; i < numParams; ++i)
        states[(size_t) i].smoothedValid = false;

    prepared = true;
}

void HostedSlot::release()
{
    if (instance != nullptr && prepared)
        instance->releaseResources();

    prepared = false;
}

int HostedSlot::getLatencySamples() const
{
    return instance != nullptr ? instance->getLatencySamples() : 0;
}

juce::MemoryBlock HostedSlot::getPluginState() const
{
    if (instance == nullptr)
        return missingState;

    juce::MemoryBlock mb;
    instance->getStateInformation (mb);
    return mb;
}

void HostedSlot::setPluginState (const juce::MemoryBlock& data)
{
    if (instance == nullptr)
    {
        missingState = data;
        return;
    }

    if (data.getSize() > 0)
        instance->setStateInformation (data.getData(), (int) data.getSize());

    // The state may have changed every parameter.
    for (int i = 0; i < numParams; ++i)
    {
        const float v = params[i]->getValue();
        states[(size_t) i].base.store (v);
        states[(size_t) i].display.store (v);
    }
}

//==============================================================================
void HostedSlot::process (float* const* channels, int numChannels, int numSamples, juce::MidiBuffer& midi,
                          juce::AudioPlayHead* playHead, bool nonRealtime) noexcept
{
    if (instance == nullptr || ! prepared || numSamples <= 0)
        return;

    if (nonRealtime != lastNonRealtime)
    {
        instance->setNonRealtime (nonRealtime);
        lastNonRealtime = nonRealtime;
    }

    instance->setPlayHead (playHead);

    const int needed = scratch.getNumChannels();
    jassert (numSamples <= scratch.getNumSamples());
    numSamples = juce::jmin (numSamples, scratch.getNumSamples());

    // Map ModdMudd's channels onto the hosted plug-in's buses.
    for (int c = 0; c < needed; ++c)
    {
        auto* dst = scratch.getWritePointer (c);

        if (c < hostedIns && numChannels > 0)
        {
            if (hostedIns == 1 && numChannels >= 2)
            {
                for (int i = 0; i < numSamples; ++i)
                    dst[i] = 0.5f * (channels[0][i] + channels[1][i]);
            }
            else
            {
                juce::FloatVectorOperations::copy (dst, channels[juce::jmin (c, numChannels - 1)], numSamples);
            }
        }
        else
        {
            juce::FloatVectorOperations::clear (dst, numSamples);
        }
    }

    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), needed, numSamples);
    instance->processBlock (view, midi);

    for (int c = 0; c < numChannels; ++c)
    {
        if (hostedOuts <= 0)
            juce::FloatVectorOperations::clear (channels[c], numSamples);
        else
            juce::FloatVectorOperations::copy (channels[c], view.getReadPointer (juce::jmin (c, hostedOuts - 1)), numSamples);
    }
}

//==============================================================================
void HostedSlot::pushEvent (const Event& e) noexcept
{
    if (juce::MessageManager::existsAndIsCurrentThread())
        messageEvents.push (e);
    else
        otherEvents.push (e);
}

bool HostedSlot::popEvent (Event& e) noexcept
{
    return messageEvents.pop (e) || otherEvents.pop (e);
}

void HostedSlot::parameterValueChanged (int parameterIndex, float newValue)
{
    if (! juce::isPositiveAndBelow (parameterIndex, numParams))
        return;

    auto& s = states[(size_t) parameterIndex];

    // While a parameter is modulated, value reports that aren't part of a
    // user gesture are echoes of ModdMudd's own writes (or the plug-in
    // reflecting them back), so they must not move the base value.
    if (s.driven.load (std::memory_order_relaxed) && ! s.gesture.load (std::memory_order_relaxed))
        return;

    s.base.store (newValue, std::memory_order_relaxed);
    s.display.store (newValue, std::memory_order_relaxed);

    Event e;
    e.type = Event::Type::value;
    e.param = parameterIndex;
    e.value = newValue;
    e.fromUser = juce::MessageManager::existsAndIsCurrentThread();
    pushEvent (e);
}

void HostedSlot::parameterGestureChanged (int parameterIndex, bool starting)
{
    if (! juce::isPositiveAndBelow (parameterIndex, numParams))
        return;

    states[(size_t) parameterIndex].gesture.store (starting, std::memory_order_relaxed);

    Event e;
    e.type = starting ? Event::Type::gestureStart : Event::Type::gestureEnd;
    e.param = parameterIndex;
    e.value = params[parameterIndex]->getValue();
    e.fromUser = true;
    pushEvent (e);
}

void HostedSlot::audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details)
{
    if (details.latencyChanged)
        latencyChanged.store (true);
}

} // namespace mm
