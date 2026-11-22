// Development tool: renders the ModdMudd editor to PNG files.
// Usage: ModdMuddUISnapshot <output-dir> [plug-in.vst3 to host]

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/Plugin/PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

static void save (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat().writeImageToStream (img, out);
    std::printf ("wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File::getCurrentWorkingDirectory());
    outDir.createDirectory();

    std::unique_ptr<mm::ModdMuddProcessor> proc (static_cast<mm::ModdMuddProcessor*> (createPluginFilter()));
    proc->setPlayConfigDetails (2, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);

    if (argc > 2)
    {
        juce::OwnedArray<juce::PluginDescription> types;
        juce::VST3PluginFormat fmt;
        fmt.findAllTypesForFile (types, argv[2]);
        if (! types.isEmpty())
        {
            std::printf ("load: %s\n", proc->loadPlugin (*types[0]).toRawUTF8());

            // A couple of connections so the matrix and indicators have content.
            auto& slot = *proc->getSlots()[0];
            for (int pi = 0; pi < slot.getNumParams(); ++pi)
                if (slot.isModulatable (pi))
                {
                    mm::Connection c;
                    c.source = mm::srcLfo0;
                    c.dest = mm::Destination::hosted (slot.getUid(), slot.getParamId (pi));
                    c.depth = 0.4f;
                    proc->addConnection (c);
                    break;
                }

            mm::Connection x;
            x.source = mm::srcRandom0;
            x.dest = mm::Destination::internalParam (mm::lfoInternal (0, 0));
            x.depth = 0.3f;
            proc->addConnection (x);
        }
    }

    // Run some audio so the displays move.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 40; ++i)
    {
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 512; ++s)
                buffer.setSample (c, s, 0.3f * std::sin ((float) (i * 512 + s) * 0.03f));
        proc->processBlock (buffer, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
    // (modal loops are disabled in the plug-in build, so no message pumping here)

    if (auto* view = editor->findChildWithID ("view"))
        if (auto* b = dynamic_cast<juce::Button*> (view); b != nullptr && b->getButtonText() == "Parameters")
            b->onClick();   // triggerClick() is asynchronous

    // (modal loops are disabled in the plug-in build, so no message pumping here)

    const char* names[] { "lfo1", "lfo2", "lfo3", "lfo4", "rnd1", "rnd2", "macros", "xy", "blend", "env1", "env2", "matrix" };
    for (int t = 0; t < 12; ++t)
    {
        if (t != 0 && t != 4 && t != 6 && t != 7 && t != 8 && t != 9 && t != 11)
            continue;

        if (auto* tab = dynamic_cast<juce::Button*> (editor->findChildWithID ("tab" + juce::String (t))))
            tab->onClick();   // triggerClick() is asynchronous

        // (modal loops are disabled in the plug-in build, so no message pumping here)
        save (*editor, outDir.getChildFile (juce::String ("editor_") + names[t] + ".png"));
    }

    editor.reset();
    proc->releaseResources();
    proc.reset();
    return 0;
}
