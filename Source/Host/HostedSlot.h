#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Engine/ModTypes.h"

namespace mm
{

/** One hosted VST3 plug-in plus the real-time state of its parameters.

    A slot can also be "missing": the plug-in isn't installed on this machine.
    Missing slots keep the saved description, state and base values so that
    saving the session again doesn't destroy them, and pass audio through. */
class HostedSlot final : private juce::AudioProcessorParameter::Listener,
                         private juce::AudioProcessorListener
{
public:
    /** loadedFrom is the description the plug-in was created from. It's kept
        (rather than the instance's own description) because it names the .vst3
        bundle, which is what a later session needs to find the plug-in again. */
    HostedSlot (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::PluginDescription& loadedFrom,
                const juce::String& uid);
    HostedSlot (const juce::PluginDescription& missingDescription, const juce::MemoryBlock& savedState,
                const juce::String& uid);
    ~HostedSlot() override;

    //==============================================================================
    // Message thread
    bool isMissing() const noexcept                            { return instance == nullptr; }
    juce::AudioPluginInstance* getInstance() const noexcept    { return instance.get(); }
    const juce::PluginDescription& getDescription() const noexcept { return description; }
    const juce::String& getUid() const noexcept                { return uid; }
    juce::String getDisplayName() const                        { return description.name; }

    int getNumParams() const noexcept                          { return numParams; }
    juce::AudioProcessorParameter* getParam (int index) const noexcept;
    ParamState& getState (int index) const noexcept            { return states[(size_t) index]; }
    juce::String getParamId (int index) const;
    juce::String getParamName (int index) const;
    int findParamIndex (const juce::String& paramId) const;
    bool isModulatable (int index) const noexcept;

    /** Sets the user's value for a parameter (generic UI, snapshots, DAW slot). */
    void setBaseValue (int index, float value, bool writeToPlugin);

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void release();
    bool isPrepared() const noexcept                           { return prepared; }

    int getLatencySamples() const;
    bool consumeLatencyChanged() noexcept                      { return latencyChanged.exchange (false); }

    juce::MemoryBlock getPluginState() const;
    void setPluginState (const juce::MemoryBlock& data);

    /** While set, ModdMudd doesn't write modulated values to this plug-in
        (used while reading one of its presets). */
    std::atomic<bool> holdWrites { false };

    /** Asks the audio thread to re-send every modulated value (after the
        plug-in's state was replaced underneath it). */
    void requestResync() noexcept                              { resync.store (true); }
    bool consumeResync() noexcept                              { return resync.exchange (false); }

    /** Saved base values restored after loading, keyed by parameter ID. */
    std::map<juce::String, float> pendingBases;

    //==============================================================================
    // Audio thread
    void process (float* const* channels, int numChannels, int numSamples, juce::MidiBuffer& midi,
                  juce::AudioPlayHead* playHead, bool nonRealtime) noexcept;

    //==============================================================================
    /** Parameter events for the message thread (learn, DAW slot sync). */
    struct Event
    {
        enum class Type { value, gestureStart, gestureEnd };
        Type  type  = Type::value;
        int   param = 0;
        float value = 0.0f;
        bool  fromUser = false;   // came from the plug-in's own editor on the message thread
    };

    bool popEvent (Event& e) noexcept;

private:
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override;

    void initialiseParameters();
    void configureBuses (int numChannels);
    void pushEvent (const Event& e) noexcept;

    struct EventQueue
    {
        juce::AbstractFifo fifo { 1024 };
        std::array<Event, 1024> events;

        bool push (const Event& e) noexcept
        {
            auto scope = fifo.write (1);
            if (scope.blockSize1 > 0) { events[(size_t) scope.startIndex1] = e; return true; }
            if (scope.blockSize2 > 0) { events[(size_t) scope.startIndex2] = e; return true; }
            return false;
        }

        bool pop (Event& e) noexcept
        {
            auto scope = fifo.read (1);
            if (scope.blockSize1 > 0) { e = events[(size_t) scope.startIndex1]; return true; }
            if (scope.blockSize2 > 0) { e = events[(size_t) scope.startIndex2]; return true; }
            return false;
        }
    };

    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::PluginDescription description;
    juce::MemoryBlock missingState;
    juce::String uid;

    juce::Array<juce::AudioProcessorParameter*> params;
    int numParams = 0;
    std::unique_ptr<ParamState[]> states;
    std::vector<bool> modulatable;

    // Two single-producer queues: one fed from the message thread, one from
    // any other thread (the plug-in's processor reporting changes).
    EventQueue messageEvents, otherEvents;

    juce::AudioBuffer<float> scratch;
    int hostedIns = 0, hostedOuts = 0;
    bool prepared = false;
    std::atomic<bool> latencyChanged { false };
    std::atomic<bool> resync { false };
    bool lastNonRealtime = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedSlot)
};

} // namespace mm
