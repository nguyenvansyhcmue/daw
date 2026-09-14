#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AssetBrowserPane final : public juce::Component, private juce::FileBrowserListener
{
public:
    AssetBrowserPane();
    ~AssetBrowserPane() override;

    std::function<void(const juce::File&)> onAudioFileActivated;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void selectionChanged() override {}
    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked(const juce::File& file) override;
    void browserRootChanged(const juce::File&) override {}

    juce::WildcardFileFilter audioFilter { "*.wav;*.aif;*.aiff;*.mp3", "*", "Audio files" };
    juce::FileBrowserComponent browser { juce::FileBrowserComponent::openMode
                                             | juce::FileBrowserComponent::canSelectFiles,
                                         juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                         &audioFilter, nullptr };
    juce::Label title { {}, "LOOPS & MEDIA" };
};
