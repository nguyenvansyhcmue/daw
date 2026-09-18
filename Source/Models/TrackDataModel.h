#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "AudioClipState.h"
#include "../Core/Automation/AutomationRenderSnapshot.h"
#include "../Media/AudioMediaPool.h"
#include "../AudioEngine/AudioEffectProcessor.h"
#include "../Project/ProjectState.h"
#include "../Midi/MidiCore.h"

#include <atomic>
#include <array>
#include <memory>
#include <vector>

class TrackDataModel final : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    // Fixed upper bound keeps every render buffer preallocated before the audio
    // device starts while allowing the full Logic-style visible track list.
    static constexpr size_t maxTracks = 14;
    static constexpr size_t maxBuses = 8;
    static constexpr size_t maxFxSlots = 4;
    static constexpr size_t maxSendsPerTrack = 8;
    enum class EditTool { Pointer, Scissors, Eraser };

    struct FxRackSnapshot
    {
        std::array<std::shared_ptr<AudioEffectProcessor>, maxFxSlots> processors;
        std::array<bool, maxFxSlots> bypass {};
    };

    struct TempoEvent
    {
        double samplePosition = 0.0;
        double bpm = 120.0;
    };

    struct BarBeatInfo
    {
        int bars = 0;
        int beats = 0;
        int pulses = 0;
        double subPulse = 0.0;
    };

    struct TrackState
    {
        struct SendRoute { BusId targetBus; float level = 1.0f; bool preFader = false; };
        TrackId id;
        TrackType type = TrackType::audio;
        std::atomic<float> volume { 1.0f };
        std::atomic<float> pan { 0.0f };
        std::atomic<bool> muted { false };
        std::atomic<bool> solo { false };
        std::atomic<bool> soloSafe { false };
        std::atomic<bool> armed { false };
        std::atomic<bool> inputMonitoring { false };
        std::atomic<int> inputChannel { 0 };
        juce::String name;
        std::vector<AudioClipState> clips;
        BusId outputBus;
        std::array<SendRoute, maxSendsPerTrack> sends {};
        std::array<AutomationLane, maxSendsPerTrack> sendAutomation {
            AutomationLane { AutomationParameter::sendLevel, 1.0f }, AutomationLane { AutomationParameter::sendLevel, 1.0f },
            AutomationLane { AutomationParameter::sendLevel, 1.0f }, AutomationLane { AutomationParameter::sendLevel, 1.0f },
            AutomationLane { AutomationParameter::sendLevel, 1.0f }, AutomationLane { AutomationParameter::sendLevel, 1.0f },
            AutomationLane { AutomationParameter::sendLevel, 1.0f }, AutomationLane { AutomationParameter::sendLevel, 1.0f }
        };
        uint8_t activeSendCount = 0;
        AutomationLane volumeAutomation { AutomationParameter::trackVolume, 1.0f };
        AutomationLane panAutomation { AutomationParameter::trackPan, 0.0f };

        TrackState() = default;
        TrackState(const TrackState& other) noexcept
            : id(other.id), type(other.type),
              volume(other.volume.load()),
              pan(other.pan.load()),
              muted(other.muted.load()),
              solo(other.solo.load()),
              soloSafe(other.soloSafe.load()),
              armed(other.armed.load()), inputMonitoring(other.inputMonitoring.load()),
              inputChannel(other.inputChannel.load()), name(other.name),
              clips(other.clips), outputBus(other.outputBus), sends(other.sends), activeSendCount(other.activeSendCount),
              volumeAutomation(other.volumeAutomation), panAutomation(other.panAutomation), sendAutomation(other.sendAutomation)
        {
        }

        TrackState& operator=(const TrackState& other) noexcept
        {
            id = other.id; type = other.type;
            volume.store(other.volume.load());
            pan.store(other.pan.load());
            muted.store(other.muted.load());
            solo.store(other.solo.load());
            soloSafe.store(other.soloSafe.load());
            armed.store(other.armed.load());
            inputMonitoring.store(other.inputMonitoring.load());
            inputChannel.store(other.inputChannel.load());
            name = other.name;
            clips = other.clips;
            outputBus = other.outputBus; sends = other.sends; activeSendCount = other.activeSendCount;
            volumeAutomation = other.volumeAutomation; panAutomation = other.panAutomation; sendAutomation = other.sendAutomation;
            return *this;
        }

        TrackState(TrackState&& other) noexcept : TrackState(other) {}
        TrackState& operator=(TrackState&& other) noexcept { return operator=(other); }
    };

    struct RenderTrack
    {
        struct SendRouteSnapshot
        {
            BusId targetBus;
            float level = 1.0f;
            bool preFader = false;
            AutomationRenderLane automation;
        };
        TrackId id;
        TrackType type = TrackType::audio;
        float volume = 1.0f;
        float pan = 0.0f;
        bool muted = false;
        bool solo = false;
        bool soloSafe = false;
        bool armed = false;
        bool inputMonitoring = false;
        int inputChannel = 0;
        const FxRackSnapshot* fxRack = nullptr;
        BusId outputBus;
        std::array<SendRouteSnapshot, maxSendsPerTrack> sends {};
        uint8_t activeSendCount = 0;
        AutomationRenderLane volumeAutomation;
        AutomationRenderLane panAutomation;
    };
    struct BusState { BusId id; float gain = 1.0f; bool muted = false; };
    struct RenderBus { BusId id; float gain = 1.0f; bool muted = false; const FxRackSnapshot* fxRack = nullptr; };

    struct RenderStructureSnapshot
    {
        std::array<RenderTrack, maxTracks> tracks {};
        std::array<RenderBus, maxBuses> buses {};
        size_t trackCount = 0;
        size_t busCount = 0;

        int getTrackIndex(TrackId id) const noexcept;
        int getBusIndex(BusId id) const noexcept;
    };

    class RealtimeSnapshotRead final
    {
    public:
        explicit RealtimeSnapshotRead(const TrackDataModel& owner) noexcept;
        ~RealtimeSnapshotRead();

        RealtimeSnapshotRead(const RealtimeSnapshotRead&) = delete;
        RealtimeSnapshotRead& operator=(const RealtimeSnapshotRead&) = delete;

        const std::vector<AudioClipState>* getAudioClips() const noexcept;
        const RenderStructureSnapshot* getRenderStructure() const noexcept;
        const FxRackSnapshot* getFxRack(size_t trackIndex) const noexcept;
        const std::vector<MidiClipState>* getMidiClips() const noexcept;

    private:
        const TrackDataModel& model;
        const std::vector<AudioClipState>* audioClips = nullptr;
        const RenderStructureSnapshot* renderStructure = nullptr;
        const std::vector<MidiClipState>* midiClips = nullptr;
    };

    TrackDataModel();

    void setSampleRate(double sampleRate) noexcept;
    void setBpm(double newBpm) noexcept;
    double getBpm() const noexcept;
    void setTimeSignatureNumerator(int numerator) noexcept;
    int getTimeSignatureNumerator() const noexcept;
    void addTempoEvent(double samplePosition, double bpm);
    void clearTempoMap();

    double getSampleRate() const noexcept
    {
        const auto value = sampleRate.load(std::memory_order_relaxed);
        return value > 0.0 ? value : 1.0;
    }
    double getTempoAtSample(double samplePosition) const noexcept;
    BarBeatInfo getBarBeatInfo(double samplePosition) const noexcept;
    double barBeatToSamples(int bars, int beats, int pulses, double subPulse = 0.0) const noexcept;
    double sampleToXPosition(double samplePos, double pixelsPerSecond) const noexcept;
    double getSnappedSamplePosition(double rawSamplePos, double snapResolution) const noexcept;
    bool isPlaying() const noexcept;
    void setPlaying(bool shouldPlay) noexcept;
    double getPlayheadPosition() const noexcept;
    void setPlayheadPosition(double samplePosition) noexcept;

    void ensureTrackCount(size_t count);
    size_t getTrackCount() const noexcept;
    const TrackState& getTrack(size_t index) const;
    TrackId getTrackId(size_t index) const noexcept;
    int getTrackIndex(TrackId id) const noexcept;
    TrackId addTrack(TrackType type = TrackType::audio);
    bool removeTrack(TrackId id);
    bool reorderTrack(TrackId id, size_t destinationIndex);
    BusId addBus();
    bool removeBus(BusId id);
    const std::vector<BusState>& getBuses() const noexcept { return busStates; }
    const FxRackSnapshot* getBusFxRackSnapshot(BusId bus) const noexcept;
    void setBusFxProcessor(BusId bus, size_t slot, std::shared_ptr<AudioEffectProcessor> processor);
    void setBusFxBypassed(BusId bus, size_t slot, bool bypassed);
    bool setBusGain(BusId bus, float gain);
    bool setBusMuted(BusId bus, bool muted);
    bool setTrackOutputBus(TrackId track, BusId bus);
    bool setTrackSend(TrackId track, BusId bus, float amount);
    bool setTrackSendRoute(TrackId track, size_t route, BusId bus, float amount, bool preFader);
    bool clearTrackSendRoute(TrackId track, size_t route);
    bool clearTrackSend(TrackId track);
    void setTrackVolume(size_t index, float volume);
    void setTrackName(size_t index, const juce::String& name);
    void setTrackPan(size_t index, float pan);
    bool upsertTrackAutomationPoint(size_t trackIndex, AutomationParameter parameter, double samplePosition, float value);
    bool removeTrackAutomationPoint(size_t trackIndex, AutomationParameter parameter, double samplePosition);
    bool setTrackAutomationMode(size_t trackIndex, AutomationParameter parameter, AutomationMode mode);
    const AutomationLane* getTrackAutomationLane(size_t trackIndex, AutomationParameter parameter) const noexcept;
    const AutomationLane* getTrackSendAutomationLane(size_t trackIndex, size_t sendSlot) const noexcept;
    bool upsertTrackSendAutomationPoint(size_t trackIndex, size_t sendSlot, double samplePosition, float value);
    bool setTrackSendAutomationMode(size_t trackIndex, size_t sendSlot, AutomationMode mode);
    void setTrackMuted(size_t index, bool muted) noexcept;
    void setTrackSolo(size_t index, bool solo) noexcept;
    void setTrackSoloSafe(size_t index, bool safe) noexcept;
    bool isTrackSoloSafe(size_t index) const noexcept;
    void setTrackArmed(size_t index, bool armed) noexcept;
    bool isTrackArmed(size_t index) const noexcept;
    void setTrackInputMonitoring(size_t index, bool enabled, int inputChannel = 0) noexcept;
    bool isTrackInputMonitoring(size_t index) const noexcept;
    int getFirstArmedTrackIndex() const noexcept;
    MidiClipId addMidiClip(TrackId track, double startSample);
    MidiEventId addMidiNote(MidiClipId clip, int pitch, float velocity, double startSample, double durationSamples, int channel);
    MidiClipId addMidiRecording(TrackId track, double startSample, std::vector<MidiNoteEvent> notes);
    juce::Result importMidiTracks(const std::vector<ImportedMidiTrack>& tracks, double startSample);
    bool deleteMidiNote(MidiClipId clip, MidiEventId note);
    bool moveMidiNote(MidiClipId clip, MidiEventId note, int pitch, double startSample);
    bool resizeMidiNote(MidiClipId clip, MidiEventId note, double durationSamples);
    bool setMidiNoteVelocity(MidiClipId clip, MidiEventId note, float velocity);
    bool transposeMidiClip(MidiClipId clip, int semitones);
    bool quantizeMidiClip(MidiClipId clip, double gridSamples);
    const std::vector<MidiClipState>& getMidiClips() const noexcept;

    ClipId addClipToTrack(int trackIndex, const juce::File& file, double startSample,
                          std::shared_ptr<juce::AudioBuffer<float>> loadedBuffer);
    RealtimeSnapshotRead acquireRealtimeSnapshot() const noexcept;
    const FxRackSnapshot* getFxRackSnapshot(size_t trackIndex) const noexcept;
    void setFxProcessor(size_t trackIndex, size_t slot,
                        std::shared_ptr<AudioEffectProcessor> processor);
    void setFxBypassed(size_t trackIndex, size_t slot, bool bypassed) noexcept;
    EditTool getActiveTool() const noexcept;
    void setActiveTool(EditTool tool) noexcept;
    float getHorizontalZoom() const noexcept;
    void setHorizontalZoom(float zoom) noexcept;
    int getTrackHeight() const noexcept;
    void setTrackHeight(int height) noexcept;
    bool isCycleActive() const noexcept;
    double getCycleStartSample() const noexcept;
    double getCycleEndSample() const noexcept;
    void setCycle(double startSample, double endSample) noexcept;
    void clearCycle() noexcept;
    bool isPunchActive() const noexcept;
    double getPunchInSample() const noexcept;
    double getPunchOutSample() const noexcept;
    void setPunchRange(double inSample, double outSample) noexcept;
    void clearPunchRange() noexcept;
    void splitAudioClip(size_t trackIndex, size_t clipIndex, double splitSample);
    void trimAudioClip(size_t trackIndex, size_t clipIndex, double startSample, double durationSamples);
    void deleteAudioClip(size_t trackIndex, size_t clipIndex);
    bool moveAudioClip(ClipId clipId, TrackId destinationTrack, double startSample);
    ClipId duplicateAudioClip(ClipId clipId, TrackId destinationTrack, double startSample);
    bool trimAudioClip(ClipId clipId, double startSample, double durationSamples);
    bool deleteAudioClip(ClipId clipId);
    bool setClipGain(ClipId clipId, float gain);
    bool setClipFades(ClipId clipId, double fadeInSamples, double fadeOutSamples);
    bool setClipMediaResource(ClipId clipId, const juce::File& sourceFile,
                              std::shared_ptr<juce::AudioBuffer<float>> decodedBuffer);
    bool setClipMediaStatus(ClipId clipId, AudioMediaStatus status);
    std::shared_ptr<juce::AudioBuffer<float>> findDecodedAudioSource(const juce::File& sourceFile) const;
    size_t getAudioMediaSourceCount() const noexcept { return audioMediaPool.size(); }
    AudioMediaStatus getClipMediaStatus(ClipId clipId) const noexcept;
    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    bool undo();
    bool redo();
    void clearUndoHistory();
    uint64_t getProjectRevision() const noexcept { return projectRevision.load(std::memory_order_relaxed); }
    void restoreProjectRevision(uint64_t revision) noexcept { projectRevision.store(revision, std::memory_order_relaxed); }
    ProjectState createProjectState() const;
    juce::Result applyProjectState(const ProjectState& state);
    void reclaimRetiredRealtimeSnapshots();
    size_t getRetiredRealtimeSnapshotCount() const noexcept;

