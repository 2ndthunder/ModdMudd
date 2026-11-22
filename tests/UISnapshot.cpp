// Development tool: renders the ModdMudd editor to PNG files.
// Usage: ModdMuddUISnapshot <output-dir> [plug-in.vst3 to host]

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/Plugin/PluginProcessor.h"
#include "../Source/UI/PluginEditor.h"

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

    // Snapping check: with the plug-in's own window showing, ModdMudd's
    // window should be exactly as wide as it (or the panel minimum), and
    // exactly the panel taller.
    std::function<juce::AudioProcessorEditor* (juce::Component&)> findHosted = [&] (juce::Component& c) -> juce::AudioProcessorEditor*
    {
        for (auto* child : c.getChildren())
        {
            if (auto* e = dynamic_cast<juce::AudioProcessorEditor*> (child))
                return e;
            if (auto* found = findHosted (*child))
                return found;
        }
        return nullptr;
    };

    if (auto* hostedEditor = findHosted (*editor))
    {
        const int expectedW = juce::jmax (mm::ModdMuddEditor::minPanelWidth, hostedEditor->getWidth());
        const int expectedH = hostedEditor->getHeight() + mm::ModdMuddEditor::panelHeight;
        std::printf ("snap: plug-in window %dx%d at (%d,%d); ModdMudd window %dx%d (expected %dx%d) -> %s\n",
                     hostedEditor->getWidth(), hostedEditor->getHeight(), hostedEditor->getX(), hostedEditor->getY(),
                     editor->getWidth(), editor->getHeight(), expectedW, expectedH,
                     (editor->getWidth() == expectedW && editor->getHeight() == expectedH) ? "OK" : "MISMATCH");
    }
    else
    {
        std::printf ("snap: no hosted plug-in window to check\n");
    }

    if (auto* view = editor->findChildWithID ("view"))
        if (auto* b = dynamic_cast<juce::Button*> (view); b != nullptr && b->getButtonText() == "Parameters")
            b->onClick();   // triggerClick() is asynchronous

    const char* names[] { "lfo1", "lfo2", "lfo3", "lfo4", "rnd1", "rnd2", "macros", "xy", "blend", "env1", "env2", "matrix" };

    for (int width : { mm::ModdMuddEditor::minPanelWidth, 1200 })
    {
        editor->setSize (width, 360 + mm::ModdMuddEditor::panelHeight);

        for (int t = 0; t < 12; ++t)
        {
            if (t != 0 && t != 4 && t != 6 && t != 7 && t != 8 && t != 9 && t != 11)
                continue;

            if (auto* tab = dynamic_cast<juce::Button*> (editor->findChildWithID ("tab" + juce::String (t))))
                tab->onClick();

            save (*editor, outDir.getChildFile (juce::String ("editor_") + juce::String (width) + "_" + names[t] + ".png"));
        }
    }

    editor.reset();
    proc->releaseResources();
    proc.reset();
    return 0;
}
