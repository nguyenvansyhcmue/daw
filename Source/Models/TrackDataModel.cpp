#include "TrackDataModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

TrackDataModel::RealtimeSnapshotRead::RealtimeSnapshotRead(const TrackDataModel& owner) noexcept
    : model(owner)
{
    model.realtimeSnapshotReaders.fetch_add(1, std::memory_order_seq_cst);
    audioClips = model.publishedAudioClipSnapshot.load(std::memory_order_seq_cst);
    renderStructure = model.publishedRenderStructure.load(std::memory_order_seq_cst);
    midiClips = model.publishedMidiClipSnapshot.load(std::memory_order_seq_cst);
}

TrackDataModel::RealtimeSnapshotRead::~RealtimeSnapshotRead()
{
    model.realtimeSnapshotReaders.fetch_sub(1, std::memory_order_seq_cst);
}

const std::vector<AudioClipState>* TrackDataModel::RealtimeSnapshotRead::getAudioClips() const noexcept
{
    return audioClips;
}

const TrackDataModel::RenderStructureSnapshot* TrackDataModel::RealtimeSnapshotRead::getRenderStructure() const noexcept
{
    return renderStructure;
}

const TrackDataModel::FxRackSnapshot* TrackDataModel::RealtimeSnapshotRead::getFxRack(size_t trackIndex) const noexcept
{
    return renderStructure != nullptr && trackIndex < renderStructure->trackCount
        ? renderStructure->tracks[trackIndex].fxRack : nullptr;
}
const std::vector<MidiClipState>* TrackDataModel::RealtimeSnapshotRead::getMidiClips() const noexcept { return midiClips; }

int TrackDataModel::RenderStructureSnapshot::getTrackIndex(TrackId id) const noexcept
{
    for (size_t index = 0; index < trackCount; ++index)
        if (tracks[index].id == id) return static_cast<int>(index);
    return -1;
}
int TrackDataModel::RenderStructureSnapshot::getBusIndex(BusId id) const noexcept
{
    for (size_t index = 0; index < busCount; ++index) if (buses[index].id == id) return static_cast<int>(index);
    return -1;
}

TrackDataModel::TrackDataModel()
{
    tempoMap.push_back({ 0.0, 120.0 });
    timeSignatureMap.push_back({ 0.0, 4, 4 });
    audioClipSnapshot = std::make_unique<const std::vector<AudioClipState>>();
    publishedAudioClipSnapshot.store(audioClipSnapshot.get(), std::memory_order_seq_cst);
    midiClipSnapshot = std::make_unique<const std::vector<MidiClipState>>();
    publishedMidiClipSnapshot.store(midiClipSnapshot.get(), std::memory_order_seq_cst);
    for (size_t index = 0; index < maxTracks; ++index)
    {
        fxRacks[index] = std::make_unique<const FxRackSnapshot>();
        publishedFxRacks[index].store(fxRacks[index].get(), std::memory_order_seq_cst);
    }
    for (size_t index = 0; index < maxBuses; ++index)
    {
        busFxRacks[index] = std::make_unique<const FxRackSnapshot>();
        publishedBusFxRacks[index].store(busFxRacks[index].get(), std::memory_order_seq_cst);
    }
    publishRenderStructureSnapshot();
    startTimerHz(30);
}

void TrackDataModel::setSampleRate(double value) noexcept { sampleRate.store(std::max(1.0, value), std::memory_order_relaxed); }
void TrackDataModel::setBpm(double value) noexcept
{
    const auto clamped = std::clamp(value, 40.0, 220.0);
    if (bpm.load(std::memory_order_relaxed) != clamped)
    {
        bpm.store(clamped, std::memory_order_relaxed);
        if (tempoMap.empty()) tempoMap.push_back({ 0.0, clamped });
        else if (tempoMap.front().samplePosition == 0.0) tempoMap.front().bpm = clamped;
        markProjectModified();
    }
}
double TrackDataModel::getBpm() const noexcept { return bpm.load(); }
void TrackDataModel::setTimeSignatureNumerator(int value) noexcept
{
    const auto clamped = std::clamp(value, 1, 32);
    if (timeSignatureNumerator.load(std::memory_order_relaxed) != clamped)
    {
        timeSignatureNumerator.store(clamped, std::memory_order_relaxed);
        if (timeSignatureMap.empty()) timeSignatureMap.push_back({ 0.0, clamped, getTimeSignatureDenominator() });
        else if (timeSignatureMap.front().samplePosition == 0.0) timeSignatureMap.front().numerator = clamped;
        markProjectModified();
    }
}
int TrackDataModel::getTimeSignatureNumerator() const noexcept { return timeSignatureNumerator.load(); }
void TrackDataModel::setTimeSignatureDenominator(int value) noexcept
{
    const auto valid = value == 1 || value == 2 || value == 4 || value == 8 || value == 16;
    const auto denominator = valid ? value : 4;
    if (timeSignatureDenominator.load(std::memory_order_relaxed) != denominator)
    {
        timeSignatureDenominator.store(denominator, std::memory_order_relaxed);
        if (timeSignatureMap.empty()) timeSignatureMap.push_back({ 0.0, getTimeSignatureNumerator(), denominator });
        else if (timeSignatureMap.front().samplePosition == 0.0) timeSignatureMap.front().denominator = denominator;
        markProjectModified();
    }
}
int TrackDataModel::getTimeSignatureDenominator() const noexcept { return timeSignatureDenominator.load(); }

void TrackDataModel::setTimeSignatureAtSample(double samplePosition, int numerator, int denominator)
{
    const auto validDenominator = denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16;
    if (! std::isfinite(samplePosition) || samplePosition < 0.0 || numerator < 1 || numerator > 32 || ! validDenominator)
        return;

    const auto location = getBarBeatInfo(samplePosition);
    const auto alignedSample = barBeatToSamples(location.bars, 0, 0);
    const auto iter = std::lower_bound(timeSignatureMap.begin(), timeSignatureMap.end(), alignedSample,
                                       [] (const TimeSignatureEvent& event, double sample) { return event.samplePosition < sample; });
    if (iter != timeSignatureMap.end() && iter->samplePosition == alignedSample)
        *iter = { alignedSample, numerator, denominator };
    else
        timeSignatureMap.insert(iter, { alignedSample, numerator, denominator });

    if (alignedSample == 0.0)
    {
        timeSignatureNumerator.store(numerator, std::memory_order_relaxed);
        timeSignatureDenominator.store(denominator, std::memory_order_relaxed);
    }
    markProjectModified();
    sendChangeMessage();
}

TrackDataModel::TimeSignatureEvent TrackDataModel::getTimeSignatureAtSample(double samplePosition) const noexcept
{
    if (timeSignatureMap.empty()) return { 0.0, getTimeSignatureNumerator(), getTimeSignatureDenominator() };
    auto iter = timeSignatureMap.begin();
    while (iter + 1 != timeSignatureMap.end() && (iter + 1)->samplePosition <= samplePosition) ++iter;
    return *iter;
}

void TrackDataModel::addTempoEvent(double samplePosition, double eventBpm)
{
    tempoMap.push_back({ samplePosition, std::clamp(eventBpm, 40.0, 220.0) });
    std::sort(tempoMap.begin(), tempoMap.end(), [] (const TempoEvent& lhs, const TempoEvent& rhs)
    {
        return lhs.samplePosition < rhs.samplePosition;
    });
    markProjectModified();
}

void TrackDataModel::clearTempoMap()
{
    tempoMap.clear();
    tempoMap.push_back({ 0.0, 120.0 });
    markProjectModified();
}

double TrackDataModel::getTempoAtSample(double samplePosition) const noexcept
{
    if (tempoMap.empty()) return 120.0;
    auto iter = tempoMap.begin();
    while (iter + 1 != tempoMap.end() && (iter + 1)->samplePosition <= samplePosition) ++iter;
    return iter->bpm;
}

double TrackDataModel::quarterNotesAtSample(double samplePosition) const noexcept
{
    if (samplePosition <= 0.0 || tempoMap.empty()) return 0.0;
    double quarters = 0.0;
    for (size_t index = 0; index < tempoMap.size(); ++index)
    {
        const auto& event = tempoMap[index];
        if (samplePosition <= event.samplePosition) break;
        const auto next = index + 1 < tempoMap.size() ? tempoMap[index + 1].samplePosition : samplePosition;
        const auto end = std::min(samplePosition, next);
        quarters += std::max(0.0, end - event.samplePosition) / getSampleRate() * event.bpm / 60.0;
        if (samplePosition <= next) break;
    }
    return quarters;
}

double TrackDataModel::sampleAtQuarterNotes(double quarterNotes) const noexcept
{
    if (quarterNotes <= 0.0 || tempoMap.empty()) return 0.0;
    double accumulated = 0.0;
    for (size_t index = 0; index < tempoMap.size(); ++index)
    {
        const auto& event = tempoMap[index];
        const auto next = index + 1 < tempoMap.size() ? tempoMap[index + 1].samplePosition
                                                       : std::numeric_limits<double>::infinity();
        const auto segmentQuarters = std::isfinite(next)
            ? std::max(0.0, next - event.samplePosition) / getSampleRate() * event.bpm / 60.0
            : std::numeric_limits<double>::infinity();
        if (quarterNotes <= accumulated + segmentQuarters)
            return event.samplePosition + (quarterNotes - accumulated) * 60.0 / event.bpm * getSampleRate();
        accumulated += segmentQuarters;
    }
    return 0.0;
}

TrackDataModel::BarBeatInfo TrackDataModel::getBarBeatInfo(double samplePosition) const noexcept
{
    const auto targetQuarterNotes = quarterNotesAtSample(samplePosition);
    auto event = getTimeSignatureAtSample(0.0);
    auto eventQuarterNotes = 0.0;
    auto completedBars = 0;
    for (size_t index = 1; index < timeSignatureMap.size() && timeSignatureMap[index].samplePosition <= samplePosition; ++index)
    {
        const auto nextQuarterNotes = quarterNotesAtSample(timeSignatureMap[index].samplePosition);
        const auto quarterNotesPerBar = static_cast<double>(event.numerator) * 4.0 / event.denominator;
        completedBars += static_cast<int>(std::round((nextQuarterNotes - eventQuarterNotes) / quarterNotesPerBar));
        event = timeSignatureMap[index];
        eventQuarterNotes = nextQuarterNotes;
    }
    const auto signatureBeats = (targetQuarterNotes - eventQuarterNotes) * static_cast<double>(event.denominator) / 4.0;
    const auto beatsPerBar = std::max(1, event.numerator);
    const auto bars = completedBars + static_cast<int>(signatureBeats / beatsPerBar);
    const auto phase = signatureBeats - (static_cast<int>(signatureBeats / beatsPerBar) * beatsPerBar);
    const auto beats = static_cast<int>(phase);
    const auto subBeat = phase - beats;
    const auto pulses = static_cast<int>(subBeat * 960.0);
    return { bars, beats, pulses, subBeat - (pulses / 960.0) };
}

double TrackDataModel::barBeatToSamples(int bars, int beats, int pulses, double subPulse) const noexcept
{
    const auto targetBar = std::max(0, bars);
    auto event = getTimeSignatureAtSample(0.0);
    auto eventQuarterNotes = 0.0;
    auto completedBars = 0;
    for (size_t index = 1; index < timeSignatureMap.size(); ++index)
    {
        const auto nextQuarterNotes = quarterNotesAtSample(timeSignatureMap[index].samplePosition);
        const auto quartersPerBar = static_cast<double>(event.numerator) * 4.0 / event.denominator;
        const auto barsInSegment = static_cast<int>(std::round((nextQuarterNotes - eventQuarterNotes) / quartersPerBar));
        if (targetBar < completedBars + barsInSegment) break;
        completedBars += barsInSegment;
        event = timeSignatureMap[index];
        eventQuarterNotes = nextQuarterNotes;
    }
    const auto signatureBeats = (targetBar - completedBars) * static_cast<double>(event.numerator)
        + static_cast<double>(juce::jlimit(0, event.numerator - 1, beats))
        + juce::jlimit(0.0, 959.0, static_cast<double>(pulses)) / 960.0 + subPulse;
    return sampleAtQuarterNotes(eventQuarterNotes + signatureBeats * 4.0 / event.denominator);
}

double TrackDataModel::sampleToXPosition(double samplePos, double pixelsPerSecond) const noexcept
{
    return (samplePos / getSampleRate()) * std::max(0.0, pixelsPerSecond);
}

double TrackDataModel::getSnappedSamplePosition(double rawSamplePos, double snapResolution) const noexcept
{
    const auto samplesPerQuarterNote = (getSampleRate() * 60.0) / std::max(1.0, getBpm());
    const auto samplesPerSignatureBeat = samplesPerQuarterNote * 4.0 / getTimeSignatureDenominator();
    // snapResolution is measured in signature beats: 0.25 means a quarter of
    // the beat shown in the ruler, whether the meter is 4/4 or 6/8.
    const auto interval = std::max(1.0, samplesPerSignatureBeat * std::max(1.0 / 960.0, snapResolution));
    return std::max(0.0, std::round(rawSamplePos / interval) * interval);
}

bool TrackDataModel::isPlaying() const noexcept { return playing.load(std::memory_order_relaxed); }
void TrackDataModel::setPlaying(bool value) noexcept { playing.store(value, std::memory_order_relaxed); }
double TrackDataModel::getPlayheadPosition() const noexcept { return playheadPosition.load(std::memory_order_relaxed); }
void TrackDataModel::setPlayheadPosition(double value) noexcept { playheadPosition.store(std::max(0.0, value), std::memory_order_relaxed); }
bool TrackDataModel::canChangeTrackStructure() const noexcept { return true; }

void TrackDataModel::ensureTrackCount(size_t count)
{
    if (!canChangeTrackStructure()) return;
    while (trackStates.size() < std::min(count, maxTracks)) addTrack();
}

size_t TrackDataModel::getTrackCount() const noexcept { return trackStates.size(); }

const TrackDataModel::TrackState& TrackDataModel::getTrack(size_t index) const
{
    static const TrackState emptyState;
    return index < trackStates.size() ? trackStates[index] : emptyState;
}

TrackId TrackDataModel::getTrackId(size_t index) const noexcept { return index < trackStates.size() ? trackStates[index].id : TrackId {}; }

int TrackDataModel::getTrackIndex(TrackId id) const noexcept
{
    for (size_t index = 0; index < trackStates.size(); ++index)
        if (trackStates[index].id == id) return static_cast<int>(index);
    return -1;
}

TrackId TrackDataModel::addTrack(TrackType type)
{
    if (!canChangeTrackStructure() || trackStates.size() >= maxTracks) return {};
    auto before = captureEditState();
    trackStates.emplace_back();
    trackStates.back().id = { nextTrackId++ };
    trackStates.back().type = type;
    const auto number = juce::String(static_cast<int>(trackStates.size()));
    trackStates.back().name = type == TrackType::instrument ? "Instrument " + number
        : type == TrackType::externalMidi ? "External MIDI " + number : "Audio " + number;
    for (size_t slot = 0; slot < maxTracks; ++slot)
        if (!fxRackTrackIds[slot].isValid())
        {
            fxRackTrackIds[slot] = trackStates.back().id;
            break;
        }
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    return trackStates.back().id;
}

bool TrackDataModel::removeTrack(TrackId id)
{
    if (!canChangeTrackStructure()) return false;
    const auto index = getTrackIndex(id);
    if (index < 0) return false;
    auto before = captureEditState();
    const auto rackSlot = getFxRackSlot(id);
    trackStates.erase(trackStates.begin() + index);
    if (rackSlot >= 0)
    {
        fxRackTrackIds[static_cast<size_t>(rackSlot)] = {};
        publishFxRackSnapshot(static_cast<size_t>(rackSlot), std::make_unique<FxRackSnapshot>());
    }
    publishAudioClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    return true;
}

