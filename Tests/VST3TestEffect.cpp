#include <juce_audio_processors/juce_audio_processors.h>

class StudioForgeVst3TestEffect final : public juce::AudioProcessor
{
public:
    StudioForgeVst3TestEffect()
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        gain = new juce::AudioParameterFloat("gain", "Gain", { 0.0f, 1.0f }, 0.25f);
        addParameter(gain);
    }

    const juce::String getName() const override { return "StudioForge VST3 Test Effect"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.applyGain(channel, 0, buffer.getNumSamples(), gain->get());
    }
    using AudioProcessor::processBlock;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    void getStateInformation(juce::MemoryBlock& destination) override
    {
        auto state = std::make_unique<juce::XmlElement>("StudioForgeVst3TestEffect");
        state->setAttribute("gain", static_cast<double>(gain->get()));
        juce::AudioProcessor::copyXmlToBinary(*state, destination);
    }
    void setStateInformation(const void* data, int sizeInBytes) override
    {
        if (auto state = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes))
            if (state->hasTagName("StudioForgeVst3TestEffect"))
                *gain = static_cast<float>(state->getDoubleAttribute("gain", gain->get()));
    }

private:
    juce::AudioParameterFloat* gain = nullptr;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StudioForgeVst3TestEffect();
}
