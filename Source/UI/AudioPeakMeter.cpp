#include "AudioPeakMeter.h"

AudioPeakMeter::AudioPeakMeter()
{
    startTimerHz(60);
}

void AudioPeakMeter::updatePeak(float newLevel) noexcept
{
    newLevel = juce::jlimit(0.0f, 1.0f, newLevel);
    auto current = currentPeak.load(std::memory_order_relaxed);
    while (newLevel > current
           && !currentPeak.compare_exchange_weak(current, newLevel,
                                                  std::memory_order_relaxed,
                                                  std::memory_order_relaxed))
    {
    }
}

void AudioPeakMeter::timerCallback()
{
    const auto incomingPeak = currentPeak.exchange(0.0f, std::memory_order_relaxed);
    displayedPeak = juce::jmax(incomingPeak, displayedPeak * 0.92f);
    repaint();
}

void AudioPeakMeter::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(0xff101010));
    g.fillRoundedRectangle(bounds, 3.0f);

    const auto levelHeight = bounds.getHeight() * displayedPeak;
    const auto level = bounds.withTop(bounds.getBottom() - levelHeight);
    juce::ColourGradient gradient(juce::Colour(0xff23d18b), 0.0f, bounds.getBottom(),
                                  juce::Colour(0xffffd166), 0.0f, bounds.getY(), false);
    gradient.addColour(0.65, juce::Colour(0xffffd166));
    gradient.addColour(0.88, juce::Colour(0xffef5350));
    g.setGradientFill(gradient);
    g.fillRoundedRectangle(level, 2.0f);

    if (displayedPeak >= 0.999f)
    {
        g.setColour(juce::Colours::red);
        g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 3.0f);
    }
}
