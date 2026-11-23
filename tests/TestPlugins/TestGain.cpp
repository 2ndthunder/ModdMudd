// A minimal VST3 effect used by the hosting test: multiplies the input by a
// "gain" parameter with no smoothing, so modulation steps are visible in the
// output.

#include <juce_audio_processors/juce_audio_processors.h>

class TestGain final : public juce::AudioProcessor
{
public:
    TestGain()
        : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Out", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (gain = new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Gain",
                                                            juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
        addParameter (mode = new juce::AudioParameterChoice (juce::ParameterID { "mode", 1 }, "Mode",
                                                             juce::StringArray { "A", "B", "C" }, 0));
    }

    const juce::String getName() const override        { return "MuddTestGain"; }
    void prepareToPlay (double, int) override          {}
    void releaseResources() override                   {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainInputChannelSet() == l.getMainOutputChannelSet() && ! l.getMainOutputChannelSet().isDisabled();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.applyGain (gain->get());
    }

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override                    { return true; }
    bool acceptsMidi() const override                  { return false; }
    bool producesMidi() const override                 { return false; }
    double getTailLengthSeconds() const override       { return 0.0; }
    // Factory presets, so the preset blender can capture from them.
    int getNumPrograms() override                      { return 3; }
    int getCurrentProgram() override                   { return currentProgram; }
    const juce::String getProgramName (int i) override { return juce::StringArray { "Default", "Quiet", "Loud" }[i]; }

    void setCurrentProgram (int i) override
    {
        currentProgram = juce::jlimit (0, 2, i);
        const float gains[] { 0.5f, 0.1f, 0.9f };
        gain->setValueNotifyingHost (gain->convertTo0to1 (gains[currentProgram]));
        mode->setValueNotifyingHost (mode->convertTo0to1 ((float) currentProgram));
    }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& mb) override
    {
        juce::MemoryOutputStream s (mb, false);
        s.writeFloat (gain->get());
        s.writeInt (mode->getIndex());
    }

    void setStateInformation (const void* data, int size) override
    {
        juce::MemoryInputStream s (data, (size_t) size, false);
        if (size >= 8)
        {
            *gain = s.readFloat();
            *mode = s.readInt();
        }
    }

private:
    juce::AudioParameterFloat* gain = nullptr;
    juce::AudioParameterChoice* mode = nullptr;
    int currentProgram = 0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestGain(); }
