#include "DeviceSettingsPanel.h"

#include "../AudioEngine/AudioEngine.h"
#include "../AudioEngine/LivePerformanceAssessment.h"
#include "AudioDeviceErrorLocalizer.h"
#include "Theme/StudioForgeLookAndFeel.h"

namespace
{
constexpr int outerPadding = 16;
constexpr int routingRowHeight = 34;
constexpr int routingContentPadding = 8;
constexpr int tabBarHeight = 32;
constexpr double liveCheckDurationMilliseconds = 10000.0;

juce::String keyName(int root, bool minor)
{
    static constexpr std::array<const char*, 12> names {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    return juce::String(names[static_cast<size_t>(juce::jlimit(0, 11, root))])
        + (minor ? " minor" : " major");
}

juce::String liveLatencyText(const AudioEngine::RealtimeDiagnostics& diagnostics)
{
    if (diagnostics.sampleRate <= 0.0)
        return "LIVE: no active audio device";

    const auto samples = diagnostics.inputLatencySamples + diagnostics.outputLatencySamples
        + diagnostics.pluginLatencySamples;
    const auto milliseconds = 1000.0 * static_cast<double>(samples) / diagnostics.sampleRate;
    const auto callbackPercent = juce::roundToInt(diagnostics.callbackLoad * 100.0f);
    const auto prefix = diagnostics.callbackLoad >= 0.80f ? "LIVE: engine overload"
        : milliseconds <= 12.0 ? "LIVE READY" : milliseconds <= 20.0 ? "LIVE OK"
        : milliseconds <= 35.0 ? "LIVE: noticeable delay" : "LIVE: delay too high";
    return juce::String(prefix) + " — round-trip " + juce::String(milliseconds, 1) + " ms"
        + " (device " + juce::String(diagnostics.inputLatencySamples + diagnostics.outputLatencySamples)
        + ", plugins " + juce::String(diagnostics.pluginLatencySamples) + " samples; engine "
        + juce::String(callbackPercent) + "%)"
        + (diagnostics.protectedPluginCount > 0
               ? " — LIVE bypassed " + juce::String(diagnostics.protectedPluginCount) + " high-latency FX"
               : "");
}
}

DeviceSettingsPanel::DeviceSettingsPanel(AudioEngine& engine, TrackDataModel& model)
    : audioEngine(engine), trackModel(model),
      selector(audioEngine.getAudioDeviceManager(), 0, 64, 2, 64, true, true, true, false)
{
    summary.setJustificationType(juce::Justification::centredLeft);
    summary.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.75f));
    summary.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::plain)));
    addAndMakeVisible(summary);
    liveReadiness.setJustificationType(juce::Justification::centredLeft);
    liveReadiness.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    addAndMakeVisible(liveReadiness);
    liveOptimizeButton.setTooltip("Sets the safest low-latency buffer for LIVE while keeping your input and output devices.");
    liveOptimizeButton.onClick = [this] { applyLiveOptimizedBuffer(); };
    addAndMakeVisible(liveOptimizeButton);
    liveCheckResult.setText("Run a 10-second LIVE check before performing.", juce::dontSendNotification);
    liveCheckResult.setJustificationType(juce::Justification::centredLeft);
    liveCheckResult.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::plain)));
    liveCheckResult.setColour(juce::Label::textColourId, StudioForgeTheme::secondaryText);
    addAndMakeVisible(liveCheckResult);
    liveCheckButton.setTooltip("Measures real callback headroom and newly detected audio overloads for 10 seconds.");
    liveCheckButton.onClick = [this] { startLiveCheck(); };
    addAndMakeVisible(liveCheckButton);
    autoKeyStatus.setJustificationType(juce::Justification::centredLeft);
    autoKeyStatus.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::plain)));
    autoKeyStatus.setColour(juce::Label::textColourId, StudioForgeTheme::secondaryText);
    addAndMakeVisible(autoKeyStatus);

    settingsTabs.setTabBarDepth(tabBarHeight);
    settingsTabs.addTab("Device", juce::Colour(0xff2a2d31), &devicePage, false);
    settingsTabs.addTab("Track Inputs", juce::Colour(0xff2a2d31), &routingPage, false);
    addAndMakeVisible(settingsTabs);
    devicePage.addAndMakeVisible(selector);

    routingHeading.setText("AUDIO TRACK INPUTS", juce::dontSendNotification);
    routingHeading.setColour(juce::Label::textColourId, StudioForgeTheme::accentCyan);
    routingHeading.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    routingHeading.setJustificationType(juce::Justification::centredLeft);
    routingHint.setColour(juce::Label::textColourId, StudioForgeTheme::secondaryText);
    routingHint.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::plain)));
    routingHint.setJustificationType(juce::Justification::centredLeft);
    routingPage.addAndMakeVisible(routingHeading);
    routingPage.addAndMakeVisible(routingHint);

    routingViewport.setViewedComponent(&routingContent, false);
    routingViewport.setScrollBarsShown(true, false);
    routingPage.addAndMakeVisible(routingViewport);

    for (size_t index = 0; index < trackInputLabels.size(); ++index)
    {
        auto& label = trackInputLabels[index];
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, StudioForgeTheme::primaryText);
        label.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::plain)));
        routingContent.addChildComponent(label);

        auto& selectorForTrack = trackInputSelectors[index];
        selectorForTrack.setTooltip("Audio interface input for this track");
        selectorForTrack.onChange = [this, index] { applyTrackInputConfiguration(index); };
        routingContent.addChildComponent(selectorForTrack);

        auto& format = trackInputFormats[index];
        format.addItem("Mono", 1);
        format.addItem("Stereo", 2);
        format.setTooltip("Mono uses one input; Stereo uses this input and the next input");
        format.onChange = [this, index] { applyTrackInputConfiguration(index); };
        routingContent.addChildComponent(format);

        auto& monitor = trackMonitorModes[index];
        monitor.addItem("Off", 1);
        monitor.addItem("Auto", 2);
        monitor.addItem("In", 3);
        monitor.setTooltip("Off: no input monitoring. Auto: monitor when armed. In: always monitor.");
        monitor.onChange = [this, index] { applyTrackInputConfiguration(index); };
        routingContent.addChildComponent(monitor);
    }

    startTimerHz(4);
    timerCallback();
}

