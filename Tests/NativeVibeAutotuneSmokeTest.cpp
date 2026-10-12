#include "Plugins/NativeVibeAutotuneProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
bool containsParameter(const std::vector<EffectParameterDescriptor>& parameters, const juce::String& identifier)
{
    return std::any_of(parameters.begin(), parameters.end(), [&] (const auto& parameter)
    {
        return parameter.identifier.equalsIgnoreCase(identifier);
    });
}

bool runLiveCallbackSize(NativeVibeAutotuneProcessor& vibe, int blockSize)
{
    vibe.prepareToPlay(48000.0, blockSize, 2);
    juce::AudioBuffer<float> block(2, blockSize);
    for (int blockIndex = 0; blockIndex < 900; ++blockIndex)
    {
        const auto phrasePosition = blockIndex % 140;
        const auto voiced = phrasePosition < 88;
        if (blockIndex > 0 && blockIndex % 211 == 0)
        {
            vibe.setParameterText("key", blockIndex % 422 == 0 ? "C" : "F#");
            vibe.setParameterText("scale", blockIndex % 422 == 0 ? "Major" : "Natural Minor");
        }
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto time = static_cast<float>(blockIndex * blockSize + sample) / 48000.0f;
            const auto value = voiced ? std::sin(juce::MathConstants<float>::twoPi * 220.0f * time) * 0.2f : 0.0f;
            block.setSample(0, sample, value);
            block.setSample(1, sample, value);
        }
        vibe.processBlock(block);
        for (int channel = 0; channel < block.getNumChannels(); ++channel)
            for (int sample = 0; sample < blockSize; ++sample)
                if (! std::isfinite(block.getSample(channel, sample)))
                    return false;
    }
    const auto health = vibe.getLiveProcessorHealth();
    return health.state != LiveProcessorState::unavailable
        && std::isfinite(health.wetMix)
        && health.wetMix >= 0.0f && health.wetMix <= 1.0f;
}

