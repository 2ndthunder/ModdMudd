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

                    c.source = mm::srcMacro0;      // a second modulator on the same parameter
                    c.depth = 0.2f;
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

    // Put every modulator in the chain so each editor can be rendered.
    for (int m = 0; m < mm::kNumModules; ++m)
        proc->addModule (m);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
    auto* moddEditor = dynamic_cast<mm::ModdMuddEditor*> (editor.get());

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

    struct Shot { int module; const char* name; };
    const Shot shots[] { { mm::modLfo0, "lfo1" }, { mm::modRandom0, "rnd1" }, { mm::modMacros, "macros" }, { mm::modXy, "xy" },
                         { mm::modBlend, "blend" }, { mm::modEnv0, "follow1" }, { mm::modShaper0, "shaper1" }, { -1, "routing" } };

    for (int width : { mm::ModdMuddEditor::minPanelWidth, 1200 })
    {
        editor->setSize (width, 360 + mm::ModdMuddEditor::panelHeight);

        for (auto& shot : shots)
        {
            if (shot.module >= 0) moddEditor->selectModule (shot.module);
            else                  moddEditor->showRouting();

            save (*editor, outDir.getChildFile (juce::String ("editor_") + juce::String (width) + "_" + shot.name + ".png"));
        }
    }

    // The pop-up list of LFO 1's targets, drawn with the editor's look.
    {
        auto list = mm::createTargetList (*proc, mm::srcLfo0);
        list->setLookAndFeel (&editor->getLookAndFeel());
        auto img = juce::Image (juce::Image::ARGB, list->getWidth(), list->getHeight(), true);
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xff34383e));
        list->paintEntireComponent (g, true);
        auto f = outDir.getChildFile ("targets_popup.png");
        f.deleteFile();
        juce::FileOutputStream out (f);
        juce::PNGImageFormat().writeImageToStream (img, out);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        list->setLookAndFeel (nullptr);
    }

    // The guided tour: a few steps at the default size.
    {
        editor->setSize (900, 360 + mm::ModdMuddEditor::panelHeight);
        moddEditor->selectModule (mm::modLfo0);
        moddEditor->startTutorial();

        if (auto* tour = dynamic_cast<mm::TutorialOverlay*> (editor->findChildWithID ("tutorialOverlay")))
        {
            for (int step : { 0, 1, 3, 6, 7, tour->getNumSteps() - 1 })
            {
                tour->showStep (step);
                save (*editor, outDir.getChildFile ("tutorial_step" + juce::String (step + 1) + ".png"));
            }
            editor->removeChildComponent (tour);
        }
    }

    // A short chain, as a new user would build it.
    for (int m = mm::kNumModules - 1; m >= 0; --m)
        if (m != mm::modLfo0 && m != mm::modMacros && m != mm::modEnv0)
            proc->removeModule (m);
    proc->dispatchPendingMessages();   // no message loop here
    moddEditor->selectModule (mm::modLfo0);
    editor->setSize (900, 360 + mm::ModdMuddEditor::panelHeight);
    save (*editor, outDir.getChildFile ("editor_chain.png"));

    // And an empty chain.
    for (int m : std::vector<int> (proc->getModules()))
        proc->removeModule (m);
    proc->dispatchPendingMessages();
    save (*editor, outDir.getChildFile ("editor_empty_chain.png"));

    editor.reset();
    proc->releaseResources();
    proc.reset();
    return 0;
}