DeviceSettingsPanel::~DeviceSettingsPanel()
{
    audioEngine.saveAudioDeviceState();
}

void DeviceSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced(outerPadding);
    summary.setBounds(area.removeFromTop(24));
    auto liveRow = area.removeFromTop(22);
    liveOptimizeButton.setBounds(liveRow.removeFromRight(118).reduced(0, 1));
    liveReadiness.setBounds(liveRow);
    auto checkRow = area.removeFromTop(22);
    liveCheckButton.setBounds(checkRow.removeFromRight(132).reduced(0, 1));
    liveCheckResult.setBounds(checkRow);
    autoKeyStatus.setBounds(area.removeFromTop(20));
    area.removeFromTop(6);
    settingsTabs.setBounds(area);

    selector.setBounds(devicePage.getLocalBounds().reduced(10));

    auto routingArea = routingPage.getLocalBounds().reduced(12);
    routingHeading.setBounds(routingArea.removeFromTop(22));
    routingHint.setBounds(routingArea.removeFromTop(20));
    routingArea.removeFromTop(6);
    routingViewport.setBounds(routingArea);

    const auto contentWidth = juce::jmax(1, routingViewport.getWidth() - routingViewport.getScrollBarThickness());
    auto audioTrackCount = 0;
    for (size_t index = 0; index < trackModel.getTrackCount(); ++index)
        if (trackModel.getTrack(index).type == TrackType::audio)
            ++audioTrackCount;
    auto contentArea = juce::Rectangle<int>(0, 0, contentWidth,
                                            routingContentPadding * 2 + juce::jmax(1, audioTrackCount) * routingRowHeight);
    routingContent.setBounds(contentArea);
    auto rows = contentArea.reduced(routingContentPadding, routingContentPadding);
    for (size_t index = 0; index < trackInputLabels.size(); ++index)
    {
        const auto active = index < trackModel.getTrackCount()
            && trackModel.getTrack(index).type == TrackType::audio;
        if (! active)
        {
            trackInputLabels[index].setBounds({});
            trackInputSelectors[index].setBounds({});
            trackInputFormats[index].setBounds({});
            trackMonitorModes[index].setBounds({});
            continue;
        }
        auto row = rows.removeFromTop(routingRowHeight);
        trackInputLabels[index].setBounds(row.removeFromLeft(row.getWidth() / 4).reduced(2, 2));
        trackInputSelectors[index].setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2, 3));
        trackInputFormats[index].setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2, 3));
        trackMonitorModes[index].setBounds(row.reduced(2, 3));
    }
}