bool verifyUnverifiedRendererKeepsDirectMic(int blockSize)
{
    NativeVibeAutotuneProcessor vibe { "studioforge.native.vibeautotune" };
    vibe.prepareToPlay(48000.0, blockSize, 2);
    juce::AudioBuffer<float> block(2, blockSize);
    juce::AudioBuffer<float> expected(2, blockSize);
    std::uint32_t noiseState = 0x9e3779b9u;

    const auto fillQuietUnpitchedSignal = [&]
    {
        for (int sample = 0; sample < blockSize; ++sample)
        {
            noiseState = noiseState * 1664525u + 1013904223u;
            const auto value = (static_cast<float>((noiseState >> 8) & 0xffffu) / 32767.5f - 1.0f) * 0.002f;
            block.setSample(0, sample, value);
            block.setSample(1, sample, -value);
        }
    };

    // Prime the internal latency history with a deliberately unpitched,
    // below-rescue signal. A live plugin must still forward this microphone
    // signal exactly while its tracker refuses to arm wet correction.
    for (int index = 0; index < 160; ++index)
    {
        fillQuietUnpitchedSignal();
        vibe.processBlock(block);
    }

    fillQuietUnpitchedSignal();
    expected.makeCopyOf(block);
    vibe.processBlock(block);
    const auto health = vibe.getLiveProcessorHealth();
    if (health.wetMix > 0.0001f)
        return false;

    for (int channel = 0; channel < block.getNumChannels(); ++channel)
        for (int sample = 0; sample < blockSize; ++sample)
            if (std::abs(block.getSample(channel, sample) - expected.getSample(channel, sample)) > 0.000001f)
                return false;
    return true;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    NativeVibeAutotuneProcessor vibe { "studioforge.native.vibeautotune" };
    vibe.prepareToPlay(48000.0, 128, 2);

    if (! containsParameter(vibe.getParameterDescriptors(), "key")
        || ! containsParameter(vibe.getParameterDescriptors(), "scale")
        || ! vibe.setParameterText("key", "F#")
        || ! vibe.setParameterText("scale", "Natural Minor"))
    {
        std::cerr << "Vibe semantic key/scale controls are unavailable\n";
        return 1;
    }

    for (const auto callbackSize : { 32, 64, 128 })
        if (! runLiveCallbackSize(vibe, callbackSize))
        {
            std::cerr << "Vibe live safety failed at callback size " << callbackSize << "\n";
            return 1;
        }

    for (const auto callbackSize : { 32, 64, 128 })
        if (! verifyUnverifiedRendererKeepsDirectMic(callbackSize))
        {
            std::cerr << "Vibe unverified renderer altered the direct microphone at callback size "
                      << callbackSize << "\\n";
            return 1;
        }

    vibe.prepareToPlay(48000.0, 128, 2);

    // A live microphone can remain open indefinitely. Run a continuous,
    // voiced stereo signal long past the renderer's internal latency and
    // verify the tail still carries audio instead of decaying to silence.
    juce::AudioBuffer<float> block(2, 128);
    // Inserting an effect while a performer is already singing must never
    // consume a multi-second renderer warm-up. Check the first callback,
    // not merely the eventual steady state.
    for (int sample = 0; sample < block.getNumSamples(); ++sample)
    {
        const auto time = static_cast<float>(sample) / 48000.0f;
        const auto value = std::sin(juce::MathConstants<float>::twoPi * 220.0f * time) * 0.25f;
        block.setSample(0, sample, value);
        block.setSample(1, sample, value);
    }
    vibe.processBlock(block);
    double initialEnergy = 0.0;
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
        for (int sample = 0; sample < block.getNumSamples(); ++sample)
            initialEnergy += static_cast<double>(block.getSample(channel, sample))
                           * block.getSample(channel, sample);
    if (std::sqrt(initialEnergy / static_cast<double>(block.getNumSamples() * block.getNumChannels())) < 0.05)
    {
        std::cerr << "Vibe muted the microphone when inserted\n";
        return 1;
    }
    double tailEnergy = 0.0;
    int tailSamples = 0;
    for (int blockIndex = 0; blockIndex < 1200; ++blockIndex)
    {
        for (int sample = 0; sample < block.getNumSamples(); ++sample)
        {
            const auto time = static_cast<float>(blockIndex * block.getNumSamples() + sample) / 48000.0f;
            const auto value = std::sin(juce::MathConstants<float>::twoPi * 220.0f * time) * 0.25f;
            block.setSample(0, sample, value);
            block.setSample(1, sample, value);
        }
        vibe.processBlock(block);
        for (int channel = 0; channel < block.getNumChannels(); ++channel)
            for (int sample = 0; sample < block.getNumSamples(); ++sample)
            {
                const auto value = block.getSample(channel, sample);
                if (! std::isfinite(value))
                {
                    std::cerr << "Vibe emitted a non-finite sample\n";
                    return 1;
                }
                if (blockIndex >= 1100)
                {
                    tailEnergy += static_cast<double>(value) * value;
                    ++tailSamples;
                }
            }
    }
    const auto tailRms = std::sqrt(tailEnergy / static_cast<double>(tailSamples));
    if (tailRms < 0.002)
    {
        std::cerr << "Vibe output decayed to silence during sustained live input\n";
        return 1;
    }

    // Speech is discontinuous.  A live processor must return to dry during a
    // pause and recover on the next voiced phrase; its analysis gate must not
    // become an audio gate after several phrase boundaries.
    double recoveredEnergy = 0.0;
    int recoveredSamples = 0;
    for (int blockIndex = 0; blockIndex < 1800; ++blockIndex)
    {
        const auto phrasePosition = blockIndex % 180;
        const auto voiced = phrasePosition < 92;
        for (int sample = 0; sample < block.getNumSamples(); ++sample)
        {
            const auto time = static_cast<float>(blockIndex * block.getNumSamples() + sample) / 48000.0f;
            const auto value = voiced
                ? std::sin(juce::MathConstants<float>::twoPi * 196.0f * time) * 0.18f : 0.0f;
            block.setSample(0, sample, value);
            block.setSample(1, sample, value);
        }
        vibe.processBlock(block);
        if (voiced && phrasePosition > 48)
            for (int channel = 0; channel < block.getNumChannels(); ++channel)
                for (int sample = 0; sample < block.getNumSamples(); ++sample)
                {
                    const auto value = block.getSample(channel, sample);
                    if (! std::isfinite(value))
                    {
                        std::cerr << "Vibe emitted a non-finite sample after a vocal pause\n";
                        return 1;
                    }
                    recoveredEnergy += static_cast<double>(value) * value;
                    ++recoveredSamples;
                }
    }
    const auto recoveredRms = std::sqrt(recoveredEnergy / static_cast<double>(recoveredSamples));
    if (recoveredRms < 0.002)
    {
        std::cerr << "Vibe did not recover after repeated vocal pauses\n";
        return 1;
    }

    // Driver/interface corruption must be contained before entering DSP.
    block.clear();
    block.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
    block.setSample(1, 1, std::numeric_limits<float>::infinity());
    vibe.processBlock(block);
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
        for (int sample = 0; sample < block.getNumSamples(); ++sample)
            if (! std::isfinite(block.getSample(channel, sample)))
            {
                std::cerr << "Live input safety did not contain a non-finite sample\n";
                return 1;
            }

    // A host block larger than prepareToPlay promised must stay dry and must
    // be reported as protected instead of entering an unbounded callback.
    juce::AudioBuffer<float> oversized(2, 5000);
    oversized.clear();
    oversized.applyGain(0.1f);
    vibe.processBlock(oversized);
    if (vibe.getLiveProcessorHealth().state != LiveProcessorState::protectedDry)
    {
        std::cerr << "Oversized callback did not engage live protection\n";
        return 1;
    }

    auto editor = vibe.createEditor();
    if (editor == nullptr || editor->getWidth() <= 0 || editor->getHeight() <= 0)
    {
        std::cerr << "Vibe's native editor could not be created\n";
        return 1;
    }

    return 0;
}