bool TrackDataModel::reorderTrack(TrackId id, size_t destinationIndex)
{
    if (!canChangeTrackStructure()) return false;
    const auto sourceIndex = getTrackIndex(id);
    if (sourceIndex < 0 || destinationIndex >= trackStates.size()) return false;
    if (static_cast<size_t>(sourceIndex) == destinationIndex) return true;
    auto before = captureEditState();
    auto track = std::move(trackStates[static_cast<size_t>(sourceIndex)]);
    trackStates.erase(trackStates.begin() + sourceIndex);
    trackStates.insert(trackStates.begin() + static_cast<std::ptrdiff_t>(destinationIndex), std::move(track));
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    return true;
}
BusId TrackDataModel::addBus()
{
    if (busStates.size() >= maxBuses) return {};
    const auto ordinal = static_cast<int>(busStates.size() + 1);
    // A bus created from a track Send is an effect return. Naming it as such
    // exposes its purpose before the performer opens the Mixer.
    busStates.push_back({ { nextBusId++ }, "FX Bus " + juce::String(ordinal) });
    publishRenderStructureSnapshot();
    markProjectModified();
    return busStates.back().id;
}

juce::String TrackDataModel::getBusDisplayName(BusId id) const
{
    const auto found = std::find_if(busStates.begin(), busStates.end(), [id] (const auto& bus) { return bus.id == id; });
    if (found == busStates.end())
        return {};

    return found->name.isNotEmpty() ? found->name
                                    : "Aux " + juce::String(static_cast<int>(std::distance(busStates.begin(), found)) + 1);
}

bool TrackDataModel::setBusName(BusId id, juce::String name)
{
    const auto found = std::find_if(busStates.begin(), busStates.end(), [id] (const auto& bus) { return bus.id == id; });
    if (found == busStates.end())
        return false;

    name = name.trim().substring(0, 48);
    if (name.isEmpty())
        name = "Aux " + juce::String(static_cast<int>(std::distance(busStates.begin(), found)) + 1);
    if (found->name == name)
        return true;

    found->name = std::move(name);
    markProjectModified();
    sendChangeMessage();
    return true;
}
bool TrackDataModel::removeBus(BusId id)
{
    const auto found = std::find_if(busStates.begin(), busStates.end(), [id] (const BusState& bus) { return bus.id == id; });
    if (found == busStates.end()) return false;
    const auto removedIndex = static_cast<size_t>(std::distance(busStates.begin(), found));
    if (busFxRacks[removedIndex] != nullptr)
        retiredBusFxRacks[0].push_back(std::move(busFxRacks[removedIndex]));
    for (size_t index = removedIndex; index + 1 < maxBuses; ++index)
    {
        busFxRacks[index] = std::move(busFxRacks[index + 1]);
        publishedBusFxRacks[index].store(publishedBusFxRacks[index + 1].load(std::memory_order_acquire),
                                         std::memory_order_release);
        for (auto& retired : retiredBusFxRacks[index + 1])
            retiredBusFxRacks[index].push_back(std::move(retired));
        retiredBusFxRacks[index + 1].clear();
    }
    busFxRacks[maxBuses - 1] = std::make_unique<FxRackSnapshot>();
    publishedBusFxRacks[maxBuses - 1].store(busFxRacks[maxBuses - 1].get(), std::memory_order_release);
    busStates.erase(found);
    for (auto& track : trackStates)
    {
        if (track.outputBus == id)
            track.outputBus = {};
        uint8_t activeRoutes = 0;
        for (auto& send : track.sends)
        {
            if (send.targetBus == id)
                send = {};
            if (send.targetBus.isValid())
                ++activeRoutes;
        }
        track.activeSendCount = activeRoutes;
    }
    publishRenderStructureSnapshot();
    markProjectModified();
    return true;
}
bool TrackDataModel::setBusGain(BusId id, float gain)
{
    const auto found = std::find_if(busStates.begin(), busStates.end(), [id] (const BusState& bus) { return bus.id == id; });
    if (found == busStates.end()) return false;
    const auto value = juce::jlimit(0.0f, AudioGainRange::maximumChannelGain, gain);
    if (found->gain == value) return true;
    found->gain = value;
    publishRenderStructureSnapshot();
    markProjectModified();
    return true;
}
bool TrackDataModel::setBusBalance(BusId id, float balance)
{
    const auto found = std::find_if(busStates.begin(), busStates.end(), [id] (const BusState& bus) { return bus.id == id; });
    if (found == busStates.end() || ! std::isfinite(balance)) return false;
    const auto value = juce::jlimit(-1.0f, 1.0f, balance);
    if (found->balance == value) return true;
    found->balance = value;
    publishRenderStructureSnapshot();
    markProjectModified();
    return true;
}
bool TrackDataModel::setBusMuted(BusId id, bool muted)
{
    const auto found = std::find_if(busStates.begin(), busStates.end(), [id] (const BusState& bus) { return bus.id == id; });
    if (found == busStates.end()) return false;
    if (found->muted == muted) return true;
    found->muted = muted;
    publishRenderStructureSnapshot();
    markProjectModified();
    return true;
}
const TrackDataModel::FxRackSnapshot* TrackDataModel::getBusFxRackSnapshot(BusId bus) const noexcept
{
    const auto index = std::find_if(busStates.begin(), busStates.end(), [bus] (const BusState& state) { return state.id == bus; });
    if (index == busStates.end()) return nullptr;
    return publishedBusFxRacks[static_cast<size_t>(std::distance(busStates.begin(), index))].load(std::memory_order_acquire);
}
void TrackDataModel::setBusFxProcessor(BusId bus, size_t slot, std::shared_ptr<AudioEffectProcessor> processor)
{
    if (slot >= maxFxSlots) return;
    const auto found = std::find_if(busStates.begin(), busStates.end(), [bus] (const BusState& state) { return state.id == bus; });
    if (found == busStates.end()) return;
    const auto index = static_cast<size_t>(std::distance(busStates.begin(), found));
    auto updated = std::make_unique<FxRackSnapshot>();
    if (const auto* current = getBusFxRackSnapshot(bus); current != nullptr) *updated = *current;
    updated->processors[slot] = std::move(processor);
    publishBusFxRackSnapshot(index, std::move(updated));
    publishRenderStructureSnapshot();
    markProjectModified();
    sendChangeMessage();
}
void TrackDataModel::setBusFxBypassed(BusId bus, size_t slot, bool bypassed)
{
    if (slot >= maxFxSlots) return;
    const auto found = std::find_if(busStates.begin(), busStates.end(), [bus] (const BusState& state) { return state.id == bus; });
    if (found == busStates.end()) return;
    const auto index = static_cast<size_t>(std::distance(busStates.begin(), found));
    auto updated = std::make_unique<FxRackSnapshot>();
    if (const auto* current = getBusFxRackSnapshot(bus); current != nullptr) *updated = *current;
    updated->bypass[slot] = bypassed;
    publishBusFxRackSnapshot(index, std::move(updated));
    publishRenderStructureSnapshot();
    markProjectModified();
    sendChangeMessage();
}
bool TrackDataModel::setTrackOutputBus(TrackId trackId, BusId bus)
{
    if (bus.isValid() && std::none_of(busStates.begin(), busStates.end(), [bus] (const BusState& state) { return state.id == bus; })) return false;
    const auto index = getTrackIndex(trackId); if (index < 0) return false;
    trackStates[static_cast<size_t>(index)].outputBus = bus; publishRenderStructureSnapshot(); markProjectModified(); return true;
}
bool TrackDataModel::setTrackSend(TrackId trackId, BusId bus, float amount)
{
    if (! bus.isValid() || std::none_of(busStates.begin(), busStates.end(), [bus] (const BusState& state) { return state.id == bus; })) return false;
    const auto index = getTrackIndex(trackId); if (index < 0) return false;
    return setTrackSendRoute(trackId, 0, bus, amount, false);
}
bool TrackDataModel::setTrackSendRoute(TrackId trackId, size_t route, BusId bus, float amount, bool preFader)
{
    if (route >= maxSendsPerTrack || ! bus.isValid() || std::none_of(busStates.begin(), busStates.end(), [bus] (const BusState& state) { return state.id == bus; })) return false;
    const auto index = getTrackIndex(trackId); if (index < 0) return false;
    auto& track = trackStates[static_cast<size_t>(index)];
    const auto constrainedAmount = juce::jlimit(0.0f, maximumSendGain, amount);
    const auto postFaderBeforePan = ! preFader && track.sends[route].postFaderBeforePan;
    track.sends[route] = { bus, constrainedAmount, preFader, postFaderBeforePan };
    if (isPlaying() && track.sendAutomation[route].getMode() != AutomationMode::read)
        try { track.sendAutomation[route].upsertPoint(getPlayheadPosition(), constrainedAmount); }
        catch (const std::length_error&) {}
    track.activeSendCount = static_cast<uint8_t>(std::count_if(track.sends.begin(), track.sends.end(),
        [] (const TrackState::SendRoute& send) { return send.targetBus.isValid(); }));
    publishRenderStructureSnapshot(); markProjectModified(); return true;
}
bool TrackDataModel::setTrackSendPostFaderBeforePan(TrackId id, size_t route, bool enabled)
{
    const auto index = getTrackIndex(id);
    if (index < 0 || route >= maxSendsPerTrack) return false;
    auto& send = trackStates[static_cast<size_t>(index)].sends[route];
    if (! send.targetBus.isValid()) return false;
    send.preFader = false;
    send.postFaderBeforePan = enabled;
    publishRenderStructureSnapshot();
    markProjectModified();
    sendChangeMessage();
    return true;
}
bool TrackDataModel::clearTrackSendRoute(TrackId trackId, size_t route)
{
    const auto index = getTrackIndex(trackId);
    if (index < 0 || route >= maxSendsPerTrack)
        return false;

    auto& track = trackStates[static_cast<size_t>(index)];
    if (! track.sends[route].targetBus.isValid())
        return false;
    track.sends[route] = {};
    track.activeSendCount = static_cast<uint8_t>(std::count_if(track.sends.begin(), track.sends.end(),
        [] (const TrackState::SendRoute& send) { return send.targetBus.isValid(); }));
    publishRenderStructureSnapshot();
    markProjectModified();
    return true;
}
bool TrackDataModel::clearTrackSend(TrackId trackId)
{
    const auto index = getTrackIndex(trackId); if (index < 0) return false;
    auto& track = trackStates[static_cast<size_t>(index)];
    if (track.activeSendCount == 0) return true;
    track.sends.fill({});
    track.activeSendCount = 0;
    publishRenderStructureSnapshot();
    markProjectModified();
    return true;
}

void TrackDataModel::setTrackVolume(size_t index, float value)
{
    if (index >= trackStates.size())
        return;

    const auto constrainedValue = juce::jlimit(0.0f, AudioGainRange::maximumChannelGain, value);
    auto& track = trackStates[index];
    if (track.volume.load(std::memory_order_relaxed) == constrainedValue)
        return;

    auto before = captureEditState();
    track.volume.store(constrainedValue, std::memory_order_relaxed);
    const auto mode = track.volumeAutomation.getMode();
    if (isPlaying() && mode != AutomationMode::read)
    {
        try { track.volumeAutomation.upsertPoint(getPlayheadPosition(), constrainedValue); }
        catch (const std::length_error&) {}
    }
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
}
void TrackDataModel::setTrackName(size_t index, const juce::String& value)
{
    if (index >= trackStates.size())
        return;

    const auto name = value.trim().substring(0, 64);
    if (name.isEmpty() || trackStates[index].name == name)
        return;

    auto before = captureEditState();
    trackStates[index].name = name;
    commitEdit(std::move(before));
    sendChangeMessage();
}
void TrackDataModel::setTrackPan(size_t index, float value)
{
    if (index >= trackStates.size())
        return;

    const auto constrainedValue = juce::jlimit(-1.0f, 1.0f, value);
    auto& track = trackStates[index];
    if (track.pan.load(std::memory_order_relaxed) == constrainedValue)
        return;

    auto before = captureEditState();
    track.pan.store(constrainedValue, std::memory_order_relaxed);
    const auto mode = track.panAutomation.getMode();
    if (isPlaying() && mode != AutomationMode::read)
    {
        try { track.panAutomation.upsertPoint(getPlayheadPosition(), constrainedValue); }
        catch (const std::length_error&) {}
    }
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
}

const AutomationLane* TrackDataModel::getTrackAutomationLane(size_t trackIndex, AutomationParameter parameter) const noexcept
{
    if (trackIndex >= trackStates.size())
        return nullptr;

    const auto& track = trackStates[trackIndex];
    switch (parameter)
    {
        case AutomationParameter::trackVolume: return &track.volumeAutomation;
        case AutomationParameter::trackPan:    return &track.panAutomation;
        case AutomationParameter::sendLevel:   return nullptr;
    }

    return nullptr;
}

const AutomationLane* TrackDataModel::getTrackSendAutomationLane(size_t trackIndex, size_t sendSlot) const noexcept
{
    if (trackIndex >= trackStates.size() || sendSlot >= maxSendsPerTrack)
        return nullptr;
    return &trackStates[trackIndex].sendAutomation[sendSlot];
}

bool TrackDataModel::upsertTrackSendAutomationPoint(size_t trackIndex, size_t sendSlot,
                                                     double samplePosition, float value)
{
    if (trackIndex >= trackStates.size() || sendSlot >= maxSendsPerTrack)
        return false;

    try
    {
        auto before = captureEditState();
        trackStates[trackIndex].sendAutomation[sendSlot].upsertPoint(samplePosition, value);
        publishRenderStructureSnapshot();
        commitEdit(std::move(before));
        sendChangeMessage();
        return true;
    }
    catch (const std::invalid_argument&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }
}

bool TrackDataModel::setTrackSendAutomationMode(size_t trackIndex, size_t sendSlot, AutomationMode mode)
{
    if (trackIndex >= trackStates.size() || sendSlot >= maxSendsPerTrack)
        return false;

    auto& lane = trackStates[trackIndex].sendAutomation[sendSlot];
    if (lane.getMode() == mode)
        return false;

    auto before = captureEditState();
    lane.setMode(mode);
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
    return true;
}

AutomationLane* TrackDataModel::getMutableTrackAutomationLane(size_t trackIndex, AutomationParameter parameter) noexcept
{
    if (trackIndex >= trackStates.size())
        return nullptr;

    auto& track = trackStates[trackIndex];
    switch (parameter)
    {
        case AutomationParameter::trackVolume: return &track.volumeAutomation;
        case AutomationParameter::trackPan:    return &track.panAutomation;
        case AutomationParameter::sendLevel:   return nullptr;
    }

    return nullptr;
}

bool TrackDataModel::upsertTrackAutomationPoint(size_t trackIndex, AutomationParameter parameter,
                                                double samplePosition, float value)
{
    if (trackIndex >= trackStates.size())
        return false;

    auto* lane = getMutableTrackAutomationLane(trackIndex, parameter);
    if (lane == nullptr)
        return false;

    try
    {
        auto before = captureEditState();
        lane->upsertPoint(samplePosition, value);
        publishRenderStructureSnapshot();
        commitEdit(std::move(before));
        sendChangeMessage();
        return true;
    }
    catch (const std::invalid_argument&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }
}

bool TrackDataModel::removeTrackAutomationPoint(size_t trackIndex, AutomationParameter parameter,
                                                double samplePosition)
{
    if (trackIndex >= trackStates.size())
        return false;

    auto before = captureEditState();
    auto* lane = getMutableTrackAutomationLane(trackIndex, parameter);
    if (lane == nullptr || ! lane->removePoint(samplePosition))
        return false;

    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
    return true;
}

