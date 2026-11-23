// End-to-end hosting test.
//
// A headless VST3 host loads ModdMudd (FX and Instrument builds) as real VST3
// plug-ins. ModdMudd in turn loads small test plug-ins (also real VST3s) and
// modulates them. The test checks the audio that comes out.
//
// Usage: ModdMuddHostTest <ModdMudd FX.vst3> <ModdMudd Instrument.vst3> <MuddTestGain.vst3> <MuddTestSynth.vst3>

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include <cstdio>

using namespace juce;

static int failures = 0;

static void check (bool ok, const String& what)
{
    std::printf ("%s %s\n", ok ? "  PASS" : "  FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

static void note (const String& what)
{
    std::printf ("  NOTE %s\n", what.toRawUTF8());
}

struct Loaded
{
    std::unique_ptr<AudioPluginInstance> instance;
    PluginDescription description;
};

static Loaded load (AudioPluginFormat& format, const String& path, double sampleRate, int blockSize)
{
    Loaded result;
    OwnedArray<PluginDescription> types;
    format.findAllTypesForFile (types, path);

    if (types.isEmpty())
    {
        std::printf ("Couldn't scan %s\n", path.toRawUTF8());
        return result;
    }

    String error;
    result.description = *types[0];
    result.instance = format.createInstanceFromDescription (result.description, sampleRate, blockSize, error);

    if (result.instance == nullptr)
        std::printf ("Couldn't load %s: %s\n", path.toRawUTF8(), error.toRawUTF8());

    return result;
}

static void pump (int ms)
{
    MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

static String paramIdFor (AudioPluginInstance& p, const String& name)
{
    for (auto* param : p.getParameters())
        if (param->getName (64) == name)
            if (auto* hosted = dynamic_cast<HostedAudioProcessorParameter*> (param))
                return hosted->getParameterID();

    return {};
}

static ValueTree makeState (const PluginDescription& hostedDesc, const String& paramId, bool withModulation,
                            const String& build, int source = 0, float depth = 0.5f)
{
    ValueTree root ("ModdMudd");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("build", build, nullptr);

    ValueTree chain ("Chain");
    ValueTree slot ("Slot");
    slot.setProperty ("uid", "test-slot", nullptr);
    if (auto xml = hostedDesc.createXml())
        slot.appendChild (ValueTree::fromXml (*xml), nullptr);
    chain.appendChild (slot, nullptr);
    root.appendChild (chain, nullptr);

    ValueTree matrix ("Matrix");
    if (withModulation)
    {
        ValueTree c ("Conn");
        c.setProperty ("source", source, nullptr);      // default: LFO 1 (1 Hz sine)
        c.setProperty ("internal", false, nullptr);
        c.setProperty ("slot", "test-slot", nullptr);
        c.setProperty ("param", paramId, nullptr);
        c.setProperty ("depth", depth, nullptr);
        matrix.appendChild (c, nullptr);

        ValueTree d ("Dest");
        d.setProperty ("key", "h:test-slot:" + paramId, nullptr);
        d.setProperty ("min", 0.0f, nullptr);
        d.setProperty ("max", 1.0f, nullptr);
        d.setProperty ("smooth", 0.0f, nullptr);         // no smoothing so steps are exact
        matrix.appendChild (d, nullptr);
    }
    root.appendChild (matrix, nullptr);

    return root;
}

static void setState (AudioPluginInstance& p, const ValueTree& tree)
{
    // ModdMudd's own state, wrapped the way JUCE's VST3 host stores a VST3
    // plug-in's component state.
    MemoryBlock inner;
    if (auto xml = tree.createXml())
        AudioProcessor::copyXmlToBinary (*xml, inner);

    XmlElement wrapper ("VST3PluginState");
    wrapper.createNewChildElement ("IComponent")->addTextElement (inner.toBase64Encoding());

    MemoryBlock mb;
    AudioProcessor::copyXmlToBinary (wrapper, mb);
    p.setStateInformation (mb.getData(), (int) mb.getSize());

    // A JUCE plug-in on Linux with no host run loop has its own message
    // thread, and ModdMudd creates hosted plug-ins there; give it time.
    pump (100);
    Thread::sleep (700);

    if (getenv ("MODDMUDD_TEST_VERBOSE") != nullptr)
    {
        MemoryBlock out;
        p.getStateInformation (out);
        if (auto outer = AudioProcessor::getXmlFromBinary (out.getData(), (int) out.getSize()))
            if (auto* comp = outer->getChildByName ("IComponent"))
                if (MemoryBlock innerState; innerState.fromBase64Encoding (comp->getAllSubText()))
                    if (auto xml = AudioProcessor::getXmlFromBinary (innerState.getData(), (int) innerState.getSize()))
                        if (auto* chain = xml->getChildByName ("Chain"))
                std::printf ("    [state] %s\n", chain->toString (XmlElement::TextFormat().singleLine()).substring (0, 300).toRawUTF8());
    }
}

struct Stats
{
    float min = 1.0e9f, max = -1.0e9f;
    double sumSquares = 0.0;
    int64 count = 0;
    int maxDistinctPerBlock = 0;
    float firstSample = 0.0f;
    bool constant = true;
};

static Stats runFx (AudioPluginInstance& p, int blocks, int blockSize)
{
    Stats s;
    AudioBuffer<float> buffer (2, blockSize);
    MidiBuffer midi;

    for (int b = 0; b < blocks; ++b)
    {
        for (int c = 0; c < 2; ++c)
            FloatVectorOperations::fill (buffer.getWritePointer (c), 1.0f, blockSize);

        p.processBlock (buffer, midi);

        std::vector<float> distinct;
        for (int i = 0; i < blockSize; ++i)
        {
            const float v = buffer.getSample (0, i);
            if (b == 0 && i == 0) s.firstSample = v;
            if (std::abs (v - s.firstSample) > 1.0e-4f) s.constant = false;
            s.min = jmin (s.min, v);
            s.max = jmax (s.max, v);
            s.sumSquares += (double) v * v;
            ++s.count;

            if (distinct.empty() || std::abs (distinct.back() - v) > 1.0e-6f)
                distinct.push_back (v);
        }

        s.maxDistinctPerBlock = jmax (s.maxDistinctPerBlock, (int) distinct.size());
    }

    return s;
}

int main (int argc, char* argv[])
{
    if (argc < 5)
    {
        std::printf ("usage: ModdMuddHostTest <fx.vst3> <instrument.vst3> <testgain.vst3> <testsynth.vst3>\n");
        return 2;
    }

    MessageManager::getInstance();

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    VST3PluginFormatHeadless format;

    const String fxPath (argv[1]), instPath (argv[2]), gainPath (argv[3]), synthPath (argv[4]);

    //==========================================================================
    std::printf ("Loading test plug-ins directly\n");
    auto gain = load (format, gainPath, sampleRate, blockSize);
    auto synth = load (format, synthPath, sampleRate, blockSize);
    check (gain.instance != nullptr, "MuddTestGain loads");
    check (synth.instance != nullptr, "MuddTestSynth loads");
    if (gain.instance == nullptr || synth.instance == nullptr)
        return 1;

    const auto gainId = paramIdFor (*gain.instance, "Gain");
    const auto levelId = paramIdFor (*synth.instance, "Level");
    check (gainId.isNotEmpty(), "found the Gain parameter ID (" + gainId + ")");

    //==========================================================================
    std::printf ("\nModdMudd FX hosting MuddTestGain\n");
    auto fx = load (format, fxPath, sampleRate, blockSize);
    check (fx.instance != nullptr, "ModdMudd FX loads as a VST3");
    if (fx.instance == nullptr)
        return 1;

    check (fx.instance->getParameters().size() > 256, "ModdMudd exposes its parameters ("
                                                         + String (fx.instance->getParameters().size()) + ")");

    fx.instance->prepareToPlay (sampleRate, blockSize);

    {
        auto s = runFx (*fx.instance, 20, blockSize);
        check (s.constant && std::abs (s.firstSample - 1.0f) < 1.0e-5f, "empty chain passes audio through unchanged");
    }

    setState (*fx.instance, makeState (gain.description, gainId, false, "fx"));
    {
        auto s = runFx (*fx.instance, 40, blockSize);
        check (s.constant && std::abs (s.firstSample - 0.5f) < 1.0e-3f,
               "hosted plug-in processes audio (gain 0.5 -> " + String (s.firstSample, 4) + ")");
    }

    setState (*fx.instance, makeState (gain.description, gainId, true, "fx"));
    {
        auto s = runFx (*fx.instance, (int) (2.0 * sampleRate / blockSize), blockSize);   // two LFO cycles
        check (s.min < 0.02f && s.max > 0.98f,
               "LFO sweeps the hosted gain across its range (min " + String (s.min, 3) + ", max " + String (s.max, 3) + ")");
        check (s.maxDistinctPerBlock >= 12,
               "modulation updates inside each host block (" + String (s.maxDistinctPerBlock) + " steps per 512-sample block)");
    }

    // Round trip: save ModdMudd's state and load it into a fresh instance.
    MemoryBlock saved;
    fx.instance->getStateInformation (saved);
    check (saved.getSize() > 0, "ModdMudd saves its state (" + String ((int) saved.getSize()) + " bytes)");

    {
        auto fx2 = load (format, fxPath, sampleRate, blockSize);
        fx2.instance->prepareToPlay (sampleRate, blockSize);
        fx2.instance->setStateInformation (saved.getData(), (int) saved.getSize());
        pump (100);
        Thread::sleep (700);
        auto s = runFx (*fx2.instance, (int) (2.0 * sampleRate / blockSize), blockSize);
        check (s.min < 0.02f && s.max > 0.98f, "restored state re-loads the hosted plug-in and its modulation (min "
                                                 + String (s.min, 3) + ", max " + String (s.max, 3) + ")");
        if (getenv ("MODDMUDD_TEST_VERBOSE") != nullptr)
        {
            MemoryBlock out;
            fx2.instance->getStateInformation (out);
            if (auto outer = AudioProcessor::getXmlFromBinary (out.getData(), (int) out.getSize()))
                if (auto* comp = outer->getChildByName ("IComponent"))
                    if (MemoryBlock innerState; innerState.fromBase64Encoding (comp->getAllSubText()))
                        if (auto xml = AudioProcessor::getXmlFromBinary (innerState.getData(), (int) innerState.getSize()))
                        {
                            for (auto* child : xml->getChildIterator())
                                if (child->getTagName() != "PARAMS")
                                    std::printf ("    [fx2] %s\n", child->toString (XmlElement::TextFormat().singleLine().withoutHeader()).substring (0, 4000).toRawUTF8());
                        }
        }

        // Parameter names follow the hosted plug-in. ModdMudd's first DAW
        // slot is mapped to the hosted "Gain" parameter.
        String name;
        for (auto* p : fx2.instance->getParameters())
        {
            const auto n = p->getName (64);
            if (n == "Gain" || n.startsWith ("Slot 001"))
            {
                name = n;
                break;
            }
        }

        if (getenv ("MODDMUDD_TEST_VERBOSE") != nullptr)
            for (int i = 0; i < 5; ++i)
                std::printf ("    [param %d] %s\n", i, fx2.instance->getParameters()[i]->getName (64).toRawUTF8());

        if (name == "Gain")
            check (true, "DAW slot parameter 1 is named after the hosted parameter (\"Gain\")");
        else
            note ("this host did not refresh parameter names after loading (saw \"" + name + "\"); "
                  "renaming depends on the DAW re-reading parameter info");

        fx2.instance->releaseResources();
    }

    // Removing the modulation puts the parameter back to its own value.
    setState (*fx.instance, makeState (gain.description, gainId, false, "fx"));
    {
        runFx (*fx.instance, 4, blockSize);
        auto s = runFx (*fx.instance, 20, blockSize);
        check (s.constant && std::abs (s.firstSample - 0.5f) < 1.0e-3f,
               "without modulation the parameter returns to its base value (" + String (s.firstSample, 4) + ")");
    }

    // A session that references a plug-in that isn't installed.
    {
        PluginDescription ghost = gain.description;
        ghost.name = "NotInstalledPlugin";
        ghost.fileOrIdentifier = "/nonexistent/NotInstalledPlugin.vst3";
        ghost.uniqueId = 12345;
        ghost.manufacturerName = "Nobody";

        setState (*fx.instance, makeState (ghost, "1", true, "fx"));
        auto s = runFx (*fx.instance, 10, blockSize);
        check (s.constant && std::abs (s.firstSample - 1.0f) < 1.0e-5f, "missing plug-in: audio passes through");

        MemoryBlock mb;
        fx.instance->getStateInformation (mb);
        bool kept = false;
        if (auto outer = AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()))
            if (auto* comp = outer->getChildByName ("IComponent"))
                if (MemoryBlock innerState; innerState.fromBase64Encoding (comp->getAllSubText()))
                    if (auto xml = AudioProcessor::getXmlFromBinary (innerState.getData(), (int) innerState.getSize()))
                        kept = xml->toString().contains ("NotInstalledPlugin");
        check (kept, "missing plug-in is kept when the session is saved again");
    }

    fx.instance->releaseResources();

    //==========================================================================
    std::printf ("\nModdMudd Instrument hosting MuddTestSynth\n");
    auto inst = load (format, instPath, sampleRate, blockSize);
    check (inst.instance != nullptr, "ModdMudd Instrument loads as a VST3");
    if (inst.instance != nullptr)
    {
        inst.instance->prepareToPlay (sampleRate, blockSize);
        auto tree = makeState (synth.description, levelId, true, "instrument");
        setState (*inst.instance, tree);

        AudioBuffer<float> buffer (2, blockSize);
        double energy = 0.0;
        float peakFirstHalf = 0.0f, peakSecondHalf = 0.0f;
        const int blocks = (int) (1.0 * sampleRate / blockSize);

        for (int b = 0; b < blocks; ++b)
        {
            MidiBuffer midi;
            if (b == 0)
                midi.addEvent (MidiMessage::noteOn (1, 69, (uint8) 100), 0);

            buffer.clear();
            inst.instance->processBlock (buffer, midi);
            energy += buffer.getRMSLevel (0, 0, blockSize);

            // LFO 1 (1 Hz) is above the base level for the first half second.
            const float peak = buffer.getMagnitude (0, 0, blockSize);
            if (b > 2 && b < blocks / 2 - 2)   peakFirstHalf = jmax (peakFirstHalf, peak);
            if (b > blocks / 2 + 2)            peakSecondHalf = jmax (peakSecondHalf, peak);
        }

        check (energy / blocks > 0.05, "MIDI reaches the hosted instrument and it makes sound");
        check (peakFirstHalf > peakSecondHalf + 0.1f,
               "LFO modulates the instrument's level (" + String (peakFirstHalf, 2) + " vs " + String (peakSecondHalf, 2) + ")");

        // Shaper 1 (source 18), note-triggered: its default curve rises in
        // 60 ms and falls back over a second. Depth -0.8 against the synth's
        // level of 0.8 ducks the note at the start and lets it return.
        setState (*inst.instance, makeState (synth.description, levelId, true, "instrument", 18, -0.8f));

        float early = 0.0f, late = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            MidiBuffer midi;
            if (b == 0)
                midi.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);

            buffer.clear();
            inst.instance->processBlock (buffer, midi);
            const float peak = buffer.getMagnitude (0, 0, blockSize);
            const double ms = b * blockSize * 1000.0 / sampleRate;

            if (ms > 50.0 && ms < 75.0)   early = jmax (early, peak);
            if (ms > 900.0)               late = jmax (late, peak);
        }

        check (early < 0.2f && late > 0.6f,
               "a note-triggered Shaper envelope modulates the instrument (" + String (early, 2) + " near the peak, "
                   + String (late, 2) + " at the end)");

        inst.instance->releaseResources();
    }

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);

    fx.instance.reset();
    inst.instance.reset();
    gain.instance.reset();
    synth.instance.reset();
    pump (100);
    return failures == 0 ? 0 : 1;
}
