#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/RtSwap.h"
#include "../Engine/ModEngine.h"
#include "../Engine/ModMatrix.h"
#include "../Engine/SubBlockPlayHead.h"
#include "../Host/HostedSlot.h"
#include "../Host/PluginCatalog.h"
#include "Parameters.h"
#include <map>
#include <set>

namespace mm
{

#if MODDMUDD_IS_INSTRUMENT
 inline constexpr bool kIsInstrumentBuild = true;
#else
 inline constexpr bool kIsInstrumentBuild = false;
#endif

/** Everything the audio thread needs, built on the message thread and
    swapped in atomically. */
struct RtState
{
    uint64_t generation = 0;

    std::array<HostedSlot*, kMaxHostedPlugins> slots {};
    int numSlots = 0;

    RoutingPlan plan;
    std::vector<uint8_t> freshTargets;                     // parallel to plan.hosted
    std::vector<std::pair<int, int>> restore;              // (slot, param) no longer driven
    std::array<std::pair<int, int>, kNumSlotParams> slotMap; // DAW slot -> (slot, param)
    std::array<std::vector<float>, kNumLfos> lfoTables;
    std::array<std::vector<float>, kNumShapers> shaperTables;
    std::array<uint32_t, kNumRandoms> seeds {};
    int subBlock = kDefaultSubBlock;
};

/** A saved preset-blender value for one hosted parameter. */
struct SnapshotEntry
{
    juce::String slotUid, paramId;
    std::array<float, kNumSnapshots> values {};
    uint8_t mask = 0;
};

struct SlotMapEntry
{
    juce::String slotUid, paramId;
    bool isEmpty() const noexcept   { return slotUid.isEmpty(); }
};

class ModdMuddProcessor final : public juce::AudioProcessor,
                                public juce::ChangeBroadcaster,
                                private juce::Timer,
                                private juce::AudioProcessorValueTreeState::Listener
{
public:
    ModdMuddProcessor();
    ~ModdMuddProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                  { return true; }

    const juce::String getName() const override      { return JucePlugin_Name; }
    bool acceptsMidi() const override                { return true; }
    bool producesMidi() const override               { return false; }
    bool isMidiEffect() const override               { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                    { return 1; }
    int getCurrentProgram() override                 { return 0; }
    void setCurrentProgram (int) override            {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Message-thread API used by the editor.
    struct Listener
    {
        virtual ~Listener() = default;
        /** Called synchronously before hosted plug-ins are removed or replaced,
            so editors can delete hosted plug-in UIs first. */
        virtual void slotsAboutToChange() = 0;
    };

    void addSlotListener (Listener* l)        { slotListeners.add (l); }
    void removeSlotListener (Listener* l)     { slotListeners.remove (l); }

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept   { return state; }
    PluginCatalog& getCatalog() noexcept                               { return *catalog; }
    ModEngine& getEngine() noexcept                                    { return engine; }

    const std::vector<std::unique_ptr<HostedSlot>>& getSlots() const noexcept   { return slots; }
    HostedSlot* findSlot (const juce::String& uid) const;
    int getSlotIndex (const juce::String& uid) const;

    juce::String loadPlugin (const juce::PluginDescription& d, int replaceIndex = -1);
    void removePlugin (int index);
    void moveSlot (int from, int to);

    void setHostedBase (int slotIndex, int paramIndex, float value);

    const ModMatrix& getMatrix() const noexcept   { return matrix; }
    ModMatrix::AddResult addConnection (Connection c);
    void removeConnection (int id);
    bool updateConnection (const Connection& c);
    void setDestinationSettings (const Destination& d, const DestinationSettings& s);
    void removeConnectionsTo (const Destination& d);

    /** Assign-by-touch: the next hosted control the user moves gets this source. */
    void armAssign (int source)          { armedSource = source; sendChangeMessage(); }
    void disarmAssign()                  { armedSource = -1; sendChangeMessage(); }
    int getArmedSource() const noexcept  { return armedSource; }

    void captureSnapshot (int index);

    /** Fills a blender snapshot from one of a hosted plug-in's own presets
        (programs). It takes a moment: the plug-in is switched to the preset,
        its parameters are read once they settle, then the plug-in is put back
        exactly as it was. */
    void captureSnapshotFromProgram (int snapshot, int slotIndex, int program);
    bool isCapturingProgram() const noexcept      { return programCapture != nullptr; }
    void recallSnapshot (int index);
    void clearSnapshot (int index);
    bool hasSnapshot (int index) const;
    int getNumSnapshotValues() const      { return (int) snapshots.size(); }

    std::vector<WavePoint> getCustomWave (int lfo) const;
    void setCustomWave (int lfo, std::vector<WavePoint> points);

    std::vector<WavePoint> getShaperCurve (int shaper) const;
    void setShaperCurve (int shaper, std::vector<WavePoint> points);
    /** Plays a shaper's curve now (audition). Any thread. */
    void triggerShaper (int shaper)               { engine.triggerShaper (shaper); }

    uint32_t getRandomSeed (int index) const;
    void setRandomSeed (int index, uint32_t seed);

    void startMidiLearn (int parameterIndex)      { clearMidiForParameter (parameterIndex); midiLearnTarget.store (parameterIndex); }
    void cancelMidiLearn()                        { midiLearnTarget.store (-1); }
    int  getMidiLearnTarget() const noexcept      { return midiLearnTarget.load(); }
    int  getCcForParameter (int parameterIndex) const;
    void clearMidiForParameter (int parameterIndex);

    int  getSubBlockSize() const noexcept         { return subBlockSize; }
    void setSubBlockSize (int samples);

    /** The modulators the user has added, in the order they chose. */
    const std::vector<int>& getModules() const noexcept   { return modules; }
    bool isModuleActive (int module) const;
    void addModule (int module);
    /** Removes a module from the chain, along with its connections. */
    void removeModule (int module);
    void moveModule (int module, int newIndex);

    int  getThemeIndex() const noexcept           { return themeIndex; }
    void setThemeIndex (int t)                    { themeIndex = t; }

    const SlotMapEntry& getSlotMapEntry (int k) const   { return slotMap[(size_t) k]; }
    RemappableParameter* getSlotParameter (int k) const { return slotParams[(size_t) k]; }

    juce::String getLastError() const             { return lastError; }
    void setStatus (const juce::String& s)        { lastError = s; sendChangeMessage(); }
    void removeConnectionsFrom (int source);
    int findParameterIndex (const juce::String& paramId) const;

    bool savePresetFile (const juce::File& f);
    bool loadPresetFile (const juce::File& f);
    static juce::File getPresetDirectory();

private:
    //==============================================================================
    void timerCallback() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    void rebuild();
    void applyNewState (RtState& st) noexcept;
    void applyTargets (RtState& st, int numSamples) noexcept;
    void handleMidi (juce::MidiBuffer& midi) noexcept;
    void handleSlotAutomation (RtState& st) noexcept;
    void processSlotEvents();
    void retireSlot (std::unique_ptr<HostedSlot> slot);
    void autoMapSlot (const HostedSlot& slot);
    void unmapSlot (const juce::String& uid);
    void refreshSlotParamNames();
    void updateLatency();

    juce::ValueTree createStateTree();
    void restoreStateTree (const juce::ValueTree& tree);

    int mainChannels() const;
    static BusesProperties makeBuses();

    //==============================================================================
    std::array<RemappableParameter*, kNumSlotParams> slotParams {};
    juce::AudioProcessorValueTreeState state;
    ParameterRefs refs;
    juce::SharedResourcePointer<PluginCatalog> catalog;

    ModEngine engine;
    EngineSettings settings;
    SubBlockPlayHead subPlayHead;
    RtSwap<RtState> rt;
    uint64_t appliedGeneration = 0;

    // Model (message thread, guarded by modelLock for state save/restore).
    juce::CriticalSection modelLock;
    std::vector<std::unique_ptr<HostedSlot>> slots;
    std::vector<std::pair<uint64_t, std::unique_ptr<HostedSlot>>> graveyard;
    ModMatrix matrix;
    std::map<juce::String, SnapshotEntry> snapshots;
    std::array<bool, kNumSnapshots> snapshotFilled {};
    std::array<std::vector<WavePoint>, kNumLfos> customWaves;
    std::array<std::vector<WavePoint>, kNumShapers> shaperCurves;
    std::array<uint32_t, kNumRandoms> randomSeeds {};
    std::array<SlotMapEntry, kNumSlotParams> slotMap;
    std::map<juce::String, std::pair<juce::String, juce::String>> previousTargets; // key -> (slot uid, param id)
    std::map<juce::String, int> slotParamLookup;   // "uid:paramId" -> DAW slot
    int subBlockSize = kDefaultSubBlock;
    int themeIndex = 0;
    std::vector<int> modules { modLfo0 };
    int armedSource = -1;
    juce::String lastError;
    std::atomic<bool> rebuildPending { false };

    juce::ListenerList<Listener> slotListeners;

    struct ProgramCapture;
    std::unique_ptr<ProgramCapture> programCapture;
    void pollProgramCapture();
    void finishProgramCapture (bool captured);

    // Audio-side
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    std::atomic<bool> audioActive { false };
    bool wasPlaying = false;
    juce::MidiBuffer midiFiltered, midiSub, midiSlot;
    std::array<float, kNumSlotParams> lastSlotValue {};
    juce::LinearSmoothedValue<float> outputGain;

    // MIDI learn
    std::array<std::atomic<int>, 128> ccToParam;
    std::atomic<int> midiLearnTarget { -1 };
    std::atomic<bool> midiLearnCompleted { false };
    juce::AbstractFifo ccFifo { 512 };
    std::array<std::pair<int, float>, 512> ccEvents {};

    JUCE_DECLARE_WEAK_REFERENCEABLE (ModdMuddProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModdMuddProcessor)
};

} // namespace mm