bool TrackDataModel::setTrackAutomationMode(size_t trackIndex, AutomationParameter parameter, AutomationMode mode)
{
    if (trackIndex >= trackStates.size())
        return false;

    auto* lane = getMutableTrackAutomationLane(trackIndex, parameter);
    if (lane == nullptr || lane->getMode() == mode)
        return false;

    auto before = captureEditState();
    lane->setMode(mode);
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
    return true;
}
void TrackDataModel::setTrackMuted(size_t index, bool value) noexcept
{
    if (index < trackStates.size())
    {
        if (trackStates[index].muted.load(std::memory_order_relaxed) == value) return;
        auto before = captureEditState();
        trackStates[index].muted.store(value, std::memory_order_relaxed);
        publishRenderStructureSnapshot();
        commitEdit(std::move(before));
    }
}
void TrackDataModel::setTrackSolo(size_t index, bool value) noexcept
{
    if (index < trackStates.size())
    {
        if (trackStates[index].solo.load(std::memory_order_relaxed) == value) return;
        auto before = captureEditState();
        trackStates[index].solo.store(value, std::memory_order_relaxed);
        publishRenderStructureSnapshot();
        commitEdit(std::move(before));
    }
}
void TrackDataModel::setTrackSoloSafe(size_t index, bool value) noexcept
{
    if (index >= trackStates.size() || trackStates[index].soloSafe.load(std::memory_order_relaxed) == value)
        return;
    auto before = captureEditState();
    trackStates[index].soloSafe.store(value, std::memory_order_relaxed);
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
}

bool TrackDataModel::isTrackSoloSafe(size_t index) const noexcept
{
    return index < trackStates.size() && trackStates[index].soloSafe.load(std::memory_order_relaxed);
}

void TrackDataModel::setTrackArmed(size_t index, bool value) noexcept
{
    if (index >= trackStates.size() || trackStates[index].armed.load(std::memory_order_relaxed) == value)
        return;

    trackStates[index].armed.store(value, std::memory_order_relaxed);
    publishRenderStructureSnapshot();
    sendChangeMessage();
}
bool TrackDataModel::isTrackArmed(size_t index) const noexcept
{
    return index < trackStates.size() && trackStates[index].armed.load(std::memory_order_relaxed);
}
void TrackDataModel::setTrackInputMonitoring(size_t index, bool enabled, int channel) noexcept
{
    if (index >= trackStates.size()) return;
    auto& track = trackStates[index];
    const auto clampedChannel = channel >= 0 ? juce::jmax(0, channel)
                                             : track.inputChannel.load(std::memory_order_relaxed);
    if (track.inputMonitoring.load(std::memory_order_relaxed) == enabled
        && track.inputChannel.load(std::memory_order_relaxed) == clampedChannel)
        return;
    auto before = captureEditState();
    track.inputMonitoring.store(enabled, std::memory_order_relaxed);
    track.autoInputMonitoring.store(false, std::memory_order_relaxed);
    if (enabled && track.monitorDestination.load(std::memory_order_relaxed) == MonitorDestination::off)
        track.monitorDestination.store(MonitorDestination::cue, std::memory_order_relaxed);
    if (! enabled)
        track.monitorPermitted.store(false, std::memory_order_relaxed);
    track.inputChannel.store(clampedChannel, std::memory_order_relaxed);
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
}

void TrackDataModel::cycleTrackInputMonitoring(size_t index) noexcept
{
    if (index >= trackStates.size() || trackStates[index].type != TrackType::audio)
        return;

    const auto& track = trackStates[index];
    const auto isAlwaysOn = track.inputMonitoring.load(std::memory_order_relaxed);
    const auto isAuto = track.autoInputMonitoring.load(std::memory_order_relaxed);
    const auto inputChannel = track.inputChannel.load(std::memory_order_relaxed);
    const auto inputChannelCount = track.inputChannelCount.load(std::memory_order_relaxed);

    // Off becomes Auto, Auto becomes In, and In becomes Off.
    setTrackInputConfiguration(index, ! isAlwaysOn && ! isAuto, isAuto,
                               inputChannel, inputChannelCount);
}

void TrackDataModel::setTrackInputConfiguration(size_t index, bool monitorWhenArmed, bool monitorAlways,
                                                 int channel, int channelCount) noexcept
{
    if (index >= trackStates.size()) return;
    auto& track = trackStates[index];
    const auto clampedChannel = juce::jmax(0, channel);
    const auto clampedChannelCount = juce::jlimit(1, 2, channelCount);
    if (track.inputMonitoring.load(std::memory_order_relaxed) == monitorAlways
        && track.autoInputMonitoring.load(std::memory_order_relaxed) == monitorWhenArmed
        && track.inputChannel.load(std::memory_order_relaxed) == clampedChannel
        && track.inputChannelCount.load(std::memory_order_relaxed) == clampedChannelCount)
        return;
    auto before = captureEditState();
    track.inputMonitoring.store(monitorAlways, std::memory_order_relaxed);
    track.autoInputMonitoring.store(monitorWhenArmed, std::memory_order_relaxed);
    if ((monitorAlways || monitorWhenArmed)
        && track.monitorDestination.load(std::memory_order_relaxed) == MonitorDestination::off)
        track.monitorDestination.store(MonitorDestination::cue, std::memory_order_relaxed);
    if (! monitorAlways && ! monitorWhenArmed)
        track.monitorPermitted.store(false, std::memory_order_relaxed);
    track.inputChannel.store(clampedChannel, std::memory_order_relaxed);
    track.inputChannelCount.store(clampedChannelCount, std::memory_order_relaxed);
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
}

void TrackDataModel::setTrackMonitorDestination(size_t index, MonitorDestination destination) noexcept
{
    if (index >= trackStates.size()) return;
    auto& track = trackStates[index];
    if (track.monitorDestination.load(std::memory_order_relaxed) == destination) return;
    auto before = captureEditState();
    track.monitorDestination.store(destination, std::memory_order_relaxed);
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
}

MonitorDestination TrackDataModel::getTrackMonitorDestination(size_t index) const noexcept
{
    return index < trackStates.size() ? trackStates[index].monitorDestination.load(std::memory_order_relaxed)
                                      : MonitorDestination::off;
}

void TrackDataModel::setTrackMonitorPermission(size_t index, bool permitted) noexcept
{
    if (index >= trackStates.size()) return;
    auto& track = trackStates[index];
    if (track.monitorPermitted.exchange(permitted, std::memory_order_relaxed) == permitted) return;
    publishRenderStructureSnapshot();
    sendChangeMessage();
}

void TrackDataModel::clearTrackMonitorPermissions() noexcept
{
    auto changed = false;
    for (auto& track : trackStates)
        changed = track.monitorPermitted.exchange(false, std::memory_order_relaxed) || changed;
    if (changed)
    {
        publishRenderStructureSnapshot();
        sendChangeMessage();
    }
}

void TrackDataModel::activateConfiguredMonitorRoutes(MonitorDestination destination) noexcept
{
    if (destination == MonitorDestination::off)
    {
        clearTrackMonitorPermissions();
        return;
    }

    auto changed = false;
    for (auto& track : trackStates)
    {
        // A workspace switch must never make an arbitrary audio track live.
        // It only restores routes the user has explicitly marked In/Auto.
        const auto configured = track.type == TrackType::audio
            && (track.inputMonitoring.load(std::memory_order_relaxed)
                || (track.autoInputMonitoring.load(std::memory_order_relaxed)
                    && track.armed.load(std::memory_order_relaxed)));
        if (! configured)
            continue;

        if (track.monitorDestination.exchange(destination, std::memory_order_relaxed) != destination)
            changed = true;
        if (! track.monitorPermitted.exchange(true, std::memory_order_relaxed))
            changed = true;
    }
    if (changed)
    {
        publishRenderStructureSnapshot();
        sendChangeMessage();
    }
}

void TrackDataModel::setTrackMidiInputChannel(size_t index, int midiChannel) noexcept
{
    if (index >= trackStates.size())
        return;

    const auto channel = juce::jlimit(0, 16, midiChannel);
    auto& track = trackStates[index];
    if (track.midiInputChannel.load(std::memory_order_relaxed) == channel)
        return;

    auto before = captureEditState();
    track.midiInputChannel.store(channel, std::memory_order_relaxed);
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
}

int TrackDataModel::getTrackMidiInputChannel(size_t index) const noexcept
{
    return index < trackStates.size()
        ? trackStates[index].midiInputChannel.load(std::memory_order_relaxed) : 0;
}

void TrackDataModel::setTrackMidiInputDevice(size_t index, const juce::String& identifier)
{
    if (index >= trackStates.size())
        return;

    auto& track = trackStates[index];
    if (track.midiInputDeviceIdentifier == identifier)
        return;

    auto before = captureEditState();
    track.midiInputDeviceIdentifier = identifier;
    track.midiInputDeviceToken.store(identifier.isEmpty() ? 0u : static_cast<uint32_t>(identifier.hashCode()),
                                     std::memory_order_relaxed);
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
}

juce::String TrackDataModel::getTrackMidiInputDevice(size_t index) const
{
    return index < trackStates.size() ? trackStates[index].midiInputDeviceIdentifier : juce::String {};
}

bool TrackDataModel::isTrackInputMonitoring(size_t index) const noexcept
{
    return index < trackStates.size() && trackStates[index].inputMonitoring.load(std::memory_order_relaxed);
}
int TrackDataModel::getFirstArmedTrackIndex() const noexcept
{
    for (size_t index = 0; index < trackStates.size(); ++index)
        if (trackStates[index].armed.load(std::memory_order_relaxed))
            return static_cast<int>(index);
    return -1;
}
MidiClipId TrackDataModel::addMidiClip(TrackId track, double startSample)
{
    if (getTrackIndex(track) < 0) return {};
    auto before = captureEditState();
    midiClips.push_back({ { nextMidiClipId++ }, track, juce::jmax(0.0, startSample), {} });
    publishMidiClipSnapshot();
    commitEdit(std::move(before));
    return midiClips.back().id;
}
MidiEventId TrackDataModel::addMidiNote(MidiClipId clipId, int pitch, float velocity, double startSample, double durationSamples, int channel)
{
    for (auto& clip : midiClips) if (clip.id == clipId)
    {
        auto before = captureEditState();
        MidiNoteEvent note { { nextMidiEventId++ }, juce::jlimit(0, 127, pitch), juce::jlimit(0.0f, 1.0f, velocity),
                             juce::jmax(0.0, startSample), juce::jmax(1.0, durationSamples), juce::jlimit(1, 16, channel) };
        clip.notes.push_back(note); publishMidiClipSnapshot(); commitEdit(std::move(before)); return note.id;
    }
    return {};
}

MidiClipId TrackDataModel::addMidiRecording(TrackId track, double startSample, std::vector<MidiNoteEvent> notes)
{
    if (getTrackIndex(track) < 0 || notes.empty())
        return {};

    auto before = captureEditState();
    MidiClipState clip { { nextMidiClipId++ }, track, juce::jmax(0.0, startSample), {} };
    clip.notes.reserve(notes.size());
    for (auto& note : notes)
    {
        note.id = { nextMidiEventId++ };
        note.pitch = juce::jlimit(0, 127, note.pitch);
        note.velocity = juce::jlimit(0.0f, 1.0f, note.velocity);
        note.startSample = juce::jmax(0.0, note.startSample);
        note.durationSamples = juce::jmax(1.0, note.durationSamples);
        note.channel = juce::jlimit(1, 16, note.channel);
        clip.notes.push_back(note);
    }
    midiClips.push_back(std::move(clip));
    publishMidiClipSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
    return midiClips.back().id;
}

bool TrackDataModel::moveMidiClip(MidiClipId clipId, TrackId destinationTrack, double startSample)
{
    if (getTrackIndex(destinationTrack) < 0)
        return false;

    for (auto& clip : midiClips)
    {
        if (clip.id != clipId)
            continue;

        auto before = captureEditState();
        clip.trackId = destinationTrack;
        clip.startSample = juce::jmax(0.0, startSample);
        publishMidiClipSnapshot();
        commitEdit(std::move(before));
        sendChangeMessage();
        return true;
    }

    return false;
}

MidiClipId TrackDataModel::duplicateMidiClip(MidiClipId clipId, TrackId destinationTrack, double startSample)
{
    if (getTrackIndex(destinationTrack) < 0)
        return {};

    for (const auto& clip : midiClips)
    {
        if (clip.id != clipId)
            continue;

        auto before = captureEditState();
        auto duplicate = clip;
        duplicate.id = { nextMidiClipId++ };
        duplicate.trackId = destinationTrack;
        duplicate.startSample = juce::jmax(0.0, startSample);
        for (auto& note : duplicate.notes)
            note.id = { nextMidiEventId++ };

        midiClips.push_back(std::move(duplicate));
        publishMidiClipSnapshot();
        commitEdit(std::move(before));
        sendChangeMessage();
        return midiClips.back().id;
    }

    return {};
}

bool TrackDataModel::setMidiClipDuration(MidiClipId clipId, double durationSamples)
{
    if (! std::isfinite(durationSamples) || durationSamples < 1.0)
        return false;

    for (auto& clip : midiClips)
    {
        if (clip.id != clipId)
            continue;

        auto before = captureEditState();
        clip.durationSamples = durationSamples;
        publishMidiClipSnapshot();
        commitEdit(std::move(before));
        sendChangeMessage();
        return true;
    }

    return false;
}

bool TrackDataModel::trimMidiClip(MidiClipId clipId, double startSample, double durationSamples)
{
    if (! std::isfinite(startSample) || ! std::isfinite(durationSamples)
        || startSample < 0.0 || durationSamples < 1.0)
        return false;

    for (auto& clip : midiClips)
    {
        if (clip.id != clipId)
            continue;

        const auto delta = startSample - clip.startSample;
        const auto newContentOffset = clip.contentOffsetSamples + delta;
        if (newContentOffset < 0.0)
            return false;

        auto before = captureEditState();
        clip.startSample = startSample;
        clip.contentOffsetSamples = newContentOffset;
        clip.durationSamples = durationSamples;
        publishMidiClipSnapshot();
        commitEdit(std::move(before));
        sendChangeMessage();
        return true;
    }
    return false;
}

bool TrackDataModel::deleteMidiClip(MidiClipId clipId)
{
    const auto found = std::find_if(midiClips.begin(), midiClips.end(), [clipId] (const auto& clip) { return clip.id == clipId; });
    if (found == midiClips.end())
        return false;

    auto before = captureEditState();
    midiClips.erase(found);
    publishMidiClipSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
    return true;
}