private:
    struct EditState
    {
        std::vector<TrackState> tracks;
        std::vector<BusState> buses;
        std::vector<MidiClipState> midiClips;
        std::vector<TempoEvent> tempo;
        std::array<FxRackSnapshot, maxTracks> fxRacks;
        std::array<TrackId, maxTracks> fxRackTrackIds {};
        double bpm = 120.0;
        int timeSignatureNumerator = 4;
        bool cycleActive = false;
        double cycleStartSample = 0.0;
        double cycleEndSample = 0.0;
        bool punchActive = false;
        double punchInSample = 0.0;
        double punchOutSample = 0.0;
    };

    EditState captureEditState() const;
    void commitEdit(EditState before);
    void restoreEditState(EditState state);
    void markProjectModified() noexcept { projectRevision.fetch_add(1, std::memory_order_relaxed); }
    void timerCallback() override;
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<uint64_t> projectRevision { 0 };
    std::atomic<double> bpm { 120.0 };
    std::atomic<int> timeSignatureNumerator { 4 };
    std::vector<TempoEvent> tempoMap;
    void publishAudioClipSnapshot();
    void publishMidiClipSnapshot();
    void publishFxRackSnapshot(size_t trackIndex, std::unique_ptr<const FxRackSnapshot> snapshot);
    void publishBusFxRackSnapshot(size_t busIndex, std::unique_ptr<const FxRackSnapshot> snapshot);
    void publishRenderStructureSnapshot();
    AutomationLane* getMutableTrackAutomationLane(size_t trackIndex, AutomationParameter parameter) noexcept;
    int getFxRackSlot(TrackId trackId) const noexcept;
    bool canChangeTrackStructure() const noexcept;

    std::vector<TrackState> trackStates;
    std::vector<MidiClipState> midiClips;
    std::vector<BusState> busStates;
    AudioMediaPool audioMediaPool;
    std::unique_ptr<const std::vector<AudioClipState>> audioClipSnapshot;
    std::atomic<const std::vector<AudioClipState>*> publishedAudioClipSnapshot { nullptr };
    std::vector<std::unique_ptr<const std::vector<AudioClipState>>> retiredAudioClipSnapshots;
    std::unique_ptr<const std::vector<MidiClipState>> midiClipSnapshot;
    std::atomic<const std::vector<MidiClipState>*> publishedMidiClipSnapshot { nullptr };
    std::vector<std::unique_ptr<const std::vector<MidiClipState>>> retiredMidiClipSnapshots;
    std::array<std::unique_ptr<const FxRackSnapshot>, maxTracks> fxRacks;
    std::array<std::atomic<const FxRackSnapshot*>, maxTracks> publishedFxRacks {};
    std::array<std::vector<std::unique_ptr<const FxRackSnapshot>>, maxTracks> retiredFxRackSnapshots;
    std::array<TrackId, maxTracks> fxRackTrackIds {};
    std::array<std::unique_ptr<const FxRackSnapshot>, maxBuses> busFxRacks;
    std::array<std::atomic<const FxRackSnapshot*>, maxBuses> publishedBusFxRacks {};
    std::array<std::vector<std::unique_ptr<const FxRackSnapshot>>, maxBuses> retiredBusFxRacks;
    std::unique_ptr<const RenderStructureSnapshot> renderStructureSnapshot;
    std::atomic<const RenderStructureSnapshot*> publishedRenderStructure { nullptr };
    std::vector<std::unique_ptr<const RenderStructureSnapshot>> retiredRenderStructures;
    mutable std::atomic<unsigned int> realtimeSnapshotReaders { 0 };
    uint64_t nextTrackId = 1;
    uint64_t nextClipId = 1;
    uint64_t nextBusId = 1;
    uint64_t nextMidiClipId = 1;
    uint64_t nextMidiEventId = 1;
    static constexpr size_t maxUndoEntries = 128;
    std::vector<EditState> undoHistory;
    std::vector<EditState> redoHistory;
    std::atomic<EditTool> activeTool { EditTool::Pointer };
    std::atomic<float> horizontalZoom { 1.0f };
    std::atomic<int> trackHeight { 42 };
    std::atomic<bool> cycleActive { false };
    std::atomic<double> cycleStartSample { 0.0 };
    std::atomic<double> cycleEndSample { 0.0 };
    std::atomic<bool> punchActive { false };
    std::atomic<double> punchInSample { 0.0 };
    std::atomic<double> punchOutSample { 0.0 };
    std::atomic<bool> playing { false };
    std::atomic<double> playheadPosition { 0.0 };
};
