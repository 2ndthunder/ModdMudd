// Processor-level test: drives ModdMudd's processor directly (no VST3 layer
// in between) with a running message loop, for features that work over time
// on the message thread.
//
// Usage: ModdMuddProcessorTest <MuddTestGain.vst3>

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/Plugin/PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    std::printf ("%s %s\n", ok ? "  PASS" : "  FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

/** Runs the test as a sequence of steps on a timer inside the message loop. */
struct Runner final : juce::Timer
{
    Runner (mm::ModdMuddProcessor& p) : proc (p) {}

    float runAudio (int blocks)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        float last = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            for (int c = 0; c < 2; ++c)
                juce::FloatVectorOperations::fill (buffer.getWritePointer (c), 1.0f, 512);
            proc.processBlock (buffer, midi);
            last = buffer.getSample (0, 511);
        }
        return last;
    }

    void setParam (const juce::String& id, float value)
    {
        auto* p = proc.getValueTreeState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    void timerCallback() override
    {
        // Wait while a preset capture is in progress.
        if (proc.isCapturingProgram())
        {
            runAudio (2);
            return;
        }

        switch (step++)
        {
            case 0:
                check (std::abs (runAudio (20) - 0.5f) < 1.0e-3f, "hosted effect runs at its default gain (0.5)");
                proc.captureSnapshotFromProgram (0, 0, 1);   // "Quiet" -> A
                break;

            case 1:
                check (proc.hasSnapshot (0), "snapshot A captured from the plug-in's \"Quiet\" preset");
                check (std::abs (runAudio (20) - 0.5f) < 1.0e-3f, "the plug-in is put back to its own settings afterwards");
                proc.captureSnapshotFromProgram (1, 0, 2);   // "Loud" -> B
                break;

            case 2:
                check (proc.hasSnapshot (1), "snapshot B captured from the plug-in's \"Loud\" preset");
                setParam (mm::ids::blendOn, 1.0f);
                setParam (mm::ids::blendY, 0.0f);
                setParam (mm::ids::blendX, 0.0f);
                break;   // the blender's routing is rebuilt on the processor's timer

            case 3:
            {
                const float atA = runAudio (40);
                check (std::abs (atA - 0.1f) < 0.01f, "blend fully on A gives the Quiet gain (" + juce::String (atA, 3) + ")");
                setParam (mm::ids::blendX, 1.0f);
                break;
            }

            case 4:
            {
                const float atB = runAudio (40);
                check (std::abs (atB - 0.9f) < 0.01f, "blend fully on B gives the Loud gain (" + juce::String (atB, 3) + ")");
                setParam (mm::ids::blendX, 0.5f);
                break;
            }

            case 5:
            {
                const float mid = runAudio (40);
                check (std::abs (mid - 0.5f) < 0.01f, "half way between A and B interpolates (" + juce::String (mid, 3) + ")");
                setParam (mm::ids::blendOn, 0.0f);
                break;
            }

            case 6:
            {
                const float off = runAudio (40);
                check (std::abs (off - 0.5f) < 0.01f, "blender off returns to the plug-in's own value (" + juce::String (off, 3) + ")");
                break;
            }

            default:
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
        }
    }

    mm::ModdMuddProcessor& proc;
    int step = 0;
};

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::printf ("usage: ModdMuddProcessorTest <MuddTestGain.vst3>\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<mm::ModdMuddProcessor> proc (static_cast<mm::ModdMuddProcessor*> (createPluginFilter()));
    proc->setPlayConfigDetails (2, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);

    juce::OwnedArray<juce::PluginDescription> types;
    juce::VST3PluginFormat format;
    format.findAllTypesForFile (types, argv[1]);
    check (! types.isEmpty(), "found MuddTestGain");
    if (types.isEmpty())
        return 1;

    check (proc->loadPlugin (*types[0]).isEmpty(), "ModdMudd loads MuddTestGain");
    check (proc->getSlots().size() == 1 && proc->getSlots()[0]->getInstance()->getNumPrograms() == 3,
           "the hosted plug-in reports its 3 presets");

    std::printf ("\nPreset blender: capture from the plug-in's own presets\n");
    Runner runner (*proc);
    runner.startTimer (60);
    juce::MessageManager::getInstance()->runDispatchLoop();

    proc->releaseResources();
    proc.reset();

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
