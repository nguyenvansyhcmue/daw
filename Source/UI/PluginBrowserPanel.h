#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Plugins/PluginHostService.h"

class PluginBrowserPanel final : public juce::Component,
                                 private juce::ListBoxModel
{
public:
    using SelectionCallback = std::function<void(const juce::PluginDescription&)>;

    PluginBrowserPanel(PluginHostService& host, SelectionCallback onSelection);

    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                          bool rowIsSelected) override;
    void selectedRowsChanged(int) override;
    void updateResults();
    void closeWindow();
    void choosePluginToScan();
    void scanDefaultVst3Locations();
    void scanAudioUnits();

    PluginHostService& pluginHost;
    SelectionCallback onPluginSelected;
    juce::TextEditor searchBox;
    juce::ListBox results { "Plugins", this };
    juce::TextButton insertButton { "Insert" };
    juce::TextButton scanButton { "Add VST3 File..." };
    juce::TextButton scanDefaultVst3Button { "Scan VST3 Folders" };
    juce::TextButton scanAudioUnitsButton { "Scan Audio Units" };
    juce::TextButton cancelButton { "Cancel" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Array<juce::PluginDescription> plugins;
};
