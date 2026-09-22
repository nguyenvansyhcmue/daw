#include "PluginBrowserPanel.h"
#include "StudioForgeDialog.h"

PluginBrowserPanel::PluginBrowserPanel(PluginHostService& host, SelectionCallback onSelection)
    : pluginHost(host), onPluginSelected(std::move(onSelection))
{
    searchBox.setTextToShowWhenEmpty("Search plug-ins", juce::Colours::white.withAlpha(0.38f));
    searchBox.onTextChange = [this] { updateResults(); };
    searchBox.setSelectAllWhenFocused(true);
    addAndMakeVisible(searchBox);

    results.setRowHeight(42);
    results.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1d2025));
    addAndMakeVisible(results);

    scanButton.onClick = [this] { choosePluginToScan(); };
    addAndMakeVisible(scanButton);

    insertButton.setEnabled(false);
    insertButton.onClick = [this]
    {
        const auto selected = results.getSelectedRow();
        if (selected >= 0 && selected < plugins.size() && onPluginSelected != nullptr)
            onPluginSelected(plugins.getReference(selected));
        closeWindow();
    };
    cancelButton.onClick = [this] { closeWindow(); };
    addAndMakeVisible(insertButton);
    addAndMakeVisible(cancelButton);
    updateResults();
}

void PluginBrowserPanel::resized()
{
    auto area = getLocalBounds().reduced(14);
    searchBox.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);
    auto buttons = area.removeFromBottom(30);
    cancelButton.setBounds(buttons.removeFromRight(86));
    scanButton.setBounds(buttons.removeFromLeft(118).reduced(2, 0));
    insertButton.setBounds(buttons.removeFromRight(86).reduced(4, 0));
    results.setBounds(area);
}

int PluginBrowserPanel::getNumRows()
{
    return plugins.size();
}

void PluginBrowserPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= plugins.size())
        return;
    const auto& plugin = plugins.getReference(row);
    if (selected)
        g.fillAll(juce::Colour(0xff3178c6));
    g.setColour(juce::Colours::white.withAlpha(0.92f));
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText(plugin.name, 8, 3, width - 16, 18, juce::Justification::centredLeft, true);
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(plugin.manufacturerName + "  •  " + plugin.category,
               8, 21, width - 16, height - 22, juce::Justification::centredLeft, true);
}

void PluginBrowserPanel::selectedRowsChanged(int)
{
    insertButton.setEnabled(results.getSelectedRow() >= 0);
}

void PluginBrowserPanel::updateResults()
{
    plugins = pluginHost.findKnownPlugins(searchBox.getText());
    results.deselectAllRows();
    results.updateContent();
    results.repaint();
    insertButton.setEnabled(false);
}

void PluginBrowserPanel::choosePluginToScan()
{
    fileChooser = std::make_unique<juce::FileChooser>("Select VST3 Plug-in", juce::File {}, "*.vst3");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectDirectories,
                             [this] (const juce::FileChooser& chooser)
    {
        const auto selectedFile = chooser.getResult();
        fileChooser.reset();
        if (! selectedFile.exists()) return;
        scanButton.setEnabled(false);
        pluginHost.scanVst3Async(selectedFile, [this] (juce::Result result)
        {
            scanButton.setEnabled(true);
            if (result.failed())
                StudioForgeDialog::showWarning(StudioForgeDialog::fromUtf8("Qu\u00E9t plugin ch\u01B0a ho\u00E0n t\u1EA5t"),
                                               result.getErrorMessage());
            updateResults();
        });
    });
}

void PluginBrowserPanel::closeWindow()
{
    if (auto* dialog = dynamic_cast<juce::DialogWindow*>(getTopLevelComponent()))
        dialog->exitModalState(0);
}
