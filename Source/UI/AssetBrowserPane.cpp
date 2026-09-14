#include "AssetBrowserPane.h"

AssetBrowserPane::AssetBrowserPane()
{
    title.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    title.setJustificationType(juce::Justification::centredLeft);
    title.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.78f));
    addAndMakeVisible(title);
    browser.addListener(this);
    addAndMakeVisible(browser);
}

AssetBrowserPane::~AssetBrowserPane()
{
    browser.removeListener(this);
}

void AssetBrowserPane::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff252526));
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
    g.drawHorizontalLine(30, 8.0f, static_cast<float>(getWidth() - 8));
}

void AssetBrowserPane::resized()
{
    auto area = getLocalBounds().reduced(8);
    title.setBounds(area.removeFromTop(22));
    browser.setBounds(area);
}

void AssetBrowserPane::fileDoubleClicked(const juce::File& file)
{
    if (file.existsAsFile() && onAudioFileActivated != nullptr)
        onAudioFileActivated(file);
}
