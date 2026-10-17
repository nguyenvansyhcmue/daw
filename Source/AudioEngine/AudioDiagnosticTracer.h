#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include "CoreAudioInputProbe.h"

class AudioDiagnosticTracer final : private juce::Thread
{
public:
    // `kind` is 0 for an audio callback, 1 for device-start, 2 for
    // device-stop, 3 for a Core Audio control poll, and 4 for a device-manager
    // change. The same fixed-size queue is used for lifecycle and signal
    // evidence, so they share one monotonic timeline.
    // This remains POD: `record()` is safe to call without allocating from an
    // audio/device callback.
    struct Event
    {
        uint64_t ms;
        uint64_t trackId;
        uint64_t inputMask;
        uint64_t outputMask;
        double sampleRate;
        float inputPeak, secondaryInputPeak, outputPeak, trackPostFxPeak, vibeWetMix, vibeTrackerConfidence, vibeTrackerF0Hz,
              vibeTrackerInputCents, vibeCorrectionCents, pluginPreFxPeak, pluginPostFxPeak;
        uint64_t vibeProtectionCount, vibeOverloadCount;
        uint32_t callbackFrames;
        uint32_t deviceBufferSize;
        uint32_t deviceHash;
        int input, mode, vibeTrackerDetectedMidiNote, vibeTrackerTargetMidiNote, vibeTrackerOutputMidiNote,
            pluginSlot, pluginLatencySamples;
        uint8_t kind, inputChannels, outputChannels, panic, vibeState, vibeTrackerVoiced,
                vibeRendererCollapsed, vibeRendererExcessive, vibeRendererInvalid,
                vibeCallbackOverloaded, vibeHardDryRescue, pluginPresent, pluginBypassed,
                pluginProcessed, pluginSkippedForLatency;
    };
    AudioDiagnosticTracer(); ~AudioDiagnosticTracer() override;
    void start(); void stop(); void markIssue() noexcept { markRequested.store(true, std::memory_order_release); }
    void setCoreAudioInputDevice(const juce::String& name) noexcept { coreAudioInput.selectDevice(name); }
    bool isEnabled() const noexcept { return enabled.load(std::memory_order_acquire); }
    // Signal samples are intentionally rate-limited before entering the FIFO.
    // Diagnostics must never become a source of callback overload.
    bool shouldRecordSignalEvent(uint64_t timestampMs) noexcept;
    uint64_t dropped() const noexcept { return droppedEvents.load(std::memory_order_relaxed); }
    void record(const Event&) noexcept;
private:
    void run() override;
    static constexpr uint32_t capacity = 16384, mask = capacity - 1;
    std::array<Event, capacity> events {};
    std::atomic<uint32_t> read { 0 }, write { 0 };
    std::atomic<bool> enabled { false }, markRequested { false };
    std::atomic<uint64_t> droppedEvents { 0 };
    std::atomic<uint64_t> nextSignalEventMs { 0 };
    juce::File output;
    CoreAudioInputProbe coreAudioInput;
};
