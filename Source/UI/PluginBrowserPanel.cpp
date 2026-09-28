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
    if (pluginHost.supportsAudioUnits())
    {
        scanAudioUnitsButton.onClick = [this] { scanAudioUnits(); };
        addAndMakeVisible(scanAudioUnitsButton);
    }
    else
    {
        scanDefaultVst3Button.onClick = [this] { scanDefaultVst3Locations(); };
        addAndMakeVisible(scanDefaultVst3Button);
    }

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
    if (pluginHost.supportsAudioUnits())
        scanAudioUnitsButton.setBounds(buttons.removeFromLeft(148));
    else
        scanDefaultVst3Button.setBounds(buttons.removeFromLeft(148));
    scanButton.setBounds(buttons.removeFromLeft(118).reduced(2, 0));
    insertButton.setBounds(buttons.removeFromRight(86).reduced(4, 0));
    results.setBounds(area);
}

void PluginBrowserPanel::scanAudioUnits()
{
    scanAudioUnitsButton.setEnabled(false);
    const juce::Component::SafePointer<PluginBrowserPanel> safeThis(this);
    pluginHost.scanAudioUnitsAsync([safeThis] (juce::Result result)
    {
        if (safeThis == nullptr)
            return;
        safeThis->scanAudioUnitsButton.setEnabled(true);
        if (result.failed())
            StudioForgeDialog::showWarning("Audio Unit scan incomplete", result.getErrorMessage());
        safeThis->updateResults();
    });
}

void PluginBrowserPanel::scanDefaultVst3Locations()
{
    scanDefaultVst3Button.setEnabled(false);
    const juce::Component::SafePointer<PluginBrowserPanel> safeThis(this);
    pluginHost.scanDefaultVst3LocationsAsync([safeThis] (juce::Result result)
    {
        if (safeThis == nullptr)
            return;
        safeThis->scanDefaultVst3Button.setEnabled(true);
        if (result.failed())
            StudioForgeDialog::showWarning("VST3 scan incomplete", result.getErrorMessage());
        safeThis->updateResults();
    });
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
    fileChooser = std::make_unique<juce::FileChooser>("Select a .vst3 plug-in bundle", juce::File {}, "*.vst3");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectDirectories,
                             [this] (const juce::FileChooser& chooser)
    {
        const auto selectedFile = chooser.getResult();
        fileChooser.reset();
        if (! selectedFile.exists()) return;
        if (! selectedFile.hasFileExtension(".vst3"))
        {
            StudioForgeDialog::showWarning("VST3 file required",
                                           "Select a plug-in bundle whose name ends in .vst3. "
                                           "Logic Pro plug-ins are Audio Units and are already listed above.");
            return;
        }
        scanButton.setEnabled(false);
        const juce::Component::SafePointer<PluginBrowserPanel> safeThis(this);
        pluginHost.scanVst3Async(selectedFile, [safeThis] (juce::Result result)
        {
            if (safeThis == nullptr)
                return;
            safeThis->scanButton.setEnabled(true);
            if (result.failed())
                StudioForgeDialog::showWarning(StudioForgeDialog::fromUtf8("Qu\u00E9t plugin ch\u01B0a ho\u00E0n t\u1EA5t"),
                                               result.getErrorMessage());
            safeThis->updateResults();
        });
    });
}

void PluginBrowserPanel::closeWindow()
{
    if (auto* dialog = dynamic_cast<juce::DialogWindow*>(getTopLevelComponent()))
        dialog->exitModalState(0);
}
