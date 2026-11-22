// A minimal VST3 instrument used by the hosting test: a sine voice per note,
// scaled by a "level" parameter.

#include <juce_audio_processors/juce_audio_processors.h>

class TestSynth final : public juce::AudioProcessor
{
public:
    TestSynth()
        : AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (level = new juce::AudioParameterFloat (juce::ParameterID { "level", 1 }, "Level",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));
    }

    const juce::String getName() const override        { return "MuddTestSynth"; }
    void prepareToPlay (double sr, int) override       { sampleRate = sr; }
    void releaseResources() override                   {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        int pos = 0;

        auto render = [&] (int end)
        {
            for (; pos < end; ++pos)
            {
                float v = 0.0f;
                if (note >= 0)
                {
                    v = std::sin (phase) * level->get();
                    phase += (float) (juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (note) / sampleRate);
                }
                for (int c = 0; c < buffer.getNumChannels(); ++c)
                    buffer.setSample (c, pos, v);
            }
        };

        for (const auto meta : midi)
        {
            render (meta.samplePosition);
            const auto m = meta.getMessage();
            if (m.isNoteOn())                                    note = m.getNoteNumber();
            else if (m.isNoteOff() && m.getNoteNumber() == note) note = -1;
        }

        render (buffer.getNumSamples());
        midi.clear();
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override                    { return false; }
    bool acceptsMidi() const override                  { return true; }
    bool producesMidi() const override                 { return false; }
    double getTailLengthSeconds() const override       { return 0.0; }
    int getNumPrograms() override                      { return 1; }
    int getCurrentProgram() override                   { return 0; }
    void setCurrentProgram (int) override              {}
    const juce::String getProgramName (int) override   { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    juce::AudioParameterFloat* level = nullptr;
    double sampleRate = 44100.0;
    float phase = 0.0f;
    int note = -1;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestSynth(); }
