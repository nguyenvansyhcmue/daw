#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

class LockFreeBuffer final
{
public:
    explicit LockFreeBuffer(int numChannels = 2, int capacitySamples = 4096)
        : buffer(numChannels, capacitySamples),
          fifo(capacitySamples)
    {
    }

    void write(const juce::AudioBuffer<float>& source)
    {
        if (source.getNumChannels() == 0 || source.getNumSamples() <= 0)
            return;

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToWrite(source.getNumSamples(), start1, size1, start2, size2);

        copyFromSource(source, start1, 0, size1);
        copyFromSource(source, start2, size1, size2);
        fifo.finishedWrite(size1 + size2);
    }

    void read(juce::AudioBuffer<float>& destination, int numSamples)
    {
        if (destination.getNumChannels() == 0 || numSamples <= 0)
            return;

        const int samplesToRead = juce::jmin(numSamples, fifo.getNumReady());
        if (samplesToRead <= 0)
            return;

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead(samplesToRead, start1, size1, start2, size2);

        copyToDestination(destination, start1, 0, size1);
        copyToDestination(destination, start2, size1, size2);
        fifo.finishedRead(size1 + size2);
    }

    bool isEmpty() const noexcept
    {
        return fifo.getNumReady() == 0;
    }

    int getNumReadySamples() const noexcept
    {
        return fifo.getNumReady();
    }

private:
    void copyFromSource(const juce::AudioBuffer<float>& source, int destinationStart, int sourceStart, int count)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto sourceChannel = juce::jmin(channel, source.getNumChannels() - 1);
            buffer.copyFrom(channel, destinationStart, source, sourceChannel, sourceStart, count);
        }
    }

    void copyToDestination(juce::AudioBuffer<float>& destination, int sourceStart, int destinationStart, int count)
    {
        for (int channel = 0; channel < destination.getNumChannels(); ++channel)
        {
            const auto sourceChannel = juce::jmin(channel, buffer.getNumChannels() - 1);
            destination.copyFrom(channel, destinationStart, buffer, sourceChannel, sourceStart, count);
        }
    }

    juce::AudioBuffer<float> buffer;
    juce::AbstractFifo fifo;
};