juce::Result TrackDataModel::importMidiTracks(const std::vector<ImportedMidiTrack>& tracks, double startSample)
{
    const auto importCount = static_cast<size_t>(std::count_if(tracks.begin(), tracks.end(), [] (const auto& track)
    {
        return ! track.notes.empty();
    }));
    if (importCount == 0)
        return juce::Result::fail("The MIDI import contains no note data.");
    if (! canChangeTrackStructure() || trackStates.size() + importCount > maxTracks)
        return juce::Result::fail("There are not enough available track slots for this MIDI file.");

    auto before = captureEditState();
    for (const auto& imported : tracks)
    {
        if (imported.notes.empty())
            continue;

        trackStates.emplace_back();
        auto& track = trackStates.back();
        track.id = { nextTrackId++ };
        track.type = TrackType::instrument;
        track.name = imported.name.isNotEmpty() ? imported.name : "MIDI " + juce::String(trackStates.size());
        for (size_t slot = 0; slot < maxTracks; ++slot)
            if (! fxRackTrackIds[slot].isValid())
            {
                fxRackTrackIds[slot] = track.id;
                break;
            }

        MidiClipState clip { { nextMidiClipId++ }, track.id, juce::jmax(0.0, startSample), {} };
        clip.notes.reserve(imported.notes.size());
        for (const auto& importedNote : imported.notes)
            clip.notes.push_back({ { nextMidiEventId++ }, juce::jlimit(0, 127, importedNote.pitch),
                                   juce::jlimit(0.0f, 1.0f, importedNote.velocity),
                                   juce::jmax(0.0, importedNote.startSample),
                                   juce::jmax(1.0, importedNote.durationSamples),
                                   juce::jlimit(1, 16, importedNote.channel) });
        midiClips.push_back(std::move(clip));
    }

    publishMidiClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
    return juce::Result::ok();
}
const std::vector<MidiClipState>& TrackDataModel::getMidiClips() const noexcept { return midiClips; }
bool TrackDataModel::deleteMidiNote(MidiClipId clipId, MidiEventId noteId)
{
    for (auto& clip : midiClips) if (clip.id == clipId)
    {
        const auto it = std::find_if(clip.notes.begin(), clip.notes.end(), [noteId] (const MidiNoteEvent& note) { return note.id == noteId; });
        if (it == clip.notes.end()) return false;
        auto before = captureEditState();
        clip.notes.erase(it); publishMidiClipSnapshot(); commitEdit(std::move(before)); return true;
    }
    return false;
}
bool TrackDataModel::moveMidiNote(MidiClipId clipId, MidiEventId noteId, int pitch, double startSample)
{
    for (auto& clip : midiClips) if (clip.id == clipId) for (auto& note : clip.notes) if (note.id == noteId)
    { auto before = captureEditState(); note.pitch = juce::jlimit(0, 127, pitch); note.startSample = juce::jmax(0.0, startSample); publishMidiClipSnapshot(); commitEdit(std::move(before)); return true; }
    return false;
}
bool TrackDataModel::resizeMidiNote(MidiClipId clipId, MidiEventId noteId, double durationSamples)
{
    for (auto& clip : midiClips) if (clip.id == clipId) for (auto& note : clip.notes) if (note.id == noteId)
    { auto before = captureEditState(); note.durationSamples = juce::jmax(1.0, durationSamples); publishMidiClipSnapshot(); commitEdit(std::move(before)); return true; }
    return false;
}
bool TrackDataModel::setMidiNoteVelocity(MidiClipId clipId, MidiEventId noteId, float velocity)
{
    for (auto& clip : midiClips) if (clip.id == clipId) for (auto& note : clip.notes) if (note.id == noteId)
    { auto before = captureEditState(); note.velocity = juce::jlimit(0.0f, 1.0f, velocity); publishMidiClipSnapshot(); commitEdit(std::move(before)); return true; }
    return false;
}
bool TrackDataModel::transposeMidiClip(MidiClipId clipId, int semitones)
{
    if (semitones == 0) return false;
    for (auto& clip : midiClips) if (clip.id == clipId)
    {
        auto before = captureEditState();
        for (auto& note : clip.notes)
            note.pitch = juce::jlimit(0, 127, note.pitch + semitones);
        publishMidiClipSnapshot();
        commitEdit(std::move(before));
        return true;
    }
    return false;
}

bool TrackDataModel::quantizeMidiClip(MidiClipId clipId, double gridSamples)
{
    if (! std::isfinite(gridSamples) || gridSamples <= 0.0) return false;
    for (auto& clip : midiClips) if (clip.id == clipId)
    {
        auto before = captureEditState();
        for (auto& note : clip.notes)
            note.startSample = juce::jmax(0.0, std::round(note.startSample / gridSamples) * gridSamples);
        publishMidiClipSnapshot();
        commitEdit(std::move(before));
        return true;
    }
    return false;
}

ClipId TrackDataModel::addClipToTrack(int trackIndex, const juce::File& file, double startSample,
                                      std::shared_ptr<juce::AudioBuffer<float>> loadedBuffer)
{
    if (trackIndex < 0 || loadedBuffer == nullptr) return {};
    ensureTrackCount(static_cast<size_t>(trackIndex + 1));
    if (static_cast<size_t>(trackIndex) >= trackStates.size()) return {};
    auto before = captureEditState();

    AudioClipState clip;
    clip.id = { nextClipId++ };
    const auto& source = audioMediaPool.registerDecodedSource(file, loadedBuffer);
    const auto playbackData = source.playbackData.lock();
    if (playbackData == nullptr)
        return {};
    clip.sourceId = source.id;
    clip.sourceFile = source.sourceFile;
    clip.startSample = std::max(0.0, startSample);
    clip.durationSamples = playbackData->getNumSamples();
    clip.trackId = trackStates[static_cast<size_t>(trackIndex)].id;
    clip.clipName = file.getFileNameWithoutExtension();
    clip.cachedBuffer = playbackData;
    clip.mediaStatus = AudioMediaStatus::Ready;
    trackStates[static_cast<size_t>(trackIndex)].clips.push_back(clip);
    publishAudioClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
    return clip.id;
}

ClipId TrackDataModel::addRecordedClipToTrack(int trackIndex, const juce::File& file, double startSample,
                                              std::shared_ptr<juce::AudioBuffer<float>> loadedBuffer)
{
    if (trackIndex < 0 || loadedBuffer == nullptr)
        return {};
    ensureTrackCount(static_cast<size_t>(trackIndex + 1));
    if (static_cast<size_t>(trackIndex) >= trackStates.size())
        return {};

    const auto& source = audioMediaPool.registerDecodedSource(file, loadedBuffer);
    const auto playbackData = source.playbackData.lock();
    if (playbackData == nullptr || playbackData->getNumSamples() <= 0)
        return {};

    auto before = captureEditState();
    AudioClipState clip;
    clip.id = { nextClipId++ };
    clip.sourceId = source.id;
    clip.sourceFile = source.sourceFile;
    clip.startSample = std::max(0.0, startSample);
    clip.durationSamples = playbackData->getNumSamples();
    clip.trackId = trackStates[static_cast<size_t>(trackIndex)].id;
    clip.clipName = file.getFileNameWithoutExtension();
    clip.cachedBuffer = playbackData;
    clip.mediaStatus = AudioMediaStatus::Ready;
    clip.recordedTake = true;
    clip.takeId = clip.id.value;

    auto& trackClips = trackStates[static_cast<size_t>(trackIndex)].clips;
    const auto takeEnd = clip.startSample + clip.durationSamples;
    uint64_t groupId = 0;
    for (const auto& existing : trackClips)
    {
        const auto overlaps = existing.startSample < takeEnd
            && existing.startSample + existing.durationSamples > clip.startSample;
        if (overlaps && existing.recordedTake)
        {
            groupId = existing.takeGroupId;
            if (groupId != 0)
                break;
        }
    }

    if (groupId == 0)
    {
        const auto hasRecordedOverlap = std::any_of(trackClips.begin(), trackClips.end(), [&] (const AudioClipState& existing)
        {
            return existing.recordedTake && existing.startSample < takeEnd
                && existing.startSample + existing.durationSamples > clip.startSample;
        });
        if (hasRecordedOverlap)
            groupId = nextTakeGroupId++;
    }

    if (groupId != 0)
    {
        clip.takeGroupId = groupId;
        for (auto& existing : trackClips)
        {
            const auto overlaps = existing.startSample < takeEnd
                && existing.startSample + existing.durationSamples > clip.startSample;
            if (! overlaps || ! existing.recordedTake)
                continue;
            if (existing.takeGroupId == 0)
                existing.takeGroupId = groupId;
            if (existing.takeGroupId == groupId)
                existing.takeActive = false;
        }
    }

    trackClips.push_back(std::move(clip));
    if (groupId != 0)
    {
        auto folder = std::find_if(trackStates[static_cast<size_t>(trackIndex)].takeFolders.begin(),
                                   trackStates[static_cast<size_t>(trackIndex)].takeFolders.end(),
                                   [groupId] (const TakeFolderState& candidate) { return candidate.id.value == groupId; });
        if (folder == trackStates[static_cast<size_t>(trackIndex)].takeFolders.end())
        {
            TakeFolderState created;
            created.id = { groupId };
            created.trackId = trackStates[static_cast<size_t>(trackIndex)].id;
            created.startSample = trackClips.back().startSample;
            auto endSample = trackClips.back().startSample + trackClips.back().durationSamples;
            for (const auto& candidate : trackClips)
                if (candidate.takeGroupId == groupId)
                {
                    created.startSample = juce::jmin(created.startSample, candidate.startSample);
                    endSample = juce::jmax(endSample, candidate.startSample + candidate.durationSamples);
                    if (std::find(created.takeIds.begin(), created.takeIds.end(), candidate.takeId) == created.takeIds.end())
                        created.takeIds.push_back(candidate.takeId);
                }
            created.durationSamples = endSample - created.startSample;
            TakeCompState initialComp;
            initialComp.id = { nextCompId++ };
            initialComp.name = "Comp 1";
            initialComp.segments.push_back({ trackClips.back().startSample, trackClips.back().durationSamples,
                                             trackClips.back().takeId });
            created.activeCompId = initialComp.id;
            created.comps.push_back(std::move(initialComp));
            trackStates[static_cast<size_t>(trackIndex)].takeFolders.push_back(std::move(created));
        }
        else
        {
            const auto& newest = trackClips.back();
            if (std::find(folder->takeIds.begin(), folder->takeIds.end(), newest.takeId) == folder->takeIds.end())
                folder->takeIds.push_back(newest.takeId);
            const auto folderEnd = juce::jmax(folder->startSample + folder->durationSamples,
                                               newest.startSample + newest.durationSamples);
            folder->startSample = juce::jmin(folder->startSample, newest.startSample);
            folder->durationSamples = folderEnd - folder->startSample;
        }
    }
    publishAudioClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
    return trackClips.back().id;
}

ClipId TrackDataModel::addUnloadedClipToTrack(int trackIndex, const juce::File& file, double startSample,
                                              double durationSamples)
{
    if (trackIndex < 0 || durationSamples <= 0.0)
        return {};
    ensureTrackCount(static_cast<size_t>(trackIndex + 1));
    if (static_cast<size_t>(trackIndex) >= trackStates.size())
        return {};

    auto before = captureEditState();
    AudioClipState clip;
    clip.id = { nextClipId++ };
    clip.sourceFile = file;
    clip.startSample = std::max(0.0, startSample);
    clip.durationSamples = durationSamples;
    clip.trackId = trackStates[static_cast<size_t>(trackIndex)].id;
    clip.clipName = file.getFileNameWithoutExtension();
    clip.mediaStatus = AudioMediaStatus::Unloaded;
    trackStates[static_cast<size_t>(trackIndex)].clips.push_back(clip);
    publishAudioClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
    return clip.id;
}

TrackDataModel::RealtimeSnapshotRead TrackDataModel::acquireRealtimeSnapshot() const noexcept { return RealtimeSnapshotRead(*this); }

const TrackDataModel::FxRackSnapshot* TrackDataModel::getFxRackSnapshot(size_t trackIndex) const noexcept
{
    if (trackIndex >= trackStates.size()) return nullptr;
    const auto rackSlot = getFxRackSlot(trackStates[trackIndex].id);
    return rackSlot >= 0 ? publishedFxRacks[static_cast<size_t>(rackSlot)].load(std::memory_order_acquire) : nullptr;
}

TrackDataModel::EditTool TrackDataModel::getActiveTool() const noexcept { return activeTool.load(); }
void TrackDataModel::setActiveTool(EditTool tool) noexcept { activeTool.store(tool); sendChangeMessage(); }
float TrackDataModel::getHorizontalZoom() const noexcept { return horizontalZoom.load(); }
void TrackDataModel::setHorizontalZoom(float zoom) noexcept { horizontalZoom.store(std::clamp(zoom, 0.5f, 4096.0f)); sendChangeMessage(); }
int TrackDataModel::getTrackHeight() const noexcept { return trackHeight.load(); }
void TrackDataModel::setTrackHeight(int height) noexcept { trackHeight.store(std::clamp(height, 28, 160)); sendChangeMessage(); }
bool TrackDataModel::isCycleActive() const noexcept { return cycleActive.load(); }
double TrackDataModel::getCycleStartSample() const noexcept { return cycleStartSample.load(); }
double TrackDataModel::getCycleEndSample() const noexcept { return cycleEndSample.load(); }
void TrackDataModel::setCycle(double startSample, double endSample) noexcept
{
    cycleStartSample.store(std::max(0.0, std::min(startSample, endSample)));
    cycleEndSample.store(std::max(0.0, std::max(startSample, endSample)));
    cycleActive.store(endSample != startSample);
    markProjectModified();
    sendChangeMessage();
}
void TrackDataModel::clearCycle() noexcept
{
    if (cycleActive.exchange(false, std::memory_order_relaxed))
        markProjectModified();
    sendChangeMessage();
}

bool TrackDataModel::isPunchActive() const noexcept { return punchActive.load(std::memory_order_relaxed); }
double TrackDataModel::getPunchInSample() const noexcept { return punchInSample.load(std::memory_order_relaxed); }
double TrackDataModel::getPunchOutSample() const noexcept { return punchOutSample.load(std::memory_order_relaxed); }
void TrackDataModel::setPunchRange(double inSample, double outSample) noexcept
{
    const auto start = std::max(0.0, std::min(inSample, outSample));
    const auto end = std::max(0.0, std::max(inSample, outSample));
    punchInSample.store(start, std::memory_order_relaxed);
    punchOutSample.store(end, std::memory_order_relaxed);
    punchActive.store(end > start, std::memory_order_relaxed);
    markProjectModified();
    sendChangeMessage();
}

void TrackDataModel::clearPunchRange() noexcept
{
    if (punchActive.exchange(false, std::memory_order_relaxed))
        markProjectModified();
    sendChangeMessage();
}

void TrackDataModel::addMarker(double samplePosition, const juce::String& requestedName)
{
    if (! std::isfinite(samplePosition) || samplePosition < 0.0)
        return;
    auto before = captureEditState();
    const auto name = requestedName.isNotEmpty() ? requestedName
                                                  : "Song " + juce::String(markers.size() + 1);
    markers.push_back({ samplePosition, name });
    std::stable_sort(markers.begin(), markers.end(), [] (const Marker& left, const Marker& right)
    {
        return left.samplePosition < right.samplePosition;
    });
    commitEdit(std::move(before));
    sendChangeMessage();
}

void TrackDataModel::splitAudioClip(size_t trackIndex, size_t clipIndex, double splitSample)
{
    if (trackIndex >= trackStates.size() || clipIndex >= trackStates[trackIndex].clips.size()) return;
    auto before = captureEditState();
    auto& clips = trackStates[trackIndex].clips;
    auto second = clips[clipIndex];
    const auto split = std::clamp(splitSample, second.startSample + 1.0, second.startSample + second.durationSamples - 1.0);
    second.id = { nextClipId++ };
    second.startSample = split;
    second.sourceOffsetSamples += split - clips[clipIndex].startSample;
    second.durationSamples -= split - clips[clipIndex].startSample;
    clips[clipIndex].durationSamples = split - clips[clipIndex].startSample;
    clips.insert(clips.begin() + static_cast<std::ptrdiff_t>(clipIndex + 1), second);
    publishAudioClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
}

void TrackDataModel::trimAudioClip(size_t trackIndex, size_t clipIndex, double startSample, double durationSamples)
{
    if (trackIndex >= trackStates.size() || clipIndex >= trackStates[trackIndex].clips.size()) return;
    auto& clip = trackStates[trackIndex].clips[clipIndex];
    const auto end = clip.startSample + clip.durationSamples;
    // The left edge may be restored up to the source's original in-point, but
    // never beyond it: a negative source offset has no audio to reveal.
    const auto earliestStart = juce::jmax(0.0, clip.startSample - clip.sourceOffsetSamples);
    const auto newStart = std::clamp(startSample, earliestStart, end - 1.0);
    const auto newSourceOffset = clip.sourceOffsetSamples + newStart - clip.startSample;
    auto newEnd = juce::jmax(newStart + 1.0, newStart + durationSamples);
    // When decoded source media is available, a normal region cannot expose
    // samples beyond its source end.  Missing/unloaded media retains its
    // existing non-destructive timeline extent until it can be resolved.
    if (! clip.loopEnabled && clip.cachedBuffer != nullptr && clip.cachedBuffer->getNumSamples() > 0)
    {
        const auto sourceEnd = newStart + juce::jmax(1.0,
            static_cast<double>(clip.cachedBuffer->getNumSamples()) - newSourceOffset);
        newEnd = juce::jmin(newEnd, sourceEnd);
    }
    const auto newDuration = newEnd - newStart;
    const auto newFadeIn = juce::jmin(clip.fadeInSamples, newDuration);
    const auto newFadeOut = juce::jmin(clip.fadeOutSamples, newDuration);
    if (clip.sourceOffsetSamples == newSourceOffset && clip.startSample == newStart
        && clip.durationSamples == newDuration && clip.fadeInSamples == newFadeIn
        && clip.fadeOutSamples == newFadeOut)
        return;

    auto before = captureEditState();
    clip.sourceOffsetSamples = newSourceOffset;
    clip.startSample = newStart;
    clip.durationSamples = newDuration;
    clip.fadeInSamples = newFadeIn;
    clip.fadeOutSamples = newFadeOut;
    publishAudioClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
}

