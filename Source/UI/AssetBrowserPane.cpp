#include "AssetBrowserPane.h"

#include "Theme/StudioForgeLookAndFeel.h"

AssetBrowserPane::AssetBrowserPane()
{
    title.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    title.setJustificationType(juce::Justification::centredLeft);
    title.setColour(juce::Label::textColourId, StudioForgeTheme::primaryText.withAlpha(0.86f));
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
    g.fillAll(StudioForgeTheme::panelBackground.darker(0.05f));
    g.setColour(StudioForgeTheme::separator.withAlpha(0.72f));
    g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
    g.drawHorizontalLine(31, 8.0f, static_cast<float>(getWidth() - 8));
}

void AssetBrowserPane::resized()
{
    auto area = getLocalBounds().reduced(9, 8);
    title.setBounds(area.removeFromTop(23));
    area.removeFromTop(3);
    browser.setBounds(area);
}

void AssetBrowserPane::fileDoubleClicked(const juce::File& file)
{
    if (file.existsAsFile() && onAudioFileActivated != nullptr)
        onAudioFileActivated(file);
}
