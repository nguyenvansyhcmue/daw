#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AssetFileFilter final : public juce::FileFilter
{
public:
    AssetFileFilter();
    void setSearchTerm(juce::String searchTerm);
    bool isFileSuitable(const juce::File& file) const override;
    bool isDirectorySuitable(const juce::File& directory) const override;

private:
    juce::String searchTerm;
};

class AssetBrowserPane final : public juce::Component, private juce::FileBrowserListener
{
public:
    AssetBrowserPane();
    ~AssetBrowserPane() override;

    std::function<void(const juce::File&)> onAudioFileActivated;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void selectionChanged() override { refreshPreviewDetails(); }
    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked(const juce::File& file) override;
    void browserRootChanged(const juce::File&) override {}
    void activateCategory(const juce::String& categoryName);
    void refreshCategorySelection();
    void refreshSearchResults();
    void refreshPreviewDetails();

    AssetFileFilter audioFilter;
    juce::File mediaRoot { juce::File::getSpecialLocation(juce::File::userMusicDirectory) };
    juce::FileBrowserComponent browser { juce::FileBrowserComponent::openMode
                                             | juce::FileBrowserComponent::canSelectFiles,
                                         mediaRoot,
                                         &audioFilter, nullptr };
    juce::Label title { {}, "LOOPS & MEDIA" };
    juce::TextButton loopsTab { "LOOPS" };
    juce::TextButton samplesTab { "SAMPLES" };
    juce::TextButton presetsTab { "PRESETS" };
    juce::TextButton goUpButton { "UP" };
    juce::TextEditor searchEditor;
    juce::Label previewDetails { {}, "No preview selected" };
    juce::Label fileDetails { {}, "file:" };
    juce::String activeCategory { "Loops" };
};
