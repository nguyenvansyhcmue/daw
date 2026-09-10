#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "AudioClipState.h"
#include "../AudioEngine/AudioEffectProcessor.h"

#include <atomic>
#include <array>
#include <vector>

class TrackDataModel final : public juce::ChangeBroadcaster
{
public:
    static constexpr size_t maxFxSlots = 4;
    enum class EditTool { Select, Split };

    struct FxRackSnapshot
    {
        std::array<std::shared_ptr<AudioEffectProcessor>, maxFxSlots> processors;
        std::array<std::atomic<bool>, maxFxSlots> bypass {};

        FxRackSnapshot() = default;
        FxRackSnapshot(const FxRackSnapshot& other)
            : processors(other.processors)
        {
            for (size_t i = 0; i < maxFxSlots; ++i)
                bypass[i].store(other.bypass[i].load(std::memory_order_relaxed));
        }

        FxRackSnapshot& operator=(const FxRackSnapshot& other)
        {
            processors = other.processors;
            for (size_t i = 0; i < maxFxSlots; ++i)
                bypass[i].store(other.bypass[i].load(std::memory_order_relaxed));
            return *this;
        }
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

    struct ClipBlock
    {
        juce::String id;
        int trackIndex = 0;
        double startSample = 0.0;
        double lengthSamples = 0.0;
        bool isMidi = false;
        bool isAudio = true;
        juce::String name;
    };

    struct TrackState
    {
        std::atomic<float> volume { 1.0f };
        std::atomic<float> pan { 0.0f };
        std::atomic<bool> muted { false };
        std::atomic<bool> solo { false };
        std::atomic<bool> armed { false };
        std::vector<AudioClipState> clips;

        TrackState() = default;
        TrackState(const TrackState& other) noexcept
            : volume(other.volume.load()),
              pan(other.pan.load()),
              muted(other.muted.load()),
              solo(other.solo.load()),
              armed(other.armed.load()),
              clips(other.clips)
        {
        }

        TrackState& operator=(const TrackState& other) noexcept
        {
            volume.store(other.volume.load());
            pan.store(other.pan.load());
            muted.store(other.muted.load());
            solo.store(other.solo.load());
            armed.store(other.armed.load());
            clips = other.clips;
            return *this;
        }

        TrackState(TrackState&& other) noexcept : TrackState(other) {}
        TrackState& operator=(TrackState&& other) noexcept { return operator=(other); }
    };

    TrackDataModel();

    void setSampleRate(double sampleRate) noexcept;
    void setBpm(double newBpm) noexcept;
    double getBpm() const noexcept;
    void setTimeSignatureNumerator(int numerator) noexcept;
    int getTimeSignatureNumerator() const noexcept;
    void addTempoEvent(double samplePosition, double bpm);
    void clearTempoMap();

    double getSampleRate() const noexcept { return sampleRate; }
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
    TrackState& getTrack(size_t index);
    const TrackState& getTrack(size_t index) const;

    void addClipBlock(const ClipBlock& clip);
    std::vector<ClipBlock>& getClipBlocks() noexcept;
    const std::vector<ClipBlock>& getClipBlocks() const noexcept;
    void addClipToTrack(int trackID, const juce::File& file, double startSample,
                        std::shared_ptr<juce::AudioBuffer<float>> loadedBuffer);
    std::shared_ptr<const std::vector<AudioClipState>> getAudioClipSnapshot() const noexcept;
    std::shared_ptr<const FxRackSnapshot> getFxRackSnapshot(size_t trackID) const noexcept;
    void setFxProcessor(size_t trackID, size_t slot,
                        std::shared_ptr<AudioEffectProcessor> processor);
    void setFxBypassed(size_t trackID, size_t slot, bool bypassed) noexcept;
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
    void splitAudioClip(size_t trackID, size_t clipIndex, double splitSample);
    void trimAudioClip(size_t trackID, size_t clipIndex, double startSample, double durationSamples);
    void deleteAudioClip(size_t trackID, size_t clipIndex);

private:
    double sampleRate = 48000.0;
    std::atomic<double> bpm { 120.0 };
    std::atomic<int> timeSignatureNumerator { 4 };
    std::vector<TempoEvent> tempoMap;
    std::vector<TrackState> trackStates;
    std::vector<ClipBlock> clipBlocks;
    std::shared_ptr<const std::vector<AudioClipState>> audioClipSnapshot;
    std::array<std::shared_ptr<const FxRackSnapshot>, 8> fxRacks;
    std::atomic<EditTool> activeTool { EditTool::Select };
    std::atomic<float> horizontalZoom { 1.0f };
    std::atomic<int> trackHeight { 60 };
    std::atomic<bool> cycleActive { false };
    std::atomic<double> cycleStartSample { 0.0 };
    std::atomic<double> cycleEndSample { 0.0 };
    std::atomic<bool> playing { false };
    std::atomic<double> playheadPosition { 0.0 };
};
