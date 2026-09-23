#include "AssetBrowserPane.h"

#include "Theme/StudioForgeLookAndFeel.h"

AssetFileFilter::AssetFileFilter() : juce::FileFilter("Audio files") {}

void AssetFileFilter::setSearchTerm(juce::String newSearchTerm)
{
    searchTerm = newSearchTerm.trim().toLowerCase();
}

bool AssetFileFilter::isFileSuitable(const juce::File& file) const
{
    const auto extension = file.getFileExtension().toLowerCase();
    const auto isAudio = extension == ".wav" || extension == ".aif" || extension == ".aiff" || extension == ".mp3";
    return isAudio && (searchTerm.isEmpty() || file.getFileName().toLowerCase().contains(searchTerm));
}

bool AssetFileFilter::isDirectorySuitable(const juce::File&) const
{
    return true;
}

AssetBrowserPane::AssetBrowserPane()
{
    title.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    title.setJustificationType(juce::Justification::centredLeft);
    title.setColour(juce::Label::textColourId, StudioForgeTheme::primaryText.withAlpha(0.86f));
    addAndMakeVisible(title);
    for (auto* tab : { &loopsTab, &samplesTab, &presetsTab })
    {
        tab->setClickingTogglesState(true);
        addAndMakeVisible(*tab);
    }
    loopsTab.onClick = [this] { activateCategory("Loops"); };
    samplesTab.onClick = [this] { activateCategory("Samples"); };
    presetsTab.onClick = [this] { activateCategory("Presets"); };
    goUpButton.onClick = [this] { browser.goUp(); };
    addAndMakeVisible(goUpButton);

    searchEditor.setTextToShowWhenEmpty("Search loops...", StudioForgeTheme::secondaryText);
    searchEditor.setColour(juce::TextEditor::backgroundColourId, StudioForgeTheme::recessedSurface);
    searchEditor.setColour(juce::TextEditor::outlineColourId, StudioForgeTheme::separator);
    searchEditor.setColour(juce::TextEditor::textColourId, StudioForgeTheme::primaryText);
    searchEditor.onTextChange = [this] { refreshSearchResults(); };
    addAndMakeVisible(searchEditor);

    for (auto* label : { &previewDetails, &fileDetails })
    {
        label->setColour(juce::Label::textColourId, StudioForgeTheme::secondaryText);
        label->setFont(juce::Font(juce::FontOptions(10.0f)));
        addAndMakeVisible(*label);
    }
    browser.addListener(this);
    addAndMakeVisible(browser);
    refreshCategorySelection();
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
    g.setColour(StudioForgeTheme::separator.withAlpha(0.55f));
    g.drawHorizontalLine(getHeight() - 67, 8.0f, static_cast<float>(getWidth() - 8));
}

void AssetBrowserPane::resized()
{
    auto area = getLocalBounds().reduced(9, 8);
    title.setBounds(area.removeFromTop(23));
    auto tabs = area.removeFromTop(28);
    loopsTab.setBounds(tabs.removeFromLeft(70).reduced(0, 2));
    samplesTab.setBounds(tabs.removeFromLeft(76).reduced(2));
    presetsTab.setBounds(tabs.removeFromLeft(76).reduced(2));
    goUpButton.setBounds(tabs.removeFromRight(34).reduced(0, 2));
    searchEditor.setBounds(area.removeFromTop(31).reduced(0, 3));
    auto previewArea = area.removeFromBottom(58);
    previewDetails.setBounds(previewArea.removeFromTop(22));
    fileDetails.setBounds(previewArea.removeFromTop(20));
    browser.setBounds(area.reduced(0, 2));
}

void AssetBrowserPane::fileDoubleClicked(const juce::File& file)
{
    if (file.existsAsFile() && onAudioFileActivated != nullptr)
        onAudioFileActivated(file);
}

void AssetBrowserPane::activateCategory(const juce::String& categoryName)
{
    activeCategory = categoryName;
    const auto categoryDirectory = mediaRoot.getChildFile(categoryName);
    browser.setRoot(categoryDirectory.isDirectory() ? categoryDirectory : mediaRoot);
    refreshCategorySelection();
}

void AssetBrowserPane::refreshCategorySelection()
{
    loopsTab.setToggleState(activeCategory == "Loops", juce::dontSendNotification);
    samplesTab.setToggleState(activeCategory == "Samples", juce::dontSendNotification);
    presetsTab.setToggleState(activeCategory == "Presets", juce::dontSendNotification);
}

void AssetBrowserPane::refreshSearchResults()
{
    audioFilter.setSearchTerm(searchEditor.getText());
    browser.refresh();
}

void AssetBrowserPane::refreshPreviewDetails()
{
    const auto selectedFile = browser.getHighlightedFile();
    previewDetails.setText(selectedFile.existsAsFile() ? selectedFile.getFileName() : "No preview selected", juce::dontSendNotification);
    fileDetails.setText(selectedFile.existsAsFile() ? "file: " + selectedFile.getFullPathName() : "file:", juce::dontSendNotification);
}