void DeviceSettingsPanel::timerCallback()
{
    localizeAudioDeviceErrors();

    const auto diagnostics = audioEngine.getRealtimeDiagnostics();
    const auto currentSummary = "Active: " + juce::String(juce::roundToInt(diagnostics.sampleRate)) + " Hz, "
                              + juce::String(diagnostics.bufferSize) + " samples, "
                              + "input latency " + juce::String(diagnostics.inputLatencySamples) + " samples, "
                              + "output latency " + juce::String(diagnostics.outputLatencySamples) + " samples, "
                              + "plugin latency " + juce::String(diagnostics.pluginLatencySamples) + " samples";
    if (currentSummary != lastSummary)
    {
        lastSummary = currentSummary;
        summary.setText(lastSummary, juce::dontSendNotification);
    }
    const auto liveStatus = liveLatencyText(diagnostics);
    liveReadiness.setText(liveStatus, juce::dontSendNotification);
    const auto totalSamples = diagnostics.inputLatencySamples + diagnostics.outputLatencySamples
        + diagnostics.pluginLatencySamples;
    const auto totalMilliseconds = diagnostics.sampleRate > 0.0
        ? 1000.0 * static_cast<double>(totalSamples) / diagnostics.sampleRate : 999.0;
    liveReadiness.setColour(juce::Label::textColourId, diagnostics.callbackLoad >= 0.80f ? juce::Colour(0xffef7777)
        : totalMilliseconds <= 20.0 ? StudioForgeTheme::accentCyan
        : totalMilliseconds <= 35.0 ? juce::Colour(0xffe6be65) : juce::Colour(0xffef7777));
    liveOptimizeButton.setEnabled(audioEngine.getAudioDeviceManager().getCurrentAudioDevice() != nullptr);
    updateLiveCheck();
    const auto detectedKey = audioEngine.getLiveKeyResult();
    if (! audioEngine.isLivePerformanceEnabled())
        autoKeyStatus.setText("AUTO KEY: turn on LIVE to listen to Stereo Out and set VibeAutotune automatically.", juce::dontSendNotification);
    else if (detectedKey.revision == 0)
        autoKeyStatus.setText("AUTO KEY: listening to Stereo Out; waiting for a stable key…", juce::dontSendNotification);
    else
    {
        autoKeyStatus.setText("AUTO KEY: " + keyName(detectedKey.root, detectedKey.minor)
                                  + " detected — applied directly to VibeAutotune.",
                              juce::dontSendNotification);
        autoKeyStatus.setColour(juce::Label::textColourId, StudioForgeTheme::accentCyan);
    }

    const auto nextInputRoutingKey = makeInputRoutingKey();
    if (nextInputRoutingKey != inputRoutingKey)
    {
        inputRoutingKey = nextInputRoutingKey;
        refreshInputRouting();
        resized();
    }
}

void DeviceSettingsPanel::applyLiveOptimizedBuffer()
{
    if (const auto result = audioEngine.optimiseDeviceForLivePerformance(); result.failed())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               "LIVE Optimize",
                                               "Could not apply the low-latency buffer: " + result.getErrorMessage());
    }
}