void TrackDataModel::deleteAudioClip(size_t trackIndex, size_t clipIndex)
{
    if (trackIndex >= trackStates.size() || clipIndex >= trackStates[trackIndex].clips.size()) return;
    auto before = captureEditState();
    trackStates[trackIndex].clips.erase(trackStates[trackIndex].clips.begin() + static_cast<std::ptrdiff_t>(clipIndex));
    publishAudioClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
}

bool TrackDataModel::moveAudioClip(ClipId clipId, TrackId destinationTrack, double startSample)
{
    const auto destinationIndex = getTrackIndex(destinationTrack);
    if (destinationIndex < 0) return false;
    const auto targetStart = std::max(0.0, startSample);
    for (size_t sourceIndex = 0; sourceIndex < trackStates.size(); ++sourceIndex)
    {
        auto& track = trackStates[sourceIndex];
        const auto iter = std::find_if(track.clips.begin(), track.clips.end(), [clipId] (const AudioClipState& clip) { return clip.id == clipId; });
        if (iter == track.clips.end()) continue;

        if (track.id == destinationTrack && iter->startSample == targetStart)
            return true;

        auto before = captureEditState();
        if (track.id == destinationTrack)
            iter->startSample = targetStart;
        else
        {
            auto moved = *iter;
            track.clips.erase(iter);
            moved.trackId = destinationTrack;
            moved.startSample = targetStart;
            trackStates[static_cast<size_t>(destinationIndex)].clips.push_back(std::move(moved));
        }
        publishAudioClipSnapshot();
        publishRenderStructureSnapshot();
        commitEdit(std::move(before));
        sendChangeMessage();
        return true;
    }
    return false;
}

ClipId TrackDataModel::duplicateAudioClip(ClipId clipId, TrackId destinationTrack, double startSample)
{
    const auto destinationIndex = getTrackIndex(destinationTrack);
    if (destinationIndex < 0) return {};
    for (const auto& track : trackStates)
        for (const auto& clip : track.clips)
            if (clip.id == clipId)
            {
                auto before = captureEditState();
                auto copy = clip;
                copy.id = { nextClipId++ };
                copy.trackId = destinationTrack;
                copy.startSample = std::max(0.0, startSample);
                trackStates[static_cast<size_t>(destinationIndex)].clips.push_back(std::move(copy));
                publishAudioClipSnapshot();
                publishRenderStructureSnapshot();
                commitEdit(std::move(before));
                sendChangeMessage();
                return trackStates[static_cast<size_t>(destinationIndex)].clips.back().id;
            }
    return {};
}

bool TrackDataModel::trimAudioClip(ClipId clipId, double startSample, double durationSamples)
{
    for (size_t trackIndex = 0; trackIndex < trackStates.size(); ++trackIndex)
        for (size_t clipIndex = 0; clipIndex < trackStates[trackIndex].clips.size(); ++clipIndex)
            if (trackStates[trackIndex].clips[clipIndex].id == clipId)
            {
                trimAudioClip(trackIndex, clipIndex, startSample, durationSamples);
                return true;
            }
    return false;
}

bool TrackDataModel::deleteAudioClip(ClipId clipId)
{
    for (size_t trackIndex = 0; trackIndex < trackStates.size(); ++trackIndex)
        for (size_t clipIndex = 0; clipIndex < trackStates[trackIndex].clips.size(); ++clipIndex)
            if (trackStates[trackIndex].clips[clipIndex].id == clipId)
            {
                deleteAudioClip(trackIndex, clipIndex);
                return true;
            }
    return false;
}

