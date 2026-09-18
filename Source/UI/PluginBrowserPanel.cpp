#include "PluginBrowserPanel.h"

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

void PluginBrowserPanel::closeWindow()
{
    if (auto* dialog = dynamic_cast<juce::DialogWindow*>(getTopLevelComponent()))
        dialog->exitModalState(0);
}