void DeviceSettingsPanel::startLiveCheck()
{
    if (! audioEngine.isLivePerformanceEnabled())
    {
        liveCheckResult.setText("Turn on LIVE first, then run the check while your full live FX chain is active.",
                                juce::dontSendNotification);
        liveCheckResult.setColour(juce::Label::textColourId, juce::Colour(0xffe6be65));
        return;
    }

    const auto diagnostics = audioEngine.getRealtimeDiagnostics();
    if (diagnostics.sampleRate <= 0.0)
    {
        liveCheckResult.setText("No active audio device.", juce::dontSendNotification);
        liveCheckResult.setColour(juce::Label::textColourId, juce::Colour(0xffef7777));
        return;
    }

    liveCheck = { true, juce::Time::getMillisecondCounterHiRes(), diagnostics.overloadCount,
                  diagnostics.callbackLoad };
    liveCheckResult.setText("Checking LIVE stability… keep your normal tracks and FX active.", juce::dontSendNotification);
    liveCheckResult.setColour(juce::Label::textColourId, StudioForgeTheme::accentCyan);
    liveCheckButton.setButtonText("CHECKING…");
    liveCheckButton.setEnabled(false);
}

void DeviceSettingsPanel::updateLiveCheck()
{
    if (! liveCheck.active)
        return;

    const auto diagnostics = audioEngine.getRealtimeDiagnostics();
    liveCheck.peakCallbackLoad = juce::jmax(liveCheck.peakCallbackLoad, diagnostics.callbackLoad);
    if (juce::Time::getMillisecondCounterHiRes() - liveCheck.startedAtMilliseconds < liveCheckDurationMilliseconds)
        return;

    liveCheck.active = false;
    liveCheckButton.setButtonText("RUN LIVE CHECK");
    liveCheckButton.setEnabled(true);
    const LivePerformanceMeasurement measurement {
        diagnostics.sampleRate,
        diagnostics.inputLatencySamples,
        diagnostics.outputLatencySamples,
        diagnostics.pluginLatencySamples,
        liveCheck.peakCallbackLoad,
        diagnostics.overloadCount - liveCheck.overloadBaseline
    };
    const auto assessment = assessLivePerformance(measurement);
    const auto loadPercent = juce::roundToInt(measurement.peakCallbackLoad * 100.0f);
    switch (assessment.rating)
    {
        case LivePerformanceRating::ready:
            liveCheckResult.setText("PASS — " + juce::String(assessment.roundTripMilliseconds, 1)
                                        + " ms, peak engine " + juce::String(loadPercent) + "%.",
                                    juce::dontSendNotification);
            liveCheckResult.setColour(juce::Label::textColourId, StudioForgeTheme::accentCyan);
            break;
        case LivePerformanceRating::caution:
            liveCheckResult.setText("CAUTION — " + juce::String(assessment.roundTripMilliseconds, 1)
                                        + " ms, peak engine " + juce::String(loadPercent)
                                        + "%. Use 128 samples or reduce LIVE FX.",
                                    juce::dontSendNotification);
            liveCheckResult.setColour(juce::Label::textColourId, juce::Colour(0xffe6be65));
            break;
        case LivePerformanceRating::notReady:
            liveCheckResult.setText("FAIL — overload detected. Raise buffer or remove heavy LIVE FX before performing.",
                                    juce::dontSendNotification);
            liveCheckResult.setColour(juce::Label::textColourId, juce::Colour(0xffef7777));
            break;
    }
}

juce::String DeviceSettingsPanel::makeInputRoutingKey() const
{
    juce::String key;
    if (const auto* device = audioEngine.getAudioDeviceManager().getCurrentAudioDevice(); device != nullptr)
        key = device->getName() + ":" + device->getActiveInputChannels().toString(16);
    key << ":" << static_cast<int>(trackModel.getTrackCount());
    for (size_t index = 0; index < trackModel.getTrackCount(); ++index)
    {
        const auto& track = trackModel.getTrack(index);
        key << ":" << track.name << ":" << track.inputChannel.load(std::memory_order_relaxed)
            << ":" << track.inputChannelCount.load(std::memory_order_relaxed)
            << ":" << static_cast<int>(track.autoInputMonitoring.load(std::memory_order_relaxed))
            << ":" << static_cast<int>(track.inputMonitoring.load(std::memory_order_relaxed))
            << ":" << static_cast<int>(track.type);
    }
    return key;
}

