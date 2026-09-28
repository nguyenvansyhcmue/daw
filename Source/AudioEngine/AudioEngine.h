#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <array>
#include <memory>
#include <vector>
#include <chrono>

#include "AudioGraph.h"
#include "AudioEffectProcessor.h"
#include "LiveKeyDetector.h"
#include "TrackMixing.h"
#include "TransportUtils.h"
#include "../Recording/RecordingSession.h"
#include "../Models/TrackDataModel.h"

class AudioEngine final : public juce::AudioIODeviceCallback,
                          public juce::MidiInputCallback
{
public:
    struct RealtimeDiagnostics
    {
        double sampleRate = 0.0;
        int bufferSize = 0;
        int inputLatencySamples = 0;
        int outputLatencySamples = 0;
        int pluginLatencySamples = 0;
        int protectedPluginCount = 0;
        float callbackLoad = 0.0f;
        uint64_t overloadCount = 0;
        uint64_t midiInputOverflowCount = 0;
        uint64_t midiRecordingOverflowCount = 0;
    };

    explicit AudioEngine(TrackDataModel* model = nullptr);
    ~AudioEngine() override;

    void initialise();
    void shutdown();
    void saveAudioDeviceState() const;

    void setMasterGain(float gain) noexcept;
    float getMasterGain() const noexcept;

    void setTempo(double newTempo) noexcept;
    double getTempo() const noexcept;

    void setPlaybackState(bool shouldPlay) noexcept;
    bool isPlaying() const noexcept;
    void setMetronomeEnabled(bool enabled) noexcept;
    bool isMetronomeEnabled() const noexcept;
    void setCountInMetronomeActive(bool active) noexcept;
    void setLivePerformanceEnabled(bool enabled) noexcept;
    bool isLivePerformanceEnabled() const noexcept;
    bool isLiveLowLatencyProtectionEnabled() const noexcept;
    juce::Result optimiseDeviceForLivePerformance();

    juce::AudioDeviceManager& getAudioDeviceManager() noexcept;
    float getTrackPeak(size_t trackIndex) const noexcept;
    float getTrackInputPeak(size_t trackIndex) const noexcept;
    float getBusPeak(size_t busIndex) const noexcept;
    float getMasterPeak() const noexcept;
    float getMasterLeftPeak() const noexcept;
    float getMasterRightPeak() const noexcept;
    RealtimeDiagnostics getRealtimeDiagnostics() const noexcept;
    int getEstimatedLivePluginLatencySamples() const noexcept;
    int getLiveProtectedPluginCount() const noexcept;
    LiveKeyDetector::Result getLiveKeyResult() const noexcept;
    int applyDetectedKeyToPitchCorrection();
    void setFxProcessor(size_t trackIndex, size_t slot,
                        std::shared_ptr<AudioEffectProcessor> processor);
    void setFxBypassed(size_t trackIndex, size_t slot, bool bypassed) noexcept;
    const TrackDataModel::FxRackSnapshot* getMasterFxRackSnapshot() const noexcept;
    void setMasterFxProcessor(size_t slot, std::shared_ptr<AudioEffectProcessor> processor);
    void setMasterFxBypassed(size_t slot, bool bypassed);
    void prepareActiveEffects();
    struct AudioRecordingTarget
    {
        size_t trackIndex = 0;
        juce::File destination;
    };
    juce::Result startRecording(size_t trackIndex, const juce::File& destination,
                                double timelineStartSample = -1.0);
    juce::Result startRecordings(const std::vector<AudioRecordingTarget>& targets,
                                 double timelineStartSample = -1.0);
    // Runs the same device/input checks as recording without opening files or
    // altering transport state. Use it before a count-in begins.
    juce::Result validateRecordingTargets(const std::vector<AudioRecordingTarget>& targets) const;
    juce::Result stopRecording();
    bool isRecording() const noexcept;
    juce::Result startMidiRecording(size_t trackIndex);
    MidiClipId stopMidiRecording();
    bool isMidiRecording() const noexcept;
    void setRecordingCaptureRange(double startSample, double endSample) noexcept;
    void clearRecordingCaptureRange() noexcept;
    size_t getScheduledMidiEventCount() const noexcept { return scheduledMidiEvents.size(); }
    struct OfflineRenderOptions
    {
        double sampleRate = 44100.0;
        int blockSize = 512;
        bool useCycle = false;
        int bitDepth = 24;
    };

    juce::Result renderOfflineAudio(const juce::File& destination, const OfflineRenderOptions& options);
    juce::Result renderOfflineWav(const juce::File& destination, double sampleRate,
                                  int blockSize = 512, bool useCycle = false);

    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;

private:
    class CallbackTimingScope
    {
    public:
        CallbackTimingScope(AudioEngine& owner, int samples) noexcept;
        ~CallbackTimingScope();

    private:
        AudioEngine& engine;
        int numSamples;
        std::chrono::steady_clock::time_point started;
    };

    void renderClipSegment(const std::vector<AudioClipState>& clips,
                           const TrackDataModel::RenderStructureSnapshot& structure,
                           double segmentStartSample, int destinationOffset,
                           int numSamples) noexcept;
    void renderInstrument(const TrackDataModel::RenderStructureSnapshot& structure, int numSamples) noexcept;
    void publishMasterFxRack(std::unique_ptr<const TrackDataModel::FxRackSnapshot> snapshot);
    void prepareRack(const TrackDataModel::FxRackSnapshot* rack, double sampleRate, int blockSize);
    int rackLatencySamples(const TrackDataModel::FxRackSnapshot* rack) const noexcept;
    int protectedPluginCount(const TrackDataModel::FxRackSnapshot* rack) const noexcept;
    bool shouldProcessForLive(const AudioEffectProcessor& processor) const noexcept;
    void recordCallbackTiming(int numSamples, std::chrono::steady_clock::time_point started) noexcept;
    void appendIncomingMidiEvents(const TrackDataModel::RenderStructureSnapshot& structure,
                                  double blockStartSample) noexcept;
    void renderMetronome(double blockStartSample, int numSamples) noexcept;
    void clearRealtimeBuffers(int numSamples) noexcept;
    void captureRecordingInput(const float* const* inputChannelData, int numInputChannels,
                               int numSamples) noexcept;
    bool hasMonitoredAudioTracks(const TrackDataModel::RenderStructureSnapshot* structure) const noexcept;
    void renderInputMonitoring(const float* const* inputChannelData, int numInputChannels,
                               const TrackDataModel::RenderStructureSnapshot* structure,
                               int numSamples) noexcept;
    void updateTrackInputPeaks(const float* const* inputChannelData, int numInputChannels,
                               const TrackDataModel::RenderStructureSnapshot* structure,
                               int numSamples) noexcept;
    void prepareTrackMidiBuffers(const TrackDataModel::RenderStructureSnapshot* structure) noexcept;
    void processTrackMidiEffects(const TrackDataModel::RenderStructureSnapshot* structure) noexcept;
    void processTrackAudio(const TrackDataModel::RenderStructureSnapshot* structure,
                           double automationSample, bool anyTrackSoloed, int numSamples) noexcept;
    void processBusReturns(const TrackDataModel::RenderStructureSnapshot* structure, int numSamples) noexcept;
    void processMasterOutput(float* const* outputChannelData, int numOutputChannels,
                             int numSamples, bool isPlaying, double blockStartSample) noexcept;
    void addTrackToBus(const TrackDataModel::RenderTrack& track,
                       size_t trackIndex,
                       const juce::AudioBuffer<float>& source,
                       const TrackDataModel::RenderStructureSnapshot* structure,
                       size_t route, float level, int numSamples) noexcept;

    juce::AudioDeviceManager deviceManager;
    AudioGraph graph;
    std::atomic<float> masterGain { 1.0f };
    std::atomic<bool> metronomeEnabled { false };
    std::atomic<bool> countInMetronomeActive { false };
    std::atomic<bool> livePerformanceEnabled { false };
    std::atomic<bool> liveLowLatencyProtectionEnabled { false };
    LiveKeyDetector liveKeyDetector;
    juce::AudioBuffer<float> processingBuffer;
    std::array<std::atomic<float>, TrackDataModel::maxTracks> trackPeaks {};
    std::array<std::atomic<float>, TrackDataModel::maxTracks> trackInputPeaks {};
    std::array<std::atomic<float>, TrackDataModel::maxBuses> busPeaks {};
    std::atomic<float> masterPeak { 0.0f };
    std::atomic<float> masterLeftPeak { 0.0f };
    std::atomic<float> masterRightPeak { 0.0f };
    std::atomic<double> activeSampleRate { 0.0 };
    std::atomic<int> activeBufferSize { 0 };
    std::atomic<int> activeInputLatencySamples { 0 };
    std::atomic<int> activeOutputLatencySamples { 0 };
    std::atomic<float> callbackLoad { 0.0f };
    std::atomic<uint64_t> callbackOverloadCount { 0 };
    std::unique_ptr<const TrackDataModel::FxRackSnapshot> masterFxRack;
    std::atomic<const TrackDataModel::FxRackSnapshot*> publishedMasterFxRack { nullptr };
    std::vector<std::unique_ptr<const TrackDataModel::FxRackSnapshot>> retiredMasterFxRacks;
    std::array<juce::AudioBuffer<float>, TrackDataModel::maxTracks> trackBuffers;
    std::array<juce::AudioBuffer<float>, TrackDataModel::maxBuses> busBuffers;
    std::array<std::array<float, TrackDataModel::maxSendsPerTrack>, TrackDataModel::maxTracks> smoothedSendGains {};
    std::array<juce::MidiBuffer, TrackDataModel::maxTracks> trackMidiBuffers;
    MidiEventBuffer scheduledMidiEvents;
    struct IncomingMidiEvent
    {
        int pitch = 60;
        float velocity = 0.0f;
        int channel = 1;
        bool noteOn = false;
    };
    static constexpr int incomingMidiCapacity = 256;
    std::array<IncomingMidiEvent, incomingMidiCapacity> incomingMidiEvents {};
    juce::AbstractFifo incomingMidiFifo { incomingMidiCapacity };
    std::atomic<uint64_t> droppedMidiInputEvents { 0 };
    struct RecordedMidiEvent
    {
        double samplePosition = 0.0;
        int pitch = 60;
        float velocity = 0.0f;
        int channel = 1;
        bool noteOn = false;
    };
    static constexpr int recordedMidiCapacity = 2048;
    std::array<RecordedMidiEvent, recordedMidiCapacity> recordedMidiEvents {};
    juce::AbstractFifo recordedMidiFifo { recordedMidiCapacity };
    std::atomic<bool> midiRecordingActive { false };
    std::atomic<unsigned int> midiRecordingCallbackUsers { 0 };
    std::atomic<uint64_t> droppedMidiRecordingEvents { 0 };
    TrackId midiRecordingTrack;
    double midiRecordingStartSample = 0.0;
    struct InstrumentVoice { bool active = false; int pitch = 60; float velocity = 0.0f; double phase = 0.0; };
    std::array<InstrumentVoice, TrackDataModel::maxTracks> instrumentVoices {};
    std::unique_ptr<RecordingSession> recordingSession;
    std::atomic<bool> recordingCaptureRangeActive { false };
    std::atomic<double> recordingCaptureStartSample { 0.0 };
    std::atomic<double> recordingCaptureEndSample { 0.0 };
    TrackDataModel* dataModel = nullptr;
};
