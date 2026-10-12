#include "AudioDiagnosticTracer.h"

AudioDiagnosticTracer::AudioDiagnosticTracer() : juce::Thread("StudioForge audio diagnostics") {}
AudioDiagnosticTracer::~AudioDiagnosticTracer() { stop(); }
void AudioDiagnosticTracer::start()
{
    if (enabled.load(std::memory_order_acquire))
        return;
    read.store(0, std::memory_order_relaxed);
    write.store(0, std::memory_order_relaxed);
    droppedEvents.store(0, std::memory_order_relaxed);
    markRequested.store(false, std::memory_order_relaxed);
    nextSignalEventMs.store(0, std::memory_order_relaxed);
    auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("StudioForge Diagnostics");
    dir.createDirectory(); output = dir.getNonexistentChildFile("audio-trace", ".csv", true);
    // Publish the freshly reset queue only after its destination is ready.
    // `start()` is called from the message thread; `record()` may run on the
    // audio thread immediately afterwards.
    enabled.store(true, std::memory_order_release);
    startThread();
}
void AudioDiagnosticTracer::stop() { enabled.store(false); signalThreadShouldExit(); stopThread(1500); }

bool AudioDiagnosticTracer::shouldRecordSignalEvent(uint64_t timestampMs) noexcept
{
    constexpr uint64_t minimumIntervalMs = 50; // 20 Hz is enough to expose fades and dropouts.
    auto next = nextSignalEventMs.load(std::memory_order_relaxed);
    while (timestampMs >= next)
        if (nextSignalEventMs.compare_exchange_weak(next, timestampMs + minimumIntervalMs,
                                                     std::memory_order_relaxed,
                                                     std::memory_order_relaxed))
            return true;
    return false;
}

void AudioDiagnosticTracer::record(const Event& value) noexcept
{
    if (! enabled.load(std::memory_order_relaxed)) return;
    auto w = write.load(std::memory_order_relaxed); const auto r = read.load(std::memory_order_acquire);
    if (w - r >= capacity) { droppedEvents.fetch_add(1, std::memory_order_relaxed); return; }
    auto event = value;
    if (markRequested.exchange(false, std::memory_order_acq_rel)) event.mode |= 0x80;
    events[w & mask] = event; write.store(w + 1, std::memory_order_release);
}
void AudioDiagnosticTracer::run()
{
    std::unique_ptr<juce::FileOutputStream> stream(output.createOutputStream());
    if (stream == nullptr) return;
    stream->writeText("ms,kind,callbackFrames,input,input2,output,track,trackPostFx,inputChannel,mode,panic,vibeState,vibeWetMix,vibeTrackerVoiced,vibeTrackerConfidence,vibeTrackerF0Hz,vibeDetectedMidi,vibeTargetMidi,vibeOutputMidi,vibeInputCents,vibeCorrectionCents,vibeRendererCollapsed,vibeRendererExcessive,vibeRendererInvalid,vibeCallbackOverloaded,vibeHardDryRescue,vibeProtectionCount,vibeOverloadCount,deviceHash,sampleRate,deviceBuffer,inputChannels,outputChannels,inputMask,outputMask,coreAudioInputGain,coreAudioInputMuted\n", false, false, "UTF-8");
    auto nextCoreAudioPoll = uint64_t { 0 };
    while (! threadShouldExit() || read.load() != write.load())
    {
        auto r = read.load(std::memory_order_relaxed), w = write.load(std::memory_order_acquire);
        while (r != w)
        {
            const auto& e = events[r & mask];
            stream->writeText(juce::String(e.ms) + "," + juce::String(e.kind) + ","
                              + juce::String(e.callbackFrames) + "," + juce::String(e.inputPeak) + ","
                              + juce::String(e.secondaryInputPeak) + "," + juce::String(e.outputPeak) + ","
                              + juce::String(e.trackId) + ","
                              + juce::String(e.trackPostFxPeak) + "," + juce::String(e.input) + ","
                              + juce::String(e.mode) + "," + juce::String(e.panic) + ","
                              + juce::String(static_cast<int>(e.vibeState)) + ","
                              + juce::String(e.vibeWetMix) + ","
                              + juce::String(static_cast<int>(e.vibeTrackerVoiced)) + ","
                              + juce::String(e.vibeTrackerConfidence) + ","
                              + juce::String(e.vibeTrackerF0Hz) + ","
                              + juce::String(e.vibeTrackerDetectedMidiNote) + ","
                              + juce::String(e.vibeTrackerTargetMidiNote) + ","
                              + juce::String(e.vibeTrackerOutputMidiNote) + ","
                              + juce::String(e.vibeTrackerInputCents) + ","
                              + juce::String(e.vibeCorrectionCents) + ","
                              + juce::String(static_cast<int>(e.vibeRendererCollapsed)) + ","
                              + juce::String(static_cast<int>(e.vibeRendererExcessive)) + ","
                              + juce::String(static_cast<int>(e.vibeRendererInvalid)) + ","
                              + juce::String(static_cast<int>(e.vibeCallbackOverloaded)) + ","
                              + juce::String(static_cast<int>(e.vibeHardDryRescue)) + ","
                              + juce::String(e.vibeProtectionCount) + ","
                              + juce::String(e.vibeOverloadCount) + ","
                              + juce::String(e.deviceHash) + "," + juce::String(e.sampleRate) + ","
                              + juce::String(e.deviceBufferSize) + "," + juce::String(e.inputChannels) + ","
                              + juce::String(e.outputChannels) + "," + juce::String(e.inputMask) + ","
                              + juce::String(e.outputMask) + ",,\n", false, false, "UTF-8");
            ++r;
        }
        read.store(r, std::memory_order_release); stream->flush(); wait(100);
        const auto now = static_cast<uint64_t>(juce::Time::getMillisecondCounterHiRes());
        if (now >= nextCoreAudioPoll)
        {
            const auto input = coreAudioInput.read();
            stream->writeText(juce::String(now) + ",3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,-1,-1,-1,0,0,0,0,0,0,0,0,0,"
                              + juce::String(input.gain) + "," + juce::String(input.muted) + "\n",
                              false, false, "UTF-8");
            nextCoreAudioPoll = now + 250;
        }
    }
}