bool TrackDataModel::deleteAudioRange(double startSample, double endSample, int firstTrack, int lastTrack)
{
    if (! std::isfinite(startSample) || ! std::isfinite(endSample) || endSample <= startSample
        || trackStates.empty())
        return false;

    firstTrack = juce::jlimit(0, static_cast<int>(trackStates.size()) - 1, firstTrack);
    lastTrack = juce::jlimit(firstTrack, static_cast<int>(trackStates.size()) - 1, lastTrack);
    auto before = captureEditState();
    auto changed = false;

    for (auto trackIndex = firstTrack; trackIndex <= lastTrack; ++trackIndex)
    {
        auto& clips = trackStates[static_cast<size_t>(trackIndex)].clips;
        for (size_t index = 0; index < clips.size();)
        {
            auto& clip = clips[index];
            const auto clipStart = clip.startSample;
            const auto clipEnd = clip.startSample + clip.durationSamples;
            if (clipEnd <= startSample || clipStart >= endSample)
            {
                ++index;
                continue;
            }

            changed = true;
            if (startSample <= clipStart && endSample >= clipEnd)
            {
                clips.erase(clips.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }
            if (startSample > clipStart && endSample < clipEnd)
            {
                auto right = clip;
                right.id = { nextClipId++ };
                right.startSample = endSample;
                right.sourceOffsetSamples += endSample - clipStart;
                right.durationSamples = clipEnd - endSample;
                right.fadeInSamples = 0.0;
                clip.durationSamples = startSample - clipStart;
                clip.fadeOutSamples = 0.0;
                clips.insert(clips.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(right));
                index += 2;
                continue;
            }
            if (startSample <= clipStart)
            {
                const auto removed = endSample - clipStart;
                clip.startSample = endSample;
                clip.sourceOffsetSamples += removed;
                clip.durationSamples = clipEnd - endSample;
                clip.fadeInSamples = 0.0;
            }
            else
            {
                clip.durationSamples = startSample - clipStart;
                clip.fadeOutSamples = 0.0;
            }
            ++index;
        }
    }

    if (! changed)
        return false;
    publishAudioClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
    return true;
}

bool TrackDataModel::setClipGain(ClipId clipId, float value)
{
    for (auto& track : trackStates)
        for (auto& clip : track.clips)
            if (clip.id == clipId)
            {
                const auto gain = juce::jlimit(0.0f, 4.0f, value);
                if (clip.gain == gain) return true;
                auto before = captureEditState();
                clip.gain = gain;
                publishAudioClipSnapshot();
                publishRenderStructureSnapshot();
                commitEdit(std::move(before));
                sendChangeMessage();
                return true;
            }
    return false;
}

bool TrackDataModel::setClipFades(ClipId clipId, double fadeIn, double fadeOut)
{
    for (auto& track : trackStates)
        for (auto& clip : track.clips)
            if (clip.id == clipId)
            {
                const auto in = juce::jlimit(0.0, clip.durationSamples, fadeIn);
                const auto out = juce::jlimit(0.0, clip.durationSamples, fadeOut);
                if (clip.fadeInSamples == in && clip.fadeOutSamples == out) return true;
                auto before = captureEditState();
                clip.fadeInSamples = in;
                clip.fadeOutSamples = out;
                publishAudioClipSnapshot();
                publishRenderStructureSnapshot();
                commitEdit(std::move(before));
                sendChangeMessage();
                return true;
            }
    return false;
}

bool TrackDataModel::setClipLoopEnabled(ClipId clipId, bool enabled)
{
    for (auto& track : trackStates)
        for (auto& clip : track.clips)
            if (clip.id == clipId)
            {
                if (clip.loopEnabled == enabled)
                    return true;
                auto before = captureEditState();
                clip.loopEnabled = enabled;
                // A DAW loop repeats the currently visible region, not the
                // remainder of its source file. This matters after trimming:
                // a two-bar edit must repeat as two bars, even if its source
                // file is five minutes long.
                if (enabled)
                    clip.loopLengthSamples = juce::jmax(1.0, clip.durationSamples);
                publishAudioClipSnapshot();
                publishRenderStructureSnapshot();
                commitEdit(std::move(before));
                sendChangeMessage();
                return true;
            }
    return false;
}

bool TrackDataModel::selectActiveTake(ClipId clipId)
{
    for (auto& track : trackStates)
    {
        const auto selected = std::find_if(track.clips.begin(), track.clips.end(), [clipId] (const AudioClipState& clip)
        {
            return clip.id == clipId;
        });
        if (selected == track.clips.end() || selected->takeGroupId == 0)
            continue;

        const auto groupId = selected->takeGroupId;
        const auto takeId = selected->takeId != 0 ? selected->takeId : selected->id.value;
        auto before = captureEditState();
        for (auto& clip : track.clips)
            if (clip.takeGroupId == groupId)
                clip.takeActive = clip.takeId == takeId;
        const auto folder = std::find_if(track.takeFolders.begin(), track.takeFolders.end(), [groupId] (const TakeFolderState& candidate)
        {
            return candidate.id.value == groupId;
        });
        if (folder != track.takeFolders.end())
            if (const auto comp = std::find_if(folder->comps.begin(), folder->comps.end(), [&] (const TakeCompState& candidate)
                { return candidate.id == folder->activeCompId; }); comp != folder->comps.end())
            {
                comp->segments.clear();
                comp->segments.push_back({ selected->startSample, selected->durationSamples, takeId });
            }
        publishAudioClipSnapshot();
        commitEdit(std::move(before));
        sendChangeMessage();
        return true;
    }
    return false;
}

const std::vector<TakeFolderState>& TrackDataModel::getTakeFolders(size_t trackIndex) const noexcept
{
    static const std::vector<TakeFolderState> empty;
    return trackIndex < trackStates.size() ? trackStates[trackIndex].takeFolders : empty;
}

bool TrackDataModel::selectTakeFolderRange(TakeFolderId folderId, uint64_t takeId,
                                           double startSample, double endSample)
{
    if (! folderId.isValid() || takeId == 0 || ! std::isfinite(startSample) || ! std::isfinite(endSample))
        return false;
    for (auto& track : trackStates)
    {
        const auto folder = std::find_if(track.takeFolders.begin(), track.takeFolders.end(), [folderId] (const TakeFolderState& candidate)
        {
            return candidate.id == folderId;
        });
        if (folder == track.takeFolders.end())
            continue;
        if (std::find(folder->takeIds.begin(), folder->takeIds.end(), takeId) == folder->takeIds.end())
            return false;
        const auto folderStart = folder->startSample;
        const auto folderEnd = folder->startSample + folder->durationSamples;
        const auto selectionStart = juce::jlimit(folderStart, folderEnd, juce::jmin(startSample, endSample));
        const auto selectionEnd = juce::jlimit(folderStart, folderEnd, juce::jmax(startSample, endSample));
        if (selectionEnd <= selectionStart)
            return false;
        const auto activeComp = std::find_if(folder->comps.begin(), folder->comps.end(), [&] (const TakeCompState& candidate)
        {
            return candidate.id == folder->activeCompId;
        });
        if (activeComp == folder->comps.end())
            return false;

        auto before = captureEditState();
        std::vector<CompSegmentState> next;
        next.reserve(activeComp->segments.size() + 2);
        for (const auto& segment : activeComp->segments)
        {
            const auto segmentEnd = segment.startSample + segment.durationSamples;
            if (segmentEnd <= selectionStart || segment.startSample >= selectionEnd)
            {
                next.push_back(segment);
                continue;
            }
            if (segment.startSample < selectionStart)
            {
                auto left = segment;
                left.durationSamples = selectionStart - segment.startSample;
                next.push_back(left);
            }
            if (segmentEnd > selectionEnd)
            {
                auto right = segment;
                right.startSample = selectionEnd;
                right.durationSamples = segmentEnd - selectionEnd;
                next.push_back(right);
            }
        }
        next.push_back({ selectionStart, selectionEnd - selectionStart, takeId });
        std::stable_sort(next.begin(), next.end(), [] (const CompSegmentState& lhs, const CompSegmentState& rhs)
        {
            return lhs.startSample < rhs.startSample;
        });
        // Segment bounds stay contiguous here. Crossfade overlap is applied
        // by the comp editor, which expands adjacent bounds together; adding
        // fades without overlap would create an audible gain dip.
        for (auto& segment : next)
            segment.fadeInSamples = segment.fadeOutSamples = 0.0;
        activeComp->segments = std::move(next);
        publishAudioClipSnapshot();
        commitEdit(std::move(before));
        sendChangeMessage();
        return true;
    }
    return false;
}

bool TrackDataModel::createCrossfadesForTrack(TrackId trackId)
{
    const auto trackIndex = getTrackIndex(trackId);
    if (trackIndex < 0)
        return false;

    auto& clips = trackStates[static_cast<size_t>(trackIndex)].clips;
    if (clips.size() < 2)
        return false;

    std::vector<size_t> orderedClipIndices(clips.size());
    std::iota(orderedClipIndices.begin(), orderedClipIndices.end(), 0);
    std::stable_sort(orderedClipIndices.begin(), orderedClipIndices.end(), [&clips] (size_t lhs, size_t rhs)
    {
        return clips[lhs].startSample < clips[rhs].startSample;
    });

    auto before = captureEditState();
    bool changed = false;
    for (size_t position = 1; position < orderedClipIndices.size(); ++position)
    {
        auto& left = clips[orderedClipIndices[position - 1]];
        auto& right = clips[orderedClipIndices[position]];
        const auto overlap = juce::jmin(left.startSample + left.durationSamples - right.startSample,
                                        left.durationSamples, right.durationSamples);
        if (overlap <= 0.0)
            continue;

        const auto fadeOut = juce::jmax(left.fadeOutSamples, overlap);
        const auto fadeIn = juce::jmax(right.fadeInSamples, overlap);
        changed = changed || fadeOut != left.fadeOutSamples || fadeIn != right.fadeInSamples;
        left.fadeOutSamples = fadeOut;
        right.fadeInSamples = fadeIn;
    }

    if (! changed)
        return false;

    publishAudioClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
    return true;
}

bool TrackDataModel::setClipMediaResource(ClipId clipId, const juce::File& file,
                                          std::shared_ptr<juce::AudioBuffer<float>> decodedBuffer)
{
    if (decodedBuffer == nullptr || decodedBuffer->getNumChannels() <= 0 || decodedBuffer->getNumSamples() <= 0)
        return false;
    for (auto& track : trackStates)
        for (auto& clip : track.clips)
            if (clip.id == clipId)
            {
                const auto& source = audioMediaPool.registerDecodedSource(file, decodedBuffer);
                const auto playbackData = source.playbackData.lock();
                if (playbackData == nullptr)
                    return false;
                clip.sourceId = source.id;
                clip.sourceFile = source.sourceFile;
                clip.cachedBuffer = playbackData;
                clip.mediaStatus = AudioMediaStatus::Ready;
                publishAudioClipSnapshot();
                publishRenderStructureSnapshot();
                sendChangeMessage();
                return true;
            }
    return false;
}

size_t TrackDataModel::applyClipMediaUpdates(const std::vector<ClipMediaUpdate>& updates)
{
    auto applied = size_t { 0 };
    auto changed = false;
    for (const auto& update : updates)
    {
        if (! update.clipId.isValid())
            continue;

        for (auto& track : trackStates)
        {
            const auto found = std::find_if(track.clips.begin(), track.clips.end(), [&] (const AudioClipState& clip)
            {
                return clip.id == update.clipId;
            });
            if (found == track.clips.end())
                continue;

            auto& clip = *found;
            if (update.status == AudioMediaStatus::Ready)
            {
                if (update.decodedBuffer == nullptr || update.decodedBuffer->getNumChannels() <= 0
                    || update.decodedBuffer->getNumSamples() <= 0)
                    break;

                const auto& source = audioMediaPool.registerDecodedSource(update.sourceFile, update.decodedBuffer);
                const auto playbackData = source.playbackData.lock();
                if (playbackData == nullptr)
                    break;
                changed = changed || clip.sourceId != source.id || clip.sourceFile != source.sourceFile
                    || clip.cachedBuffer != playbackData || clip.mediaStatus != AudioMediaStatus::Ready;
                clip.sourceId = source.id;
                clip.sourceFile = source.sourceFile;
                clip.cachedBuffer = playbackData;
                clip.mediaStatus = AudioMediaStatus::Ready;
            }
            else
            {
                changed = changed || clip.cachedBuffer != nullptr || clip.mediaStatus != update.status;
                clip.cachedBuffer.reset();
                clip.mediaStatus = update.status;
            }
            ++applied;
            break;
        }
    }

    if (changed)
    {
        publishAudioClipSnapshot();
        publishRenderStructureSnapshot();
        sendChangeMessage();
    }
    return applied;
}

std::shared_ptr<juce::AudioBuffer<float>> TrackDataModel::findDecodedAudioSource(const juce::File& sourceFile) const
{
    return audioMediaPool.findPlaybackData(sourceFile);
}

bool TrackDataModel::setClipMediaStatus(ClipId clipId, AudioMediaStatus status)
{
    for (auto& track : trackStates)
        for (auto& clip : track.clips)
            if (clip.id == clipId)
            {
                clip.cachedBuffer.reset();
                clip.mediaStatus = status;
                publishAudioClipSnapshot();
                publishRenderStructureSnapshot();
                sendChangeMessage();
                return true;
            }
    return false;
}

AudioMediaStatus TrackDataModel::getClipMediaStatus(ClipId clipId) const noexcept
{
    for (const auto& track : trackStates)
        for (const auto& clip : track.clips)
            if (clip.id == clipId) return clip.mediaStatus;
    return AudioMediaStatus::Unloaded;
}

bool TrackDataModel::canUndo() const noexcept { return ! undoHistory.empty(); }
bool TrackDataModel::canRedo() const noexcept { return ! redoHistory.empty(); }

TrackDataModel::EditState TrackDataModel::captureEditState() const
{
    EditState state;
    state.tracks = trackStates;
    state.buses = busStates;
    state.midiClips = midiClips;
    state.tempo = tempoMap;
    state.timeSignatures = timeSignatureMap;
    state.markers = markers;
    state.bpm = bpm.load(std::memory_order_relaxed);
    state.timeSignatureNumerator = timeSignatureNumerator.load(std::memory_order_relaxed);
    state.timeSignatureDenominator = timeSignatureDenominator.load(std::memory_order_relaxed);
    state.cycleActive = cycleActive.load(std::memory_order_relaxed);
    state.cycleStartSample = cycleStartSample.load(std::memory_order_relaxed);
    state.cycleEndSample = cycleEndSample.load(std::memory_order_relaxed);
    state.punchActive = punchActive.load(std::memory_order_relaxed);
    state.punchInSample = punchInSample.load(std::memory_order_relaxed);
    state.punchOutSample = punchOutSample.load(std::memory_order_relaxed);
    state.fxRackTrackIds = fxRackTrackIds;
    for (size_t slot = 0; slot < maxTracks; ++slot)
        if (fxRacks[slot] != nullptr) state.fxRacks[slot] = *fxRacks[slot];
    return state;
}

void TrackDataModel::commitEdit(EditState before)
{
    if (undoHistory.size() == maxUndoEntries) undoHistory.erase(undoHistory.begin());
    undoHistory.push_back(std::move(before));
    redoHistory.clear();
    markProjectModified();
}

void TrackDataModel::restoreEditState(EditState state)
{
    trackStates = std::move(state.tracks);
    busStates = std::move(state.buses);
    midiClips = std::move(state.midiClips);
    tempoMap = std::move(state.tempo);
    timeSignatureMap = std::move(state.timeSignatures);
    markers = std::move(state.markers);
    bpm.store(state.bpm, std::memory_order_relaxed);
    timeSignatureNumerator.store(state.timeSignatureNumerator, std::memory_order_relaxed);
    timeSignatureDenominator.store(state.timeSignatureDenominator, std::memory_order_relaxed);
    cycleActive.store(state.cycleActive, std::memory_order_relaxed);
    cycleStartSample.store(state.cycleStartSample, std::memory_order_relaxed);
    cycleEndSample.store(state.cycleEndSample, std::memory_order_relaxed);
    punchActive.store(state.punchActive, std::memory_order_relaxed);
    punchInSample.store(state.punchInSample, std::memory_order_relaxed);
    punchOutSample.store(state.punchOutSample, std::memory_order_relaxed);
    fxRackTrackIds = state.fxRackTrackIds;
    for (size_t slot = 0; slot < maxTracks; ++slot)
        publishFxRackSnapshot(slot, std::make_unique<FxRackSnapshot>(std::move(state.fxRacks[slot])));
    publishAudioClipSnapshot();
    publishMidiClipSnapshot();
    publishRenderStructureSnapshot();
    markProjectModified();
    sendChangeMessage();
}

bool TrackDataModel::undo()
{
    if (undoHistory.empty()) return false;
    auto current = captureEditState();
    auto previous = std::move(undoHistory.back());
    undoHistory.pop_back();
    redoHistory.push_back(std::move(current));
    restoreEditState(std::move(previous));
    return true;
}

bool TrackDataModel::redo()
{
    if (redoHistory.empty()) return false;
    auto current = captureEditState();
    auto next = std::move(redoHistory.back());
    redoHistory.pop_back();
    undoHistory.push_back(std::move(current));
    restoreEditState(std::move(next));
    return true;
}

void TrackDataModel::clearUndoHistory()
{
    undoHistory.clear();
    redoHistory.clear();
}

ProjectState TrackDataModel::createProjectState() const
{
    ProjectState state;
    state.controlRoutingProfile = controlRoutingProfile;
    state.liveKeyReferenceTrack = liveKeyReferenceTrack;
    state.bpm = getBpm();
    state.timeSignatureNumerator = getTimeSignatureNumerator();
    state.timeSignatureDenominator = getTimeSignatureDenominator();
    state.cycleActive = isCycleActive();
    state.cycleStartSample = getCycleStartSample();
    state.cycleEndSample = getCycleEndSample();
    state.punchActive = isPunchActive();
    state.punchInSample = getPunchInSample();
    state.punchOutSample = getPunchOutSample();
    state.tempoMap.reserve(tempoMap.size());
    for (const auto& event : tempoMap) state.tempoMap.push_back({ event.samplePosition, event.bpm });
    state.timeSignatureMap.reserve(timeSignatureMap.size());
    for (const auto& event : timeSignatureMap)
        state.timeSignatureMap.push_back({ event.samplePosition, event.numerator, event.denominator });
    for (const auto& marker : markers) state.markers.push_back({ marker.samplePosition, marker.name });
    state.buses.reserve(busStates.size());
    for (size_t busIndex = 0; busIndex < busStates.size(); ++busIndex)
    {
        const auto& bus = busStates[busIndex];
        PersistedBusState savedBus { bus.id, bus.name, bus.gain, bus.muted };
        savedBus.balance = bus.balance;
        if (const auto* rack = publishedBusFxRacks[busIndex].load(std::memory_order_acquire); rack != nullptr)
            for (size_t slot = 0; slot < maxFxSlots; ++slot)
                if (const auto& processor = rack->processors[slot]; processor != nullptr)
                {
                    savedBus.fxSlots[slot].persistentIdentifier = processor->getPersistentIdentifier();
                    savedBus.fxSlots[slot].bypassed = rack->bypass[slot];
                    processor->getState(savedBus.fxSlots[slot].state);
                }
        state.buses.push_back(std::move(savedBus));
    }
    state.tracks.reserve(trackStates.size());
    for (size_t trackIndex = 0; trackIndex < trackStates.size(); ++trackIndex)
    {
        const auto& track = trackStates[trackIndex];
        PersistedTrackState savedTrack;
        savedTrack.id = track.id;
        savedTrack.type = track.type;
        savedTrack.name = track.name;
        savedTrack.volume = track.volume.load(std::memory_order_relaxed);
        savedTrack.pan = track.pan.load(std::memory_order_relaxed);
        savedTrack.muted = track.muted.load(std::memory_order_relaxed);
        savedTrack.solo = track.solo.load(std::memory_order_relaxed);
        savedTrack.soloSafe = track.soloSafe.load(std::memory_order_relaxed);
        savedTrack.inputMonitoring = track.inputMonitoring.load(std::memory_order_relaxed);
        savedTrack.autoInputMonitoring = track.autoInputMonitoring.load(std::memory_order_relaxed);
        savedTrack.monitorDestination = track.monitorDestination.load(std::memory_order_relaxed);
        savedTrack.inputChannel = track.inputChannel.load(std::memory_order_relaxed);
        savedTrack.inputChannelCount = track.inputChannelCount.load(std::memory_order_relaxed);
        savedTrack.midiInputChannel = track.midiInputChannel.load(std::memory_order_relaxed);
        savedTrack.midiInputDeviceIdentifier = track.midiInputDeviceIdentifier;
        savedTrack.outputBus = track.outputBus;
        savedTrack.activeSendCount = track.activeSendCount;
        for (size_t i = 0; i < maxSendsPerTrack; ++i)
            savedTrack.sends[i] = { track.sends[i].targetBus, track.sends[i].level, track.sends[i].preFader, track.sends[i].postFaderBeforePan };
        if (const auto* rack = getFxRackSnapshot(trackIndex); rack != nullptr)
            for (size_t slot = 0; slot < maxFxSlots; ++slot)
                if (const auto& processor = rack->processors[slot]; processor != nullptr)
                {
                    savedTrack.fxSlots[slot].persistentIdentifier = processor->getPersistentIdentifier();
                    savedTrack.fxSlots[slot].bypassed = rack->bypass[slot];
                    processor->getState(savedTrack.fxSlots[slot].state);
                }
        const auto saveAutomation = [&savedTrack] (const AutomationLane& lane)
        {
            PersistedAutomationLane savedLane;
            savedLane.parameter = static_cast<int>(lane.getParameter());
            savedLane.mode = static_cast<int>(lane.getMode());
            savedLane.defaultValue = lane.getDefaultValue();
            savedLane.points.reserve(lane.getPoints().size());
            for (const auto& point : lane.getPoints())
                savedLane.points.push_back({ point.samplePosition, point.value });
            savedTrack.automationLanes.push_back(std::move(savedLane));
        };
        saveAutomation(track.volumeAutomation);
        saveAutomation(track.panAutomation);
        for (size_t slot = 0; slot < maxSendsPerTrack; ++slot)
            if (const auto& lane = track.sendAutomation[slot]; lane.getMode() != AutomationMode::read || ! lane.getPoints().empty())
            {
                PersistedAutomationLane savedLane;
                savedLane.parameter = static_cast<int>(AutomationParameter::sendLevel);
                savedLane.sendSlot = static_cast<int>(slot);
                savedLane.mode = static_cast<int>(lane.getMode());
                savedLane.defaultValue = lane.getDefaultValue();
                savedLane.points.reserve(lane.getPoints().size());
                for (const auto& point : lane.getPoints())
                    savedLane.points.push_back({ point.samplePosition, point.value });
                savedTrack.automationLanes.push_back(std::move(savedLane));
            }
        savedTrack.clips.reserve(track.clips.size());
        for (const auto& clip : track.clips)
        {
            PersistedClipState savedClip;
            savedClip.id = clip.id;
            savedClip.trackId = clip.trackId;
            savedClip.sourcePath = clip.sourceFile.getFullPathName();
            savedClip.clipName = clip.clipName;
            savedClip.startSample = clip.startSample;
            savedClip.durationSamples = clip.durationSamples;
            savedClip.sourceOffsetSamples = clip.sourceOffsetSamples;
            savedClip.loopEnabled = clip.loopEnabled;
            savedClip.loopLengthSamples = clip.loopLengthSamples;
            savedClip.gain = clip.gain;
            savedClip.fadeInSamples = clip.fadeInSamples;
            savedClip.fadeOutSamples = clip.fadeOutSamples;
            savedClip.takeGroupId = clip.takeGroupId;
            savedClip.takeId = clip.takeId;
            savedClip.takeActive = clip.takeActive;
            savedClip.recordedTake = clip.recordedTake;
            savedTrack.clips.push_back(std::move(savedClip));
        }
        savedTrack.takeFolders.reserve(track.takeFolders.size());
        for (const auto& folder : track.takeFolders)
            savedTrack.takeFolders.push_back({ folder });
        state.tracks.push_back(std::move(savedTrack));
    }
    state.midiClips.reserve(midiClips.size());
    for (const auto& clip : midiClips)
    {
        PersistedMidiClipState savedClip;
        savedClip.id = clip.id;
        savedClip.trackId = clip.trackId;
        savedClip.startSample = clip.startSample;
        savedClip.contentOffsetSamples = clip.contentOffsetSamples;
        savedClip.durationSamples = clip.durationSamples;
        savedClip.notes.reserve(clip.notes.size());
        for (const auto& note : clip.notes)
            savedClip.notes.push_back({ note.id, note.pitch, note.velocity, note.startSample,
                                        note.durationSamples, note.channel });
        state.midiClips.push_back(std::move(savedClip));
    }
    return state;
}

juce::Result TrackDataModel::applyProjectState(const ProjectState& state)
{
    if (const auto profileResult = state.controlRoutingProfile.validate(); profileResult.failed())
        return juce::Result::fail("Project control routing is invalid: " + profileResult.getErrorMessage());
    if (state.tracks.size() > maxTracks)
        return juce::Result::fail("Project exceeds the current " + juce::String(maxTracks) + "-track engine limit");
    if (! std::isfinite(state.bpm) || state.bpm < 40.0 || state.bpm > 220.0)
        return juce::Result::fail("Project BPM is invalid");
    if (state.timeSignatureNumerator < 1 || state.timeSignatureNumerator > 32)
        return juce::Result::fail("Project time signature is invalid");
    if (state.timeSignatureDenominator != 1 && state.timeSignatureDenominator != 2
        && state.timeSignatureDenominator != 4 && state.timeSignatureDenominator != 8
        && state.timeSignatureDenominator != 16)
        return juce::Result::fail("Project time signature denominator is invalid");
    if (state.tempoMap.empty()) return juce::Result::fail("Project tempo map is empty");
    for (const auto& marker : state.markers)
        if (! std::isfinite(marker.samplePosition) || marker.samplePosition < 0.0 || marker.name.isEmpty())
            return juce::Result::fail("Project marker state is invalid");
    if (! std::isfinite(state.cycleStartSample) || ! std::isfinite(state.cycleEndSample)
        || state.cycleStartSample < 0.0 || state.cycleEndSample < state.cycleStartSample
        || (state.cycleActive && state.cycleEndSample <= state.cycleStartSample))
        return juce::Result::fail("Project cycle range is invalid");
    if (! std::isfinite(state.punchInSample) || ! std::isfinite(state.punchOutSample)
        || state.punchInSample < 0.0 || state.punchOutSample < state.punchInSample
        || (state.punchActive && state.punchOutSample <= state.punchInSample))
        return juce::Result::fail("Project punch range is invalid");
    if (state.buses.size() > maxBuses)
        return juce::Result::fail("Project exceeds the current " + juce::String(maxBuses) + "-bus engine limit");

    uint64_t largestTrackId = 0;
    uint64_t largestBusId = 0;
    uint64_t largestClipId = 0;
    uint64_t largestTakeGroupId = 0;
    uint64_t largestCompId = 0;
    uint64_t largestMidiClipId = 0;
    uint64_t largestMidiEventId = 0;
    std::vector<TrackId> trackIds;
    std::vector<BusId> busIds;
    std::vector<ClipId> clipIds;
    std::vector<MidiClipId> midiClipIds;
    std::vector<MidiEventId> midiEventIds;
    trackIds.reserve(state.tracks.size());
    busIds.reserve(state.buses.size());
    for (const auto& bus : state.buses)
    {
        if (! bus.id.isValid() || std::find(busIds.begin(), busIds.end(), bus.id) != busIds.end()
            || ! std::isfinite(bus.gain) || bus.gain < 0.0f || bus.gain > AudioGainRange::maximumChannelGain
            || ! std::isfinite(bus.balance) || bus.balance < -1.0f || bus.balance > 1.0f)
            return juce::Result::fail("Project bus state is invalid");
        busIds.push_back(bus.id);
        largestBusId = std::max(largestBusId, bus.id.value);
    }
    for (const auto& track : state.tracks)
    {
        if (! track.id.isValid() || std::find(trackIds.begin(), trackIds.end(), track.id) != trackIds.end()
            || (track.type != TrackType::audio && track.type != TrackType::instrument && track.type != TrackType::externalMidi))
            return juce::Result::fail("Project has duplicate or invalid track IDs");
        if (! std::isfinite(track.volume) || track.volume < 0.0f || track.volume > AudioGainRange::maximumChannelGain
            || ! std::isfinite(track.pan) || track.pan < -1.0f || track.pan > 1.0f)
            return juce::Result::fail("Project mixer values are invalid");
        if (track.inputChannel < 0 || track.inputChannel > 255
            || track.inputChannelCount < 1 || track.inputChannelCount > 2)
            return juce::Result::fail("Project input routing is invalid");
        if (track.midiInputChannel < 0 || track.midiInputChannel > 16)
            return juce::Result::fail("Project MIDI input channel is invalid");
        if ((track.outputBus.isValid() && std::find(busIds.begin(), busIds.end(), track.outputBus) == busIds.end())
            || track.activeSendCount > maxSendsPerTrack)
            return juce::Result::fail("Project track bus routing is invalid");
        const auto activeRoutes = static_cast<uint8_t>(std::count_if(track.sends.begin(), track.sends.end(),
            [] (const PersistedTrackState::SendRoute& send) { return send.targetBus.isValid(); }));
        if (activeRoutes != track.activeSendCount)
            return juce::Result::fail("Project track send count is invalid");
        for (const auto& send : track.sends)
            if (send.targetBus.isValid()
                && (std::find(busIds.begin(), busIds.end(), send.targetBus) == busIds.end()
                    || ! std::isfinite(send.level) || send.level < 0.0f || send.level > maximumSendGain))
                return juce::Result::fail("Project track send routing is invalid");
        if (track.automationLanes.size() > 2 + maxSendsPerTrack)
            return juce::Result::fail("Project has too many automation lanes per track");
        std::array<bool, 2> seenTrackAutomation {};
        std::array<bool, maxSendsPerTrack> seenSendAutomation {};
        for (const auto& lane : track.automationLanes)
        {
            if (lane.parameter < static_cast<int>(AutomationParameter::trackVolume)
                || lane.parameter > static_cast<int>(AutomationParameter::sendLevel)
                || lane.mode < static_cast<int>(AutomationMode::read) || lane.mode > static_cast<int>(AutomationMode::write)
                || lane.points.size() > AutomationLane::maximumPoints)
                return juce::Result::fail("Project automation lane is invalid");
            const auto parameter = static_cast<AutomationParameter>(lane.parameter);
            if (! AutomationLane::isValueValid(parameter, lane.defaultValue))
                return juce::Result::fail("Project automation lane is invalid");
            if (parameter == AutomationParameter::sendLevel)
            {
                if (lane.sendSlot < 0 || lane.sendSlot >= static_cast<int>(maxSendsPerTrack)
                    || seenSendAutomation[static_cast<size_t>(lane.sendSlot)])
                    return juce::Result::fail("Project send automation lane is invalid");
                seenSendAutomation[static_cast<size_t>(lane.sendSlot)] = true;
            }
            else if (lane.sendSlot != -1 || seenTrackAutomation[static_cast<size_t>(parameter)])
                return juce::Result::fail("Project automation lane is invalid");
            else
                seenTrackAutomation[static_cast<size_t>(parameter)] = true;
            double previousPosition = -1.0;
            for (const auto& point : lane.points)
            {
                if (! std::isfinite(point.samplePosition) || point.samplePosition < 0.0 || point.samplePosition <= previousPosition
                    || ! AutomationLane::isValueValid(parameter, point.value))
                    return juce::Result::fail("Project automation point is invalid");
                previousPosition = point.samplePosition;
            }
        }
        trackIds.push_back(track.id);
        largestTrackId = std::max(largestTrackId, track.id.value);
        for (const auto& clip : track.clips)
        {
            if (! clip.id.isValid() || clip.trackId != track.id
                || std::find(clipIds.begin(), clipIds.end(), clip.id) != clipIds.end())
                return juce::Result::fail("Project has invalid clip IDs or relationships");
            if (! std::isfinite(clip.startSample) || ! std::isfinite(clip.durationSamples)
                || ! std::isfinite(clip.sourceOffsetSamples) || ! std::isfinite(clip.loopLengthSamples) || ! std::isfinite(clip.gain)
                || ! std::isfinite(clip.fadeInSamples) || ! std::isfinite(clip.fadeOutSamples) || clip.startSample < 0.0
                || clip.durationSamples <= 0.0 || clip.sourceOffsetSamples < 0.0 || clip.loopLengthSamples < 0.0
                || clip.gain < 0.0f || clip.gain > 4.0f || clip.fadeInSamples < 0.0
                || clip.fadeOutSamples < 0.0 || clip.fadeInSamples > clip.durationSamples
                || clip.fadeOutSamples > clip.durationSamples)
                return juce::Result::fail("Project clip timing is invalid");
            clipIds.push_back(clip.id);
            largestClipId = std::max(largestClipId, clip.id.value);
            if (clip.takeGroupId == 0 && ! clip.takeActive)
                return juce::Result::fail("Project has an inactive clip outside a take group");
            if (clip.takeGroupId != 0 && clip.takeId == 0)
                return juce::Result::fail("Project take clip has no take identity");
            largestTakeGroupId = std::max(largestTakeGroupId, clip.takeGroupId);
        }

        for (const auto& persistedFolder : track.takeFolders)
        {
            const auto& folder = persistedFolder.state;
            if (! folder.id.isValid() || folder.trackId != track.id || folder.durationSamples <= 0.0
                || ! std::isfinite(folder.startSample) || ! std::isfinite(folder.durationSamples)
                || folder.takeIds.size() < 2 || folder.comps.empty() || ! folder.activeCompId.isValid())
                return juce::Result::fail("Project take folder is invalid");
            if (std::find_if(folder.comps.begin(), folder.comps.end(), [&] (const TakeCompState& comp)
                { return comp.id == folder.activeCompId; }) == folder.comps.end())
                return juce::Result::fail("Project take folder has no active comp");
            for (const auto takeId : folder.takeIds)
                if (takeId == 0 || std::none_of(track.clips.begin(), track.clips.end(), [&] (const PersistedClipState& clip)
                    { return clip.takeGroupId == folder.id.value && clip.takeId == takeId; }))
                    return juce::Result::fail("Project take folder references a missing take");
            largestTakeGroupId = std::max(largestTakeGroupId, folder.id.value);
            for (const auto& comp : folder.comps)
            {
                if (! comp.id.isValid() || comp.name.isEmpty())
                    return juce::Result::fail("Project take comp is invalid");
                largestCompId = std::max(largestCompId, comp.id.value);
                for (const auto& segment : comp.segments)
                    if (! std::isfinite(segment.startSample) || ! std::isfinite(segment.durationSamples)
                        || segment.durationSamples <= 0.0 || segment.sourceTakeId == 0
                        || std::find(folder.takeIds.begin(), folder.takeIds.end(), segment.sourceTakeId) == folder.takeIds.end())
                        return juce::Result::fail("Project comp segment is invalid");
            }
        }

        for (const auto& clip : track.clips)
            if (clip.takeGroupId != 0)
            {
                std::vector<uint64_t> activeTakeIds;
                for (const auto& candidate : track.clips)
                {
                    if (candidate.takeGroupId != clip.takeGroupId || ! candidate.takeActive)
                        continue;
                    if (std::find(activeTakeIds.begin(), activeTakeIds.end(), candidate.takeId) == activeTakeIds.end())
                        activeTakeIds.push_back(candidate.takeId);
                }
                if (activeTakeIds.size() != 1)
                    return juce::Result::fail("Project take group must have one active take");
            }
    }
    midiClipIds.reserve(state.midiClips.size());
    for (const auto& clip : state.midiClips)
    {
        if (! clip.id.isValid() || std::find(midiClipIds.begin(), midiClipIds.end(), clip.id) != midiClipIds.end()
            || std::find(trackIds.begin(), trackIds.end(), clip.trackId) == trackIds.end()
            || ! std::isfinite(clip.startSample) || clip.startSample < 0.0)
            return juce::Result::fail("Project MIDI clip state is invalid");
        midiClipIds.push_back(clip.id);
        largestMidiClipId = std::max(largestMidiClipId, clip.id.value);
        for (const auto& note : clip.notes)
        {
            if (! note.id.isValid() || std::find(midiEventIds.begin(), midiEventIds.end(), note.id) != midiEventIds.end()
                || note.pitch < 0 || note.pitch > 127 || ! std::isfinite(note.velocity)
                || note.velocity < 0.0f || note.velocity > 1.0f || ! std::isfinite(note.startSample)
                || ! std::isfinite(note.durationSamples) || note.startSample < 0.0 || note.durationSamples <= 0.0
                || note.channel < 1 || note.channel > 16)
                return juce::Result::fail("Project MIDI note state is invalid");
            midiEventIds.push_back(note.id);
            largestMidiEventId = std::max(largestMidiEventId, note.id.value);
        }
    }
    double previousTempoPosition = -1.0;
    for (const auto& event : state.tempoMap)
    {
        if (! std::isfinite(event.samplePosition) || ! std::isfinite(event.bpm) || event.samplePosition < 0.0
            || event.samplePosition < previousTempoPosition || event.bpm < 40.0 || event.bpm > 220.0)
            return juce::Result::fail("Project tempo map is invalid");
        previousTempoPosition = event.samplePosition;
    }
    double previousSignaturePosition = -1.0;
    for (const auto& event : state.timeSignatureMap)
    {
        const auto validDenominator = event.denominator == 1 || event.denominator == 2 || event.denominator == 4
            || event.denominator == 8 || event.denominator == 16;
        if (! std::isfinite(event.samplePosition) || event.samplePosition < 0.0
            || event.samplePosition < previousSignaturePosition || event.numerator < 1 || event.numerator > 32
            || ! validDenominator)
            return juce::Result::fail("Project time signature map is invalid");
        previousSignaturePosition = event.samplePosition;
    }
    if (largestTrackId == std::numeric_limits<uint64_t>::max() || largestBusId == std::numeric_limits<uint64_t>::max()
        || largestClipId == std::numeric_limits<uint64_t>::max() || largestTakeGroupId == std::numeric_limits<uint64_t>::max()
        || largestCompId == std::numeric_limits<uint64_t>::max()
        || largestMidiClipId == std::numeric_limits<uint64_t>::max() || largestMidiEventId == std::numeric_limits<uint64_t>::max())
        return juce::Result::fail("Project ID range cannot allocate new objects safely");

    std::vector<TrackState> restoredTracks;
    restoredTracks.reserve(state.tracks.size());
    for (const auto& track : state.tracks)
    {
        restoredTracks.emplace_back();
        auto& restored = restoredTracks.back();
        restored.id = track.id;
        restored.type = track.type;
        restored.name = track.name.isNotEmpty() ? track.name
                                                : "Track " + juce::String(static_cast<int>(restoredTracks.size()));
        restored.volume.store(track.volume, std::memory_order_relaxed);
        restored.pan.store(track.pan, std::memory_order_relaxed);
        restored.muted.store(track.muted, std::memory_order_relaxed);
        restored.solo.store(track.solo, std::memory_order_relaxed);
        restored.soloSafe.store(track.soloSafe, std::memory_order_relaxed);
        restored.inputMonitoring.store(track.inputMonitoring, std::memory_order_relaxed);
        restored.autoInputMonitoring.store(track.autoInputMonitoring, std::memory_order_relaxed);
        restored.monitorDestination.store(track.monitorDestination, std::memory_order_relaxed);
        restored.monitorPermitted.store(false, std::memory_order_relaxed);
        restored.inputChannel.store(track.inputChannel, std::memory_order_relaxed);
        restored.inputChannelCount.store(track.inputChannelCount, std::memory_order_relaxed);
        restored.midiInputChannel.store(track.midiInputChannel, std::memory_order_relaxed);
        restored.midiInputDeviceIdentifier = track.midiInputDeviceIdentifier;
        restored.midiInputDeviceToken.store(track.midiInputDeviceIdentifier.isEmpty() ? 0u
            : static_cast<uint32_t>(track.midiInputDeviceIdentifier.hashCode()), std::memory_order_relaxed);
        restored.outputBus = track.outputBus;
        restored.activeSendCount = track.activeSendCount;
        for (size_t i = 0; i < maxSendsPerTrack; ++i)
            restored.sends[i] = { track.sends[i].targetBus, track.sends[i].level, track.sends[i].preFader, track.sends[i].postFaderBeforePan };
        for (const auto& lane : track.automationLanes)
        {
            const auto parameter = static_cast<AutomationParameter>(lane.parameter);
            AutomationLane* restoredLane = parameter == AutomationParameter::trackVolume ? &restored.volumeAutomation
                                             : parameter == AutomationParameter::trackPan ? &restored.panAutomation
                                             : lane.sendSlot >= 0 && lane.sendSlot < static_cast<int>(maxSendsPerTrack)
                                                ? &restored.sendAutomation[static_cast<size_t>(lane.sendSlot)] : nullptr;
            if (restoredLane == nullptr) continue;
            restoredLane->clear();
            restoredLane->setMode(static_cast<AutomationMode>(lane.mode));
            for (const auto& point : lane.points)
                restoredLane->upsertPoint(point.samplePosition, point.value);
        }
        restored.clips.reserve(track.clips.size());
        for (const auto& clip : track.clips)
        {
            AudioClipState restoredClip;
            restoredClip.id = clip.id;
            restoredClip.trackId = clip.trackId;
            restoredClip.sourceFile = juce::File(clip.sourcePath);
            restoredClip.clipName = clip.clipName;
            restoredClip.startSample = clip.startSample;
            restoredClip.durationSamples = clip.durationSamples;
            restoredClip.sourceOffsetSamples = clip.sourceOffsetSamples;
            restoredClip.loopEnabled = clip.loopEnabled;
            restoredClip.loopLengthSamples = clip.loopLengthSamples;
            restoredClip.gain = clip.gain;
            restoredClip.fadeInSamples = clip.fadeInSamples;
            restoredClip.fadeOutSamples = clip.fadeOutSamples;
            restoredClip.takeGroupId = clip.takeGroupId;
            restoredClip.takeId = clip.takeId != 0 ? clip.takeId : clip.id.value;
            restoredClip.takeActive = clip.takeActive;
            restoredClip.recordedTake = clip.recordedTake;
            restored.clips.push_back(std::move(restoredClip));
        }
        restored.takeFolders.reserve(track.takeFolders.size());
        for (const auto& folder : track.takeFolders)
            restored.takeFolders.push_back(folder.state);
    }

    std::vector<MidiClipState> restoredMidiClips;
    restoredMidiClips.reserve(state.midiClips.size());
    for (const auto& clip : state.midiClips)
    {
        MidiClipState restoredClip;
        restoredClip.id = clip.id;
        restoredClip.trackId = clip.trackId;
        restoredClip.startSample = clip.startSample;
        restoredClip.contentOffsetSamples = clip.contentOffsetSamples;
        restoredClip.durationSamples = clip.durationSamples;
        restoredClip.notes.reserve(clip.notes.size());
        for (const auto& note : clip.notes)
            restoredClip.notes.push_back({ note.id, note.pitch, note.velocity, note.startSample,
                                           note.durationSamples, note.channel });
        restoredMidiClips.push_back(std::move(restoredClip));
    }

    std::vector<BusState> restoredBuses;
    restoredBuses.reserve(state.buses.size());
    for (const auto& bus : state.buses)
    {
        const auto fallbackName = "Aux " + juce::String(static_cast<int>(restoredBuses.size() + 1));
        restoredBuses.push_back({ bus.id, bus.name.isNotEmpty() ? bus.name : fallbackName, bus.gain, bus.muted, bus.balance });
    }

    audioMediaPool.clear();
    trackStates = std::move(restoredTracks);
    midiClips = std::move(restoredMidiClips);
    busStates = std::move(restoredBuses);
    controlRoutingProfile = state.controlRoutingProfile;
    liveKeyReferenceTrack = state.liveKeyReferenceTrack;
    tempoMap.clear();
    tempoMap.reserve(state.tempoMap.size());
    for (const auto& event : state.tempoMap) tempoMap.push_back({ event.samplePosition, event.bpm });
    timeSignatureMap.clear();
    timeSignatureMap.reserve(std::max<size_t>(1, state.timeSignatureMap.size()));
    if (state.timeSignatureMap.empty())
        timeSignatureMap.push_back({ 0.0, state.timeSignatureNumerator, state.timeSignatureDenominator });
    else
        for (const auto& event : state.timeSignatureMap)
            timeSignatureMap.push_back({ event.samplePosition, event.numerator, event.denominator });
    markers.clear();
    markers.reserve(state.markers.size());
    for (const auto& marker : state.markers) markers.push_back({ marker.samplePosition, marker.name });
    bpm.store(state.bpm, std::memory_order_relaxed);
    timeSignatureNumerator.store(state.timeSignatureNumerator, std::memory_order_relaxed);
    timeSignatureDenominator.store(state.timeSignatureDenominator, std::memory_order_relaxed);
    cycleStartSample.store(state.cycleStartSample, std::memory_order_relaxed);
    cycleEndSample.store(state.cycleEndSample, std::memory_order_relaxed);
    cycleActive.store(state.cycleActive, std::memory_order_relaxed);
    punchInSample.store(state.punchInSample, std::memory_order_relaxed);
    punchOutSample.store(state.punchOutSample, std::memory_order_relaxed);
    punchActive.store(state.punchActive, std::memory_order_relaxed);
    playing.store(false, std::memory_order_relaxed);
    playheadPosition.store(0.0, std::memory_order_relaxed);
    nextTrackId = largestTrackId + 1;
    nextBusId = largestBusId + 1;
    nextClipId = largestClipId + 1;
    nextTakeGroupId = largestTakeGroupId + 1;
    nextCompId = largestCompId + 1;
    nextMidiClipId = largestMidiClipId + 1;
    nextMidiEventId = largestMidiEventId + 1;

    for (size_t slot = 0; slot < maxTracks; ++slot)
    {
        fxRackTrackIds[slot] = slot < trackStates.size() ? trackStates[slot].id : TrackId {};
        publishFxRackSnapshot(slot, std::make_unique<FxRackSnapshot>());
    }
    publishAudioClipSnapshot();
    publishMidiClipSnapshot();
    publishRenderStructureSnapshot();
    clearUndoHistory();
    markProjectModified();
    sendChangeMessage();
    return juce::Result::ok();
}

juce::Result TrackDataModel::setControlRoutingProfile(ControlRoutingProfile profile)
{
    if (const auto result = profile.validate(); result.failed()) return result;
    controlRoutingProfile = std::move(profile);
    markProjectModified();
    sendChangeMessage();
    return juce::Result::ok();
}

void TrackDataModel::setLiveKeyReferenceTrack(TrackId track) noexcept
{
    liveKeyReferenceTrack = track;
    markProjectModified();
    sendChangeMessage();
}

void TrackDataModel::setFxProcessor(size_t trackIndex, size_t slot, std::shared_ptr<AudioEffectProcessor> processor)
{
    if (trackIndex >= trackStates.size() || slot >= maxFxSlots) return;
    const auto rackSlot = getFxRackSlot(trackStates[trackIndex].id);
    if (rackSlot < 0) return;
    auto updated = std::make_unique<FxRackSnapshot>();
    if (const auto* current = getFxRackSnapshot(trackIndex); current != nullptr) *updated = *current;
    updated->processors[slot] = std::move(processor);
    publishFxRackSnapshot(static_cast<size_t>(rackSlot), std::move(updated));
    publishRenderStructureSnapshot();
    markProjectModified();
    sendChangeMessage();
}

void TrackDataModel::setFxBypassed(size_t trackIndex, size_t slot, bool bypassed) noexcept
{
    if (trackIndex >= trackStates.size() || slot >= maxFxSlots) return;
    const auto rackSlot = getFxRackSlot(trackStates[trackIndex].id);
    if (rackSlot < 0) return;
    auto updated = std::make_unique<FxRackSnapshot>();
    if (const auto* current = getFxRackSnapshot(trackIndex); current != nullptr) *updated = *current;
    updated->bypass[slot] = bypassed;
    publishFxRackSnapshot(static_cast<size_t>(rackSlot), std::move(updated));
    publishRenderStructureSnapshot();
    markProjectModified();
    sendChangeMessage();
}

const std::vector<TrackDataModel::FxScene>& TrackDataModel::getFxScenes(size_t trackIndex) const noexcept
{
    static const std::vector<FxScene> emptyScenes;
    return trackIndex < trackStates.size() ? trackStates[trackIndex].fxScenes : emptyScenes;
}

bool TrackDataModel::captureFxScene(size_t trackIndex, const juce::String& name)
{
    if (trackIndex >= trackStates.size() || trackStates[trackIndex].fxScenes.size() >= maxFxScenes)
        return false;

    FxScene scene;
    scene.name = name.trim().substring(0, 32);
    if (scene.name.isEmpty())
        scene.name = "Scene " + juce::String(static_cast<int>(trackStates[trackIndex].fxScenes.size() + 1));
    if (const auto* rack = getFxRackSnapshot(trackIndex); rack != nullptr)
        scene.bypass = rack->bypass;

    trackStates[trackIndex].fxScenes.push_back(std::move(scene));
    markProjectModified();
    sendChangeMessage();
    return true;
}

bool TrackDataModel::recallFxScene(size_t trackIndex, size_t sceneIndex) noexcept
{
    if (trackIndex >= trackStates.size() || sceneIndex >= trackStates[trackIndex].fxScenes.size())
        return false;

    const auto rackSlot = getFxRackSlot(trackStates[trackIndex].id);
    if (rackSlot < 0)
        return false;

    auto updated = std::make_unique<FxRackSnapshot>();
    if (const auto* current = getFxRackSnapshot(trackIndex); current != nullptr)
        *updated = *current;
    updated->bypass = trackStates[trackIndex].fxScenes[sceneIndex].bypass;
    publishFxRackSnapshot(static_cast<size_t>(rackSlot), std::move(updated));
    publishRenderStructureSnapshot();
    sendChangeMessage();
    return true;
}

void TrackDataModel::publishAudioClipSnapshot()
{
    auto updated = std::make_unique<std::vector<AudioClipState>>();
    for (const auto& state : trackStates)
    {
        // Ordinary arrangement clips remain direct render sources. A take
        // folder instead contributes only its active comp selections.
        for (const auto& clip : state.clips)
            if (clip.takeGroupId == 0)
                updated->push_back(clip);

        for (const auto& folder : state.takeFolders)
        {
            const auto comp = std::find_if(folder.comps.begin(), folder.comps.end(), [&] (const TakeCompState& candidate)
            {
                return candidate.id == folder.activeCompId;
            });
            if (comp == folder.comps.end())
                continue;
            for (const auto& segment : comp->segments)
            {
                const auto segmentEnd = segment.startSample + segment.durationSamples;
                for (const auto& source : state.clips)
                {
                    if (source.takeGroupId != folder.id.value || source.takeId != segment.sourceTakeId)
                        continue;
                    const auto rangeStart = juce::jmax(segment.startSample, source.startSample);
                    const auto rangeEnd = juce::jmin(segmentEnd, source.startSample + source.durationSamples);
                    if (rangeEnd <= rangeStart)
                        continue;
                    auto renderedSegment = source;
                    renderedSegment.takeGroupId = 0;
                    renderedSegment.takeActive = true;
                    renderedSegment.startSample = rangeStart;
                    renderedSegment.durationSamples = rangeEnd - rangeStart;
                    renderedSegment.sourceOffsetSamples += rangeStart - source.startSample;
                    renderedSegment.fadeInSamples = rangeStart == segment.startSample ? segment.fadeInSamples : 0.0;
                    renderedSegment.fadeOutSamples = rangeEnd == segmentEnd ? segment.fadeOutSamples : 0.0;
                    updated->push_back(std::move(renderedSegment));
                }
            }
        }

        // Legacy projects can contain pre-folder take groups. Preserve their
        // previous audible behavior until they are explicitly migrated.
        const auto folderOwnsGroup = [&state] (uint64_t groupId)
        {
            return std::any_of(state.takeFolders.begin(), state.takeFolders.end(), [groupId] (const TakeFolderState& folder)
            {
                return folder.id.value == groupId;
            });
        };
        for (const auto& clip : state.clips)
            if (clip.takeGroupId != 0 && ! folderOwnsGroup(clip.takeGroupId) && clip.takeActive)
                updated->push_back(clip);
    }
    std::unique_ptr<const std::vector<AudioClipState>> next = std::move(updated);
    auto previous = std::move(audioClipSnapshot);
    audioClipSnapshot = std::move(next);
    publishedAudioClipSnapshot.store(audioClipSnapshot.get(), std::memory_order_seq_cst);
    if (previous != nullptr) retiredAudioClipSnapshots.push_back(std::move(previous));
}
void TrackDataModel::publishMidiClipSnapshot()
{
    auto previous = std::move(midiClipSnapshot);
    midiClipSnapshot = std::make_unique<const std::vector<MidiClipState>>(midiClips);
    publishedMidiClipSnapshot.store(midiClipSnapshot.get(), std::memory_order_seq_cst);
    if (previous != nullptr) retiredMidiClipSnapshots.push_back(std::move(previous));
}

void TrackDataModel::publishFxRackSnapshot(size_t trackIndex, std::unique_ptr<const FxRackSnapshot> snapshot)
{
    auto previous = std::move(fxRacks[trackIndex]);
    fxRacks[trackIndex] = std::move(snapshot);
    publishedFxRacks[trackIndex].store(fxRacks[trackIndex].get(), std::memory_order_seq_cst);
    if (previous != nullptr) retiredFxRackSnapshots[trackIndex].push_back(std::move(previous));
}
void TrackDataModel::publishBusFxRackSnapshot(size_t busIndex, std::unique_ptr<const FxRackSnapshot> snapshot)
{
    if (busIndex >= maxBuses || snapshot == nullptr) return;
    auto previous = std::move(busFxRacks[busIndex]);
    busFxRacks[busIndex] = std::move(snapshot);
    publishedBusFxRacks[busIndex].store(busFxRacks[busIndex].get(), std::memory_order_release);
    if (previous != nullptr) retiredBusFxRacks[busIndex].push_back(std::move(previous));
}

void TrackDataModel::publishRenderStructureSnapshot()
{
    auto next = std::make_unique<RenderStructureSnapshot>();
    next->trackCount = trackStates.size();
    next->busCount = busStates.size();
    for (size_t index = 0; index < next->busCount; ++index)
        next->buses[index] = { busStates[index].id, busStates[index].gain, busStates[index].muted,
                               publishedBusFxRacks[index].load(std::memory_order_acquire), busStates[index].balance };
    for (size_t index = 0; index < next->trackCount; ++index)
    {
        const auto& track = trackStates[index];
        auto& renderTrack = next->tracks[index];
        renderTrack.id = track.id;
        renderTrack.type = track.type;
        renderTrack.volume = track.volume.load(std::memory_order_relaxed);
        renderTrack.pan = track.pan.load(std::memory_order_relaxed);
        renderTrack.muted = track.muted.load(std::memory_order_relaxed);
        renderTrack.solo = track.solo.load(std::memory_order_relaxed);
        renderTrack.soloSafe = track.soloSafe.load(std::memory_order_relaxed);
        renderTrack.armed = track.armed.load(std::memory_order_relaxed);
        renderTrack.inputMonitoring = track.inputMonitoring.load(std::memory_order_relaxed);
        renderTrack.autoInputMonitoring = track.autoInputMonitoring.load(std::memory_order_relaxed);
        renderTrack.monitorDestination = track.monitorDestination.load(std::memory_order_relaxed);
        renderTrack.monitorPermitted = track.monitorPermitted.load(std::memory_order_relaxed);
        renderTrack.inputChannel = track.inputChannel.load(std::memory_order_relaxed);
        renderTrack.inputChannelCount = track.inputChannelCount.load(std::memory_order_relaxed);
        renderTrack.midiInputChannel = track.midiInputChannel.load(std::memory_order_relaxed);
        renderTrack.midiInputDeviceToken = track.midiInputDeviceToken.load(std::memory_order_relaxed);
        const auto rackSlot = getFxRackSlot(track.id);
        renderTrack.fxRack = rackSlot >= 0 ? publishedFxRacks[static_cast<size_t>(rackSlot)].load(std::memory_order_acquire) : nullptr;
        renderTrack.outputBus = track.outputBus;
        renderTrack.activeSendCount = track.activeSendCount;
        for (size_t i = 0; i < maxSendsPerTrack; ++i)
            renderTrack.sends[i] = { track.sends[i].targetBus, track.sends[i].level, track.sends[i].preFader,
                                     AutomationRenderLane::compile(track.sendAutomation[i]), track.sends[i].postFaderBeforePan };
        renderTrack.volumeAutomation = AutomationRenderLane::compile(track.volumeAutomation);
        renderTrack.panAutomation = AutomationRenderLane::compile(track.panAutomation);
    }
    std::unique_ptr<const RenderStructureSnapshot> snapshot = std::move(next);
    auto previous = std::move(renderStructureSnapshot);
    renderStructureSnapshot = std::move(snapshot);
    publishedRenderStructure.store(renderStructureSnapshot.get(), std::memory_order_seq_cst);
    if (previous != nullptr) retiredRenderStructures.push_back(std::move(previous));
    reclaimRetiredRealtimeSnapshots();
}

int TrackDataModel::getFxRackSlot(TrackId trackId) const noexcept
{
    for (size_t slot = 0; slot < maxTracks; ++slot)
        if (fxRackTrackIds[slot] == trackId) return static_cast<int>(slot);
    return -1;
}

void TrackDataModel::reclaimRetiredRealtimeSnapshots()
{
    if (realtimeSnapshotReaders.load(std::memory_order_seq_cst) != 0) return;
    retiredAudioClipSnapshots.clear();
    retiredMidiClipSnapshots.clear();
    for (auto& retiredRacks : retiredFxRackSnapshots) retiredRacks.clear();
    for (auto& retiredRacks : retiredBusFxRacks) retiredRacks.clear();
    retiredRenderStructures.clear();
}

size_t TrackDataModel::getRetiredRealtimeSnapshotCount() const noexcept
{
    size_t count = retiredAudioClipSnapshots.size();
    count += retiredMidiClipSnapshots.size();
    for (const auto& retiredRacks : retiredFxRackSnapshots) count += retiredRacks.size();
    for (const auto& retiredRacks : retiredBusFxRacks) count += retiredRacks.size();
    count += retiredRenderStructures.size();
    return count;
}

void TrackDataModel::timerCallback()
{
    reclaimRetiredRealtimeSnapshots();
}
