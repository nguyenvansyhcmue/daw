#include "PluginBrowserPanel.h"
#include "StudioForgeDialog.h"

namespace
{
juce::StringArray recentPluginIdentifiers;
bool recentPluginsLoaded = false;

juce::File recentPluginsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("StudioForge").getChildFile("RecentPlugins.txt");
}

void loadRecentPlugins()
{
    if (recentPluginsLoaded)
        return;
    recentPluginsLoaded = true;
    const auto file = recentPluginsFile();
    if (file.existsAsFile())
        recentPluginIdentifiers = juce::StringArray::fromLines(file.loadFileAsString());
}

bool isRecent(const juce::PluginDescription& description)
{
    return recentPluginIdentifiers.contains(description.createIdentifierString());
}

void rememberPlugin(const juce::PluginDescription& description)
{
    loadRecentPlugins();
    const auto identifier = description.createIdentifierString();
    recentPluginIdentifiers.removeString(identifier);
    recentPluginIdentifiers.insert(0, identifier);
    while (recentPluginIdentifiers.size() > 8)
        recentPluginIdentifiers.remove(recentPluginIdentifiers.size() - 1);
    const auto file = recentPluginsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(recentPluginIdentifiers.joinIntoString("\n"));
}
}

PluginBrowserPanel::PluginBrowserPanel(PluginHostService& host, SelectionCallback onSelection)
    : pluginHost(host), onPluginSelected(std::move(onSelection))
{
    loadRecentPlugins();
    setSize(preferredWidth, preferredHeight);
    searchBox.setTextToShowWhenEmpty("Search by name, manufacturer or format", juce::Colour(0xff8d9aa2));
    searchBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff11181d));
    searchBox.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff53646d));
    searchBox.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff42b6d8));
    searchBox.setColour(juce::TextEditor::textColourId, juce::Colour(0xffe6edf0));
    searchBox.setFont(juce::Font(juce::FontOptions(14.0f)));
    searchBox.onTextChange = [this] { updateResults(); };
    searchBox.setSelectAllWhenFocused(true);
    addAndMakeVisible(searchBox);

    results.setRowHeight(32);
    results.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1d2025));
    results.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff46555d));
    results.setOutlineThickness(1);
    addAndMakeVisible(results);

    // Scanning is automatic and runs in the background. The insert browser
    // stays focused on the user task: search, choose, insert.
    scanButton.setVisible(false);
    scanDefaultVst3Button.setVisible(false);
    scanAudioUnitsButton.setVisible(false);
    insertButton.setVisible(false);
    cancelButton.setVisible(false);

    insertButton.setEnabled(false);
    insertButton.onClick = [this]
    {
        // A plug-in instantiation can create an audio processor and a native
        // editor window. Never do that while this modal window is handling its
        // mouse click: AppKit may otherwise send the tail of the click to a
        // window that JUCE is in the process of destroying.
        const auto selected = results.getSelectedRow();
        const auto description = selected >= 0 && selected < plugins.size()
            ? plugins.getReference(selected) : juce::PluginDescription {};
        const auto selectionCallback = onPluginSelected;
        closeWindow();

        if (description.name.isNotEmpty() && selectionCallback != nullptr)
            juce::MessageManager::callAsync([selectionCallback, description]
            {
                selectionCallback(description);
            });
    };
    cancelButton.onClick = [this] { closeWindow(); };
    addAndMakeVisible(insertButton);
    addAndMakeVisible(cancelButton);
    updateResults();

    // The startup scan may still be running when a user opens an FX slot.
    // Refresh this view when discovery has caught up; no manual folder browse
    // is required for normally installed VST3 bundles.
    const juce::Component::SafePointer<PluginBrowserPanel> safeThis(this);
    pluginHost.scanDefaultVst3LocationsAsync([safeThis] (juce::Result)
    {
        if (safeThis != nullptr)
            safeThis->updateResults();
    });
}

void PluginBrowserPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient surface(juce::Colour(0xff20282d), 0.0f, 0.0f,
                                 juce::Colour(0xff11171b), 0.0f, bounds.getBottom(), false);
    g.setGradientFill(surface);
    g.fillAll();

    g.setColour(juce::Colour(0xff0b1013));
    g.fillRect(bounds.removeFromTop(42.0f));
    g.setColour(juce::Colour(0xffd7e2e6));
    g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    g.drawText("Choose plug-in", 12, 9, 220, 20, juce::Justification::left);
    g.setColour(juce::Colour(0xff42b6d8).withAlpha(0.65f));
    g.fillRect(12.0f, 40.0f, 90.0f, 2.0f);
}

void PluginBrowserPanel::resized()
{
    auto area = getLocalBounds().reduced(10, 8);
    area.removeFromTop(30);
    searchBox.setBounds(area.removeFromTop(26));
    area.removeFromTop(6);
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
    const auto accent = selected ? juce::Colour(0xff42b6d8) : juce::Colour(0xff536b75);
    g.setColour(accent.withAlpha(selected ? 0.95f : 0.55f));
    g.fillRect(0.0f, 5.0f, 3.0f, static_cast<float>(height - 10));
    g.setColour(selected ? juce::Colour(0xffeaf7fb) : juce::Colour(0xffe0e8eb));
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText(plugin.name, 14, 4, width - 28, 18, juce::Justification::centredLeft, true);
    g.setColour(selected ? juce::Colour(0xffc5e9f3) : juce::Colour(0xff94a4ac));
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(plugin.manufacturerName + "  •  " + plugin.pluginFormatName,
               14, 22, width - 28, height - 24, juce::Justification::centredLeft, true);
    g.setColour(selected ? juce::Colour(0xff123c49) : juce::Colour(0xff2b383e));
    g.fillRoundedRectangle(static_cast<float>(width - 76), 13.0f, 58.0f, 18.0f, 4.0f);
    g.setColour(selected ? juce::Colour(0xffbcecf7) : juce::Colour(0xffaab8bd));
    g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    g.drawText(plugin.pluginFormatName, width - 72, 14, 50, 16, juce::Justification::centred, false);
    g.setColour(juce::Colour(0xff415159).withAlpha(0.55f));
    g.drawHorizontalLine(height - 1, 12.0f, static_cast<float>(width - 12));
}

void PluginBrowserPanel::selectedRowsChanged(int)
{
    const auto selected = results.getSelectedRow();
    if (selected < 0 || selected >= plugins.size() || onPluginSelected == nullptr)
        return;

    const auto description = plugins.getReference(selected);
    rememberPlugin(description);
    const auto callback = onPluginSelected;
    closeWindow();
    juce::MessageManager::callAsync([callback, description]
    {
        callback(description);
    });
}

void PluginBrowserPanel::updateResults()
{
    loadRecentPlugins();
    plugins = pluginHost.findKnownPlugins(searchBox.getText());
    // Keep recent plug-ins at the top while preserving stable catalog order
    // for the remaining entries. Search results still remain flat and exact.
    std::stable_sort(plugins.begin(), plugins.end(), [] (const auto& left, const auto& right)
    {
        return isRecent(left) && ! isRecent(right);
    });
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
    // The browser may be embedded in the DAW peer, so its top-level component
    // is the main window rather than the CallOutBox itself.
    if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
    {
        callout->dismiss();
        return;
    }
    if (auto* dialog = dynamic_cast<juce::DialogWindow*>(getTopLevelComponent()))
        dialog->exitModalState(0);
}