void DeviceSettingsPanel::refreshInputRouting()
{
    juce::Array<int> activeInputIndices;
    if (const auto* device = audioEngine.getAudioDeviceManager().getCurrentAudioDevice(); device != nullptr)
    {
        const auto activeInputs = device->getActiveInputChannels();
        for (int channel = 0; channel < activeInputs.getHighestBit() + 1; ++channel)
            if (activeInputs[channel])
                activeInputIndices.add(channel);
    }

    routingHint.setText(activeInputIndices.isEmpty()
                            ? "Enable interface inputs above, then assign them to each audio track."
                            : "Audio tracks only. Arm multiple tracks to record their selected inputs together.",
                        juce::dontSendNotification);
    for (size_t index = 0; index < trackInputSelectors.size(); ++index)
    {
        const auto activeTrack = index < trackModel.getTrackCount()
            && trackModel.getTrack(index).type == TrackType::audio;
        trackInputLabels[index].setVisible(activeTrack);
        trackInputSelectors[index].setVisible(activeTrack);
        trackInputFormats[index].setVisible(activeTrack);
        trackMonitorModes[index].setVisible(activeTrack);
        if (! activeTrack)
            continue;

        const auto& track = trackModel.getTrack(index);
        trackInputLabels[index].setText(track.name.isNotEmpty() ? track.name : "Audio " + juce::String(static_cast<int>(index + 1)),
                                        juce::dontSendNotification);
        auto& selectorForTrack = trackInputSelectors[index];
        selectorForTrack.clear(juce::dontSendNotification);
        for (int callbackChannel = 0; callbackChannel < activeInputIndices.size(); ++callbackChannel)
            selectorForTrack.addItem("Input " + juce::String(activeInputIndices[callbackChannel] + 1), callbackChannel + 1);

        const auto selected = track.inputChannel.load(std::memory_order_relaxed) + 1;
        selectorForTrack.setSelectedId(activeInputIndices.isEmpty() ? 0 : selected, juce::dontSendNotification);
        selectorForTrack.setEnabled(! activeInputIndices.isEmpty());
        trackInputFormats[index].setSelectedId(track.inputChannelCount.load(std::memory_order_relaxed), juce::dontSendNotification);
        trackInputFormats[index].setEnabled(! activeInputIndices.isEmpty());
        trackInputFormats[index].setItemEnabled(2, selected < activeInputIndices.size());
        const auto monitorMode = track.inputMonitoring.load(std::memory_order_relaxed) ? 3
            : track.autoInputMonitoring.load(std::memory_order_relaxed) ? 2 : 1;
        trackMonitorModes[index].setSelectedId(monitorMode, juce::dontSendNotification);
        trackMonitorModes[index].setEnabled(! activeInputIndices.isEmpty());
    }
}

void DeviceSettingsPanel::applyTrackInputConfiguration(size_t trackIndex)
{
    if (trackIndex >= trackModel.getTrackCount() || trackModel.getTrack(trackIndex).type != TrackType::audio)
        return;
    const auto selectedInput = trackInputSelectors[trackIndex].getSelectedId();
    if (selectedInput <= 0)
        return;
    auto inputChannelCount = trackInputFormats[trackIndex].getSelectedId();
    if (const auto* device = audioEngine.getAudioDeviceManager().getCurrentAudioDevice(); device != nullptr
        && inputChannelCount == 2
        && selectedInput >= device->getActiveInputChannels().countNumberOfSetBits())
    {
        inputChannelCount = 1;
        trackInputFormats[trackIndex].setSelectedId(1, juce::dontSendNotification);
    }
    const auto monitorMode = trackMonitorModes[trackIndex].getSelectedId();
    trackModel.setTrackInputConfiguration(trackIndex, monitorMode == 2, monitorMode == 3,
                                          selectedInput - 1, inputChannelCount);
    inputRoutingKey.clear();
}

void DeviceSettingsPanel::localizeAudioDeviceErrors()
{
    AudioDeviceErrorLocalizer::localizePendingOpenDeviceFailure();
}
