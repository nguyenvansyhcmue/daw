#include <cmath>
#include <iostream>
#include <chrono>
#include <atomic>
#include <new>

#ifdef STUDIOFORGE_PERF_BENCHMARK
std::atomic<bool> studioForgeMeasureAllocations { false };
std::atomic<uint64_t> studioForgeAllocationCount { 0 };
void* operator new(std::size_t size)
{
    if (studioForgeMeasureAllocations.load(std::memory_order_relaxed)) studioForgeAllocationCount.fetch_add(1, std::memory_order_relaxed);
    if (auto* allocation = std::malloc(size)) return allocation;
    throw std::bad_alloc();
}
void operator delete(void* allocation) noexcept { std::free(allocation); }
#endif

#include "AudioEngine/AudioGraph.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/GainUtilityProcessor.h"
#include "AudioEngine/TrackMixing.h"
#include "AudioEngine/TransportUtils.h"
#include "AudioEngine/GlobalScaleContext.h"
#include "AudioEngine/VocalistProcessor.h"
#include "AudioEngine/VocalistRack.h"
#include "AudioEngine/AuxRouting.h"
#include "Media/WaveformThumbnailCache.h"
#include "Models/TrackDataModel.h"
#include "Media/MediaReloadService.h"
#include "Midi/MidiCore.h"
#include "Midi/MidiFileImporter.h"
#include "Midi/MidiTransposeProcessor.h"
#include "Plugins/PluginHostService.h"
#include "Project/ProjectSerializer.h"
#include "Project/RecentProjectsStore.h"
#include "Project/AutosaveService.h"
#include "Project/ProjectTemplates.h"
#include "Project/ProjectTemplateStore.h"
#include "Project/ProjectAlternativeStore.h"
#include "UI/Inspector/InspectorViewState.h"
#include "AutomationDomainTests.h"

namespace
{
bool expect(bool condition, const char* message);

bool testGlobalScaleContext()
{
    GlobalScaleContext context;
    const auto initial = context.read();
    if (! expect(initial.rootNote == 0 && initial.scale == MusicalScale::chromatic,
                 "scale context starts chromatic")) return false;

    context.publish(9, MusicalScale::minor);
    const auto updated = context.read();
    return expect(updated.rootNote == 9 && updated.scale == MusicalScale::minor
                  && updated.revision == initial.revision + 1,
                  "scale context publishes a new immutable revision");
}

bool testVocalistProcessor()
{
    GlobalScaleContext scale;
    scale.publish(7, MusicalScale::major);
    VocalistProcessor vocalist;
    vocalist.prepare(48000.0, 8, 2);
    vocalist.setGain(0.5f);

    juce::AudioBuffer<float> buffer(2, 8);
    buffer.clear();
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample(channel, sample, 1.0f);
    vocalist.process(buffer, scale);

    const auto audioOk = buffer.getSample(0, 7) > 0.0f && buffer.getSample(0, 7) < 1.0f;
    const auto scaleOk = vocalist.getLastScaleRevision() == scale.read().revision;
    return expect(audioOk, "vocalist processor applies realtime gain")
        && expect(scaleOk, "vocalist processor observes scale revision");
}

bool testVocalistRackSnapshots()
{
    VocalistRack rack;
    uint32_t hostId = 0, guestId = 0;
    if (! expect(rack.addVocalist(0, hostId) && rack.addVocalist(1, guestId), "vocalist rack adds inputs")) return false;
    {
        const auto read = rack.acquire();
        if (! expect(read->count == 2 && read->vocalists[0].id == hostId,
                     "vocalist rack publishes immutable snapshot")) return false;
        rack.setGain(hostId, 0.75f);
        if (! expect(read->vocalists[0].gain == 1.0f,
                     "active vocalist reader keeps its snapshot")) return false;
    }
    rack.reclaim();
    return expect(rack.removeVocalist(guestId) && rack.acquire()->count == 1,
                  "vocalist rack removes a vocalist on the control thread");
}

bool testAuxRoutingSnapshots()
{
    AuxRouting routing;
    uint32_t reverb = 0;
    if (! expect(routing.addAux(reverb), "aux routing creates a shared return")) return false;
    if (! expect(routing.setSend(0, 0, reverb, 0.6f, false),
                 "aux routing connects a vocalist send")) return false;
    const auto read = routing.acquire();
    return expect(read->auxCount == 1 && read->sends[0][0].auxId == reverb
                  && read->sends[0][0].level == 0.6f,
                  "aux routing publishes immutable send snapshot");
}

bool testProjectTemplates()
{
    const auto empty = ProjectTemplates::create(ProjectTemplate::empty);
    if (! expect(empty.tracks.empty() && empty.tempoMap.size() == 1, "empty template has only a valid tempo map")) return false;

    const auto recording = ProjectTemplates::create(ProjectTemplate::audioRecording);
    if (! expect(recording.tracks.size() == 4 && std::all_of(recording.tracks.begin(), recording.tracks.end(),
        [] (const PersistedTrackState& track) { return track.type == TrackType::audio && track.id.isValid(); }),
        "audio recording template creates valid audio tracks")) return false;

    const auto midi = ProjectTemplates::create(ProjectTemplate::midiProduction);
    TrackDataModel model;
    return expect(midi.tracks.size() == 3 && midi.tracks[0].type == TrackType::instrument
                      && midi.tracks[2].type == TrackType::externalMidi,
                  "MIDI production template creates instrument and external MIDI tracks")
        && expect(model.applyProjectState(midi).wasOk() && model.getTrackCount() == 3,
                  "template project state is accepted by the model");
}

bool testMidiFileImport()
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("StudioForgeMidiImport", ".mid", false);
    juce::MidiMessageSequence sequence;
    sequence.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0.0);
    sequence.addEvent(juce::MidiMessage::noteOff(1, 60), 480.0);
    sequence.addEvent(juce::MidiMessage::noteOn(2, 67, static_cast<juce::uint8>(80)), 480.0);
    sequence.addEvent(juce::MidiMessage::noteOff(2, 67), 960.0);

    juce::MidiFile fileToWrite;
    fileToWrite.setTicksPerQuarterNote(480);
    fileToWrite.addTrack(sequence);
    auto output = file.createOutputStream();
    if (! expect(output != nullptr && fileToWrite.writeTo(*output), "test MIDI file is written"))
        return false;
    output.reset();

    std::vector<ImportedMidiTrack> imported;
    const auto parseResult = MidiFileImporter::read(file, 48000.0, 120.0, imported);
    TrackDataModel model;
    model.setSampleRate(48000.0);
    const auto importResult = parseResult.wasOk()
        ? model.importMidiTracks(imported, 960.0)
        : parseResult;

    const auto importedCorrectly = importResult.wasOk()
        && imported.size() == 1
        && imported.front().notes.size() == 2
        && model.getTrackCount() == 1
        && model.getTrack(0).type == TrackType::instrument
        && model.getMidiClips().size() == 1
        && model.getMidiClips().front().startSample == 960.0
        && model.getMidiClips().front().notes.size() == 2
        && std::abs(model.getMidiClips().front().notes[0].durationSamples - 24000.0) < 1.0e-4
        && model.undo()
        && model.getTrackCount() == 0
        && model.getMidiClips().empty()
        && model.redo()
        && model.getTrackCount() == 1
        && model.getMidiClips().size() == 1;
    file.deleteFile();
    return expect(importedCorrectly, "MIDI file import creates an undoable instrument track and note clip");
}

bool testProjectTemplateStore()
{
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("StudioForgeTemplateStore", {}, true);
    ProjectTemplateStore store(directory);
    const auto templateFile = store.makeTemplateFile("My / Recording Template");
    const auto written = directory.createDirectory() && templateFile.replaceWithText("template");
    const auto templates = store.load();
    const auto valid = written
        && templateFile.hasFileExtension("studioforge-template")
        && templateFile.getParentDirectory() == directory
        && templates.size() == 1
        && templates[0] == templateFile.getFullPathName();
    directory.deleteRecursively();
    return expect(valid, "template store isolates and lists user project templates");
}

bool testProjectAlternativeStore()
{
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("StudioForgeAlternativeStore", {}, true);
    const auto projectFile = directory.getChildFile("Mix.studioforge");
    const auto rootWritten = directory.createDirectory() && projectFile.replaceWithText("project");
    ProjectAlternativeStore store;
    const auto alternativeFile = store.makeAlternativeFile(projectFile, "Vocal / Edit");
    const auto alternativeWritten = alternativeFile.getParentDirectory().createDirectory()
        && alternativeFile.replaceWithText("alternative");
    const auto alternatives = store.load(projectFile);
    const auto valid = rootWritten && alternativeWritten
        && alternativeFile.hasFileExtension("studioforge")
        && alternativeFile.getParentDirectory().getFileName() == "Mix Alternatives"
        && alternatives.size() == 1
        && alternatives[0] == alternativeFile.getFullPathName();
    directory.deleteRecursively();
    return expect(valid, "alternative store isolates and lists project alternatives");
}

class OfflineAudioDevice final : public juce::AudioIODevice
{
public:
    explicit OfflineAudioDevice(int size = 4) : juce::AudioIODevice("Offline", "Test"), blockSize(size) {}

    juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { 44100.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { blockSize }; }
    int getDefaultBufferSize() override { return blockSize; }
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start(juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return false; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return blockSize; }
    double getCurrentSampleRate() override { return 44100.0; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return 3; }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
private:
    int blockSize;
};

bool testMetronomeRendering()
{
    TrackDataModel model;
    model.setSampleRate(44100.0);
    AudioEngine engine(&model);
    OfflineAudioDevice device(64);
    engine.audioDeviceAboutToStart(&device);
    std::array<float, 64> left {};
    std::array<float, 64> right {};
    float* outputs[] { left.data(), right.data() };

    model.setPlaying(true);
    model.setPlayheadPosition(0.0);
    engine.setMetronomeEnabled(false);
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 64, {});
    const auto silentWhenDisabled = std::all_of(left.begin(), left.end(), [] (float sample) { return sample == 0.0f; });

    left.fill(0.0f);
    right.fill(0.0f);
    model.setPlayheadPosition(0.0);
    engine.setMetronomeEnabled(true);
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 64, {});
    const auto audibleWhenEnabled = std::any_of(left.begin(), left.end(), [] (float sample) { return std::abs(sample) > 1.0e-5f; });
    engine.audioDeviceStopped();
    return expect(silentWhenDisabled && audibleWhenEnabled,
                  "metronome is an allocation-free playback signal controlled by atomic state");
}

class MidiProbeProcessor final : public AudioEffectProcessor
{
public:
    void prepareToPlay(double, int, int) override {}
    void processBlock(juce::AudioBuffer<float>&) override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer& midi) override { receivedEvents = midi.getNumEvents(); }
    void releaseResources() override {}
    juce::String getName() const override { return "MIDI Probe"; }
    int receivedEvents = 0;
};

class MidiPitchProbeProcessor final : public AudioEffectProcessor
{
public:
    void prepareToPlay(double, int, int) override {}
    void processBlock(juce::AudioBuffer<float>&) override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer& midi) override
    {
        juce::MidiBuffer::Iterator iterator(midi);
        juce::MidiMessage message;
        int samplePosition = 0;
        if (iterator.getNextEvent(message, samplePosition) && message.isNoteOn())
            receivedPitch = message.getNoteNumber();
    }
    void releaseResources() override {}
    juce::String getName() const override { return "MIDI Pitch Probe"; }
    int receivedPitch = -1;
};

bool approximatelyEqual(float actual, float expected) noexcept
{
    return std::abs(actual - expected) < 1.0e-4f;
}

bool expect(bool condition, const char* message)
{
    if (! condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool writeTestWav(const juce::File& file, float sampleValue)
{
    file.deleteFile();
    auto stream = file.createOutputStream();
    if (stream == nullptr) return false;
    juce::WavAudioFormat wav;
    auto* rawStream = stream.release();
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(rawStream, 44100.0, 2, 32, {}, 0));
    if (writer == nullptr)
    {
        delete rawStream;
        return false;
    }
    juce::AudioBuffer<float> buffer(2, 4);
    buffer.clear();
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 4; ++sample)
            buffer.setSample(channel, sample, sampleValue);
    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

bool testInspectorViewStateBuilder()
{
    TrackDataModel model;
    model.setSampleRate(44100.0);
    const auto emptyRegion = InspectorViewStateBuilder::makeEmptyRegion();
    if (! expect(emptyRegion.kind == InspectorRegionKind::none && emptyRegion.title == "No region selected",
                 "empty inspector region state")) return false;

    const auto instrumentId = model.addTrack(TrackType::instrument);
    const auto instrumentIndex = model.getTrackIndex(instrumentId);
    model.setTrackName(static_cast<size_t>(instrumentIndex), "Lead Synth");
    const auto trackState = InspectorViewStateBuilder::makeTrack(model, instrumentIndex);
    if (! expect(trackState.isSelected && ! trackState.supportsInputMonitoring
                 && trackState.name == "Lead Synth" && trackState.inputDetails == "Software Instrument",
                 "instrument inspector track state")) return false;

    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 128);
    source->clear();
    const auto clipId = model.addClipToTrack(instrumentIndex, {}, 44100.0, source);
    if (! expect(clipId.isValid(), "audio clip created for inspector state")) return false;

    const auto regionState = InspectorViewStateBuilder::makeAudioRegion(model, clipId);
    return expect(regionState.kind == InspectorRegionKind::audio
                  && regionState.timelineDetails.contains("Start 1.00 s")
                  && regionState.modifierDetails.contains("Gain 0.0 dB"),
                  "audio inspector region state");
}

bool testTrackAutomationSnapshots()
{
    TrackDataModel model;
    const auto track = model.addTrack(TrackType::audio);
    const auto index = model.getTrackIndex(track);
    if (! expect(index == 0, "automation target track created")) return false;
    if (! expect(model.upsertTrackAutomationPoint(0, AutomationParameter::trackVolume, 0.0, 0.25f)
                 && model.upsertTrackAutomationPoint(0, AutomationParameter::trackVolume, 480.0, 1.0f),
                 "automation points update model")) return false;

    const auto snapshot = model.acquireRealtimeSnapshot();
    const auto* structure = snapshot.getRenderStructure();
    if (! expect(structure != nullptr && structure->tracks[0].volumeAutomation.pointCount == 2
                 && approximatelyEqual(structure->tracks[0].volumeAutomation.evaluate(240.0), 0.625f),
                 "automation compiles to immutable render snapshot")) return false;

    if (! expect(model.undo(), "automation point edit is undoable")) return false;
    const auto* lane = model.getTrackAutomationLane(0, AutomationParameter::trackVolume);
    return expect(lane != nullptr && lane->getPoints().size() == 1, "automation undo restores lane contents");
}

bool testTrackMixing()
{
    juce::AudioBuffer<float> buffer(2, 8);
    buffer.clear();
    buffer.addSample(0, 0, 1.0f);
    buffer.addSample(1, 0, 1.0f);
    TrackMixing::apply(buffer, 8, { 0.5f, 0.0f, true });
    if (! expect(approximatelyEqual(buffer.getSample(0, 0), 0.3535534f), "constant-power centre gain left")) return false;
    if (! expect(approximatelyEqual(buffer.getSample(1, 0), 0.3535534f), "constant-power centre gain right")) return false;

    buffer.setSample(0, 0, 1.0f);
    buffer.setSample(1, 0, 1.0f);
    TrackMixing::apply(buffer, 8, { 1.0f, -1.0f, true });
    if (! expect(approximatelyEqual(buffer.getSample(0, 0), 1.0f), "pan left keeps left")) return false;
    if (! expect(approximatelyEqual(buffer.getSample(1, 0), 0.0f), "pan left mutes right")) return false;

    buffer.setSample(0, 0, 1.0f);
    buffer.setSample(1, 0, 1.0f);
    TrackMixing::apply(buffer, 8, { 1.0f, 1.0f, true });
    if (! expect(approximatelyEqual(buffer.getSample(0, 0), 0.0f), "pan right mutes left")) return false;
    if (! expect(approximatelyEqual(buffer.getSample(1, 0), 1.0f), "pan right keeps right")) return false;

    buffer.setSample(0, 0, 1.0f); buffer.setSample(1, 0, 1.0f);
    TrackMixing::apply(buffer, 8, { 1.0f, -0.5f, true });
    if (! expect(approximatelyEqual(buffer.getSample(0, 0), 0.9238795f) && approximatelyEqual(buffer.getSample(1, 0), 0.3826834f), "half-left uses constant-power law")) return false;
    buffer.setSample(0, 0, 1.0f); buffer.setSample(1, 0, 1.0f);
    TrackMixing::apply(buffer, 8, { 1.0f, 0.5f, true });
    if (! expect(approximatelyEqual(buffer.getSample(0, 0), 0.3826834f) && approximatelyEqual(buffer.getSample(1, 0), 0.9238795f), "half-right uses constant-power law")) return false;

    TrackMixing::apply(buffer, 8, { 1.0f, 0.0f, false });
    return expect(approximatelyEqual(TrackMixing::peak(buffer, 8), 0.0f), "mute and solo exclusion clear track");
}

bool testRecordArmTargetSelection()
{
    TrackDataModel model;
    model.ensureTrackCount(3);
    if (! expect(model.getFirstArmedTrackIndex() == -1,
                 "record target is absent when no track is armed")) return false;

    model.setTrackArmed(2, true);
    if (! expect(model.getFirstArmedTrackIndex() == 2,
                 "record target follows the armed track")) return false;

    model.setTrackArmed(1, true);
    return expect(model.getFirstArmedTrackIndex() == 1,
                  "multiple armed tracks choose the first visible track deterministically");
}

bool testProjectRevision()
{
    TrackDataModel model;
    model.ensureTrackCount(1);
    const auto baseline = model.getProjectRevision();
    model.setTrackVolume(0, 0.5f);
    const auto afterVolume = model.getProjectRevision();
    if (! expect(afterVolume > baseline, "persisted mixer edit advances project revision")) return false;
    model.setTrackVolume(0, 0.5f);
    if (! expect(model.getProjectRevision() == afterVolume, "no-op mixer edit leaves project revision stable")) return false;
    model.setCycle(10.0, 20.0);
    if (! expect(model.getProjectRevision() > afterVolume, "persisted transport edit advances project revision")) return false;
    const auto beforeUndo = model.getProjectRevision();
    return expect(model.undo() && model.getProjectRevision() > beforeUndo,
                  "undo advances project revision so unsaved history cannot be missed");
}

bool testMasterGraph()
{
    juce::AudioBuffer<float> buffer(2, 4);
    buffer.clear();
    buffer.setSample(0, 0, 1.0f);
    buffer.setSample(1, 0, 1.0f);
    AudioGraph graph;
    graph.setMasterGain(0.25f);
    graph.process(buffer, 4);
    return expect(approximatelyEqual(buffer.getSample(0, 0), 0.25f)
                      && approximatelyEqual(buffer.getSample(1, 0), 0.25f),
                  "master gain is applied exactly once");
}

bool testTransportLoopBoundary()
{
    const auto firstSegment = TransportUtils::samplesUntilBoundary(12.0, 5, true, 10.0, 14.0);
    const auto afterFirst = TransportUtils::advance(12.0, firstSegment, true, 10.0, 14.0);
    const auto secondSegment = TransportUtils::samplesUntilBoundary(afterFirst, 5 - firstSegment, true, 10.0, 14.0);
    const auto afterSecond = TransportUtils::advance(afterFirst, secondSegment, true, 10.0, 14.0);
    return expect(firstSegment == 2, "cycle splits block at boundary")
        && expect(approximatelyEqual(static_cast<float>(afterFirst), 10.0f), "cycle wraps at boundary")
        && expect(secondSegment == 3, "cycle renders remaining block after wrap")
        && expect(approximatelyEqual(static_cast<float>(afterSecond), 13.0f), "cycle advances after wrapped segment");
}

bool testModelSampleRate()
{
    TrackDataModel model;
    model.setSampleRate(44100.0);
    return expect(std::abs(model.getSampleRate() - 44100.0) < 1.0e-9,
                  "model uses supplied device sample rate");
}

bool testTrackCapacity()
{
    TrackDataModel model;
    model.ensureTrackCount(TrackDataModel::maxTracks);
    if (! expect(model.getTrackCount() == TrackDataModel::maxTracks,
                 "model accepts all preallocated track render slots")) return false;

    return expect(! model.addTrack().isValid(),
                  "model rejects a track beyond the fixed realtime capacity");
}

bool testTrackReadDoesNotCreateTracks()
{
    TrackDataModel model;
    static_cast<void>(model.getTrack(0));
    static_cast<void>(model.getTrack(TrackDataModel::maxTracks - 1));
    return expect(model.getTrackCount() == 0,
                  "reading an empty project never creates default tracks");
}

bool testRealtimeSnapshotOwnership()
{
    TrackDataModel model;
    model.ensureTrackCount(1);
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 8);
    source->clear();

    const auto read = model.acquireRealtimeSnapshot();
    const auto* previousSnapshot = read.getAudioClips();
    const auto* previousRack = read.getFxRack(0);
    if (! expect(previousSnapshot != nullptr && previousSnapshot->empty(), "initial realtime snapshot is valid")) return false;
    const auto clipId = model.addClipToTrack(0, {}, 0.0, source);
    if (! expect(clipId.isValid(), "clip publication succeeds while reader is active")) return false;
    if (! expect(read.getAudioClips() == previousSnapshot && previousSnapshot->empty(),
                 "active audio reader retains its immutable snapshot")) return false;
    model.setFxProcessor(0, 0, std::make_shared<GainUtilityProcessor>());
    if (! expect(read.getFxRack(0) == previousRack && previousRack->processors[0] == nullptr,
                 "active audio reader retains its immutable FX snapshot")) return false;
    if (! expect(model.getRetiredRealtimeSnapshotCount() > 0,
                 "replaced snapshot is deferred while audio reader is active")) return false;
    model.reclaimRetiredRealtimeSnapshots();
    return expect(model.getRetiredRealtimeSnapshotCount() > 0,
                  "control thread does not reclaim while audio reader is active");
}

bool testRealtimeSnapshotReclaim()
{
    TrackDataModel model;
    model.ensureTrackCount(1);
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 8);
    source->clear();
    {
        const auto read = model.acquireRealtimeSnapshot();
        model.addClipToTrack(0, {}, 0.0, source);
        if (! expect(read.getAudioClips() != nullptr && model.getRetiredRealtimeSnapshotCount() > 0,
                     "snapshot remains deferred before reader release")) return false;
    }
    model.reclaimRetiredRealtimeSnapshots();
    return expect(model.getRetiredRealtimeSnapshotCount() == 0,
                  "control thread reclaims retired snapshots after reader release");
}

bool testStableTrackAndClipIdentity()
{
    TrackDataModel model;
    const auto trackA = model.addTrack();
    const auto trackB = model.addTrack();
    model.setFxProcessor(0, 0, std::make_shared<GainUtilityProcessor>());
    const auto trackC = model.addTrack();
    if (! expect(trackA.isValid() && trackB.isValid() && trackC.isValid(), "tracks receive IDs")) return false;
    if (! expect(model.reorderTrack(trackB, 0), "track reorder succeeds")) return false;
    if (! expect(model.getTrackId(0) == trackB && model.getTrackId(1) == trackA && model.getTrackId(2) == trackC,
                 "reorder changes ordering but preserves track IDs")) return false;
    if (! expect(model.removeTrack(trackB), "track deletion succeeds")) return false;
    if (! expect(model.getTrackId(0) == trackA && model.getTrackId(1) == trackC,
                 "deleting a track does not change other track IDs")) return false;
    const auto trackD = model.addTrack();
    if (! expect(trackD.isValid() && trackD != trackA && trackD != trackB && trackD != trackC,
                 "new track ID does not collide")) return false;

    model.setFxProcessor(static_cast<size_t>(model.getTrackIndex(trackA)), 0,
                         std::make_shared<GainUtilityProcessor>());
    if (! expect(model.getFxRackSnapshot(static_cast<size_t>(model.getTrackIndex(trackA)))->processors[0] != nullptr,
                 "FX rack attaches to track identity")) return false;
    if (! expect(model.reorderTrack(trackA, 2), "second track reorder succeeds")) return false;
    if (! expect(model.getFxRackSnapshot(static_cast<size_t>(model.getTrackIndex(trackA)))->processors[0] != nullptr,
                 "FX rack remains attached after reorder")) return false;

    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 16);
    source->clear();
    const auto clipId = model.addClipToTrack(model.getTrackIndex(trackA), {}, 0.0, source);
    if (! expect(clipId.isValid(), "audio clip receives stable ID")) return false;
    if (! expect(model.trimAudioClip(clipId, 2.0, 8.0), "clip trim by ID succeeds")) return false;
    if (! expect(model.getTrack(static_cast<size_t>(model.getTrackIndex(trackA))).clips.front().id == clipId,
                 "trim preserves clip ID")) return false;
    if (! expect(model.moveAudioClip(clipId, trackC, 4.0), "clip move by ID succeeds")) return false;
    const auto trackCIndex = static_cast<size_t>(model.getTrackIndex(trackC));
    if (! expect(model.getTrack(trackCIndex).clips.front().id == clipId
                 && model.getTrack(trackCIndex).clips.front().trackId == trackC,
                 "move preserves clip ID and updates track ID")) return false;
    model.splitAudioClip(trackCIndex, 0, 7.0);
    const auto& splitClips = model.getTrack(trackCIndex).clips;
    if (! expect(splitClips.size() == 2 && splitClips[0].id == clipId && splitClips[1].id != clipId,
                 "split keeps left clip ID and assigns a new right clip ID")) return false;
    return expect(model.deleteAudioClip(splitClips[1].id) && model.deleteAudioClip(clipId),
                  "clips can be deleted by stable ID");
}

bool testPlaybackStructuralSnapshots()
{
    TrackDataModel model;
    const auto trackA = model.addTrack();
    const auto trackB = model.addTrack();
    model.setFxProcessor(0, 0, std::make_shared<GainUtilityProcessor>());
    model.setPlaying(true);
    {
        const auto callbackA = model.acquireRealtimeSnapshot();
        const auto* structureA = callbackA.getRenderStructure();
        if (! expect(structureA != nullptr && structureA->trackCount == 2, "callback captures initial structure")) return false;

        const auto trackC = model.addTrack();
        if (! expect(trackC.isValid(), "add track succeeds during playback")) return false;
        if (! expect(callbackA.getRenderStructure() == structureA && structureA->trackCount == 2,
                     "current callback keeps old structure after add")) return false;

        const auto callbackB = model.acquireRealtimeSnapshot();
        if (! expect(callbackB.getRenderStructure()->trackCount == 3,
                     "next callback observes added track")) return false;
        if (! expect(model.reorderTrack(trackC, 0), "reorder succeeds during playback")) return false;
        const auto callbackC = model.acquireRealtimeSnapshot();
        if (! expect(callbackC.getRenderStructure()->tracks[0].id == trackC,
                     "next callback observes new render order")) return false;
        const auto trackAIndex = callbackC.getRenderStructure()->getTrackIndex(trackA);
        if (! expect(trackAIndex >= 0
                     && callbackC.getRenderStructure()->tracks[static_cast<size_t>(trackAIndex)].fxRack->processors[0] != nullptr,
                     "FX ownership follows TrackId during playback reorder")) return false;
        if (! expect(model.removeTrack(trackB), "remove succeeds during playback")) return false;
        const auto callbackD = model.acquireRealtimeSnapshot();
        if (! expect(callbackD.getRenderStructure()->trackCount == 2
                     && callbackD.getRenderStructure()->getTrackIndex(trackB) < 0
                     && callbackD.getRenderStructure()->getTrackIndex(trackA) >= 0
                     && callbackD.getRenderStructure()->tracks[static_cast<size_t>(callbackD.getRenderStructure()->getTrackIndex(trackA))].fxRack->processors[0] != nullptr,
                     "remove publishes structure without dangling track")) return false;
    }
    model.reclaimRetiredRealtimeSnapshots();
    if (! expect(model.getRetiredRealtimeSnapshotCount() == 0,
                 "structural snapshots reclaim after callbacks complete")) return false;

    TrackDataModel zeroTrackModel;
    zeroTrackModel.setPlaying(true);
    const auto zeroTrackCallback = zeroTrackModel.acquireRealtimeSnapshot();
    return expect(zeroTrackCallback.getRenderStructure() != nullptr
                      && zeroTrackCallback.getRenderStructure()->trackCount == 0,
                  "zero-track render structure is valid");
}

bool testProjectPersistence()
{
    const auto projectFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("StudioForgeCorePersistenceTest.studioforge");
    const auto malformedFile = projectFile.getSiblingFile("StudioForgeCoreMalformed.studioforge");
    const auto unsupportedFile = projectFile.getSiblingFile("StudioForgeCoreUnsupported.studioforge");
    const auto sparseIdFile = projectFile.getSiblingFile("StudioForgeCoreSparseIds.studioforge");
    projectFile.deleteFile();
    malformedFile.deleteFile();
    unsupportedFile.deleteFile();
    sparseIdFile.deleteFile();

    TrackDataModel source;
    const auto trackA = source.addTrack(TrackType::audio);
    const auto trackB = source.addTrack(TrackType::externalMidi);
    const auto trackC = source.addTrack(TrackType::instrument);
    source.reorderTrack(trackC, 0);
    source.setTrackName(0, "Drums");
    source.setTrackVolume(0, 1.5f);
    source.setTrackPan(0, -0.25f);
    source.upsertTrackAutomationPoint(0, AutomationParameter::trackVolume, 0.0, 0.5f);
    source.upsertTrackAutomationPoint(0, AutomationParameter::trackVolume, 960.0, 1.0f);
    source.setTrackMuted(0, true);
    source.setTrackSolo(1, true);
    source.setTrackSoloSafe(static_cast<size_t>(source.getTrackIndex(trackA)), true);
    source.setTrackInputMonitoring(static_cast<size_t>(source.getTrackIndex(trackA)), true, 1);
    const auto bus = source.addBus();
    source.setTrackOutputBus(trackA, bus);
    source.setTrackSend(trackB, bus, 0.35f);
    source.setBpm(137.0);
    source.setTimeSignatureNumerator(7);
    source.clearTempoMap();
    source.addTempoEvent(960.0, 90.0);
    source.setCycle(100.0, 500.0);
    source.setPunchRange(180.0, 420.0);
    auto audio = std::make_shared<juce::AudioBuffer<float>>(2, 64);
    audio->clear();
    const auto clip = source.addClipToTrack(static_cast<int>(source.getTrackIndex(trackA)),
                                            juce::File("Z:/does-not-exist/source.wav"), 10.0, audio);
    source.trimAudioClip(clip, 12.0, 40.0);
    source.moveAudioClip(clip, trackB, 24.0);
    source.splitAudioClip(static_cast<size_t>(source.getTrackIndex(trackB)), 0, 40.0);
    const auto midiClip = source.addMidiClip(trackC, 120.0);
    const auto midiNote = source.addMidiNote(midiClip, 64, 0.75f, 24.0, 96.0, 3);
    if (! expect(midiClip.isValid() && midiNote.isValid(), "MIDI state is created for persistence")) return false;
    const auto before = source.createProjectState();
    if (! expect(ProjectSerializer::save(source, projectFile).wasOk(), "project save succeeds")) return false;

    TrackDataModel loaded;
    juce::StringArray missingMedia;
    if (! expect(ProjectSerializer::load(loaded, projectFile, &missingMedia).wasOk(), "project load succeeds")) return false;
    const auto after = loaded.createProjectState();
    if (! expect(after.tracks.size() == before.tracks.size() && after.tempoMap.size() == before.tempoMap.size()
                 && approximatelyEqual(static_cast<float>(after.bpm), static_cast<float>(before.bpm))
                 && after.timeSignatureNumerator == before.timeSignatureNumerator && after.cycleActive
                 && after.cycleStartSample == before.cycleStartSample && after.cycleEndSample == before.cycleEndSample
                 && after.punchActive && after.punchInSample == before.punchInSample
                 && after.punchOutSample == before.punchOutSample,
                 "transport and project state round-trip")) return false;
    if (! expect(after.tracks[0].id == trackC && after.tracks[1].id == trackA && after.tracks[2].id == trackB,
                 "track order and IDs round-trip")) return false;
    if (! expect(after.tracks[0].name == "Drums", "track name round-trips")) return false;
    if (! expect(after.tracks[0].type == TrackType::instrument
                 && after.tracks[1].type == TrackType::audio
                 && after.tracks[2].type == TrackType::externalMidi,
                 "track types round-trip with project order")) return false;
    if (! expect(after.tracks[0].volume == 1.5f && after.tracks[0].pan == -0.25f && after.tracks[0].muted
                 && after.tracks[1].soloSafe && after.tracks[1].solo, "mixer state round-trips")) return false;
    if (! expect(after.tracks[0].automationLanes.size() == 2
                 && after.tracks[0].automationLanes[0].points.size() == 2
                 && approximatelyEqual(after.tracks[0].automationLanes[0].points[0].value, 0.5f),
                 "automation lanes round-trip with project state")) return false;
    if (! expect(after.tracks[1].inputMonitoring && after.tracks[1].inputChannel == 1,
                 "track input-monitor routing round-trips")) return false;
    if (! expect(after.buses.size() == 1 && after.buses[0].id == bus
                 && after.tracks[1].outputBus == bus && after.tracks[2].activeSendCount == 1
                 && after.tracks[2].sends[0].targetBus == bus && approximatelyEqual(after.tracks[2].sends[0].level, 0.35f),
                 "bus and send routing round-trips")) return false;
    if (! expect(loaded.clearTrackSend(trackB) && loaded.createProjectState().tracks[2].activeSendCount == 0,
                 "track send can be disabled without changing track ownership")) return false;
    const auto secondBus = loaded.addBus();
    if (! expect(secondBus.isValid()
                 && loaded.setTrackSendRoute(trackB, 0, bus, 0.25f, true)
                 && loaded.setTrackSendRoute(trackB, 1, secondBus, 0.75f, false)
                 && loaded.clearTrackSendRoute(trackB, 0),
                 "send route removal accepts a sparse fixed matrix")) return false;
    const auto sparseRoutes = loaded.createProjectState().tracks[2];
    if (! expect(sparseRoutes.activeSendCount == 1
                 && ! sparseRoutes.sends[0].targetBus.isValid()
                 && sparseRoutes.sends[1].targetBus == secondBus
                 && approximatelyEqual(sparseRoutes.sends[1].level, 0.75f)
                 && ! sparseRoutes.sends[1].preFader,
                 "removing a send leaves its slot empty and preserves later slot positions")) return false;
    if (! expect(after.tracks[2].clips.size() == 2 && after.tracks[2].clips[0].id == clip
                 && after.tracks[2].clips[0].trackId == trackB
                 && after.tracks[2].clips[0].sourcePath == before.tracks[2].clips[0].sourcePath
                 && missingMedia.size() == 1, "clip IDs, relationship and missing-media reference round-trip")) return false;
    if (! expect(after.midiClips.size() == 1 && after.midiClips[0].id == midiClip
                 && after.midiClips[0].trackId == trackC && after.midiClips[0].startSample == 120.0
                 && after.midiClips[0].notes.size() == 1 && after.midiClips[0].notes[0].id == midiNote
                 && after.midiClips[0].notes[0].pitch == 64 && after.midiClips[0].notes[0].velocity == 0.75f
                 && after.midiClips[0].notes[0].channel == 3,
                 "MIDI clips and notes round-trip with IDs and timing")) return false;
    const auto newTrack = loaded.addTrack();
    if (! expect(newTrack.isValid() && newTrack.value > trackC.value, "post-load track ID does not collide")) return false;
    const auto newAudio = std::make_shared<juce::AudioBuffer<float>>(2, 8);
    const auto newClip = loaded.addClipToTrack(0, {}, 0.0, newAudio);
    if (! expect(newClip.isValid() && newClip.value > after.tracks[2].clips[1].id.value,
                 "post-load clip ID does not collide")) return false;

    malformedFile.replaceWithText("not XML");
    const auto originalTrack = loaded.getTrackId(0);
    if (! expect(ProjectSerializer::load(loaded, malformedFile).failed() && loaded.getTrackId(0) == originalTrack,
                 "malformed project fails without mutating current project")) return false;
    unsupportedFile.replaceWithText("<StudioForgeProject formatVersion=\"999\" bpm=\"120\" timeSignatureNumerator=\"4\" cycleActive=\"0\" cycleStartSample=\"0\" cycleEndSample=\"0\"><TempoMap><Tempo samplePosition=\"0\" bpm=\"120\"/></TempoMap><Tracks/></StudioForgeProject>");
    if (! expect(ProjectSerializer::load(loaded, unsupportedFile).failed() && loaded.getTrackId(0) == originalTrack,
                 "unsupported version fails without mutating current project")) return false;

    ProjectState sparseIds;
    sparseIds.tempoMap.push_back({ 0.0, 120.0 });
    for (const auto id : { uint64_t { 3 }, uint64_t { 8 }, uint64_t { 12 } })
    {
        PersistedTrackState track;
        track.id = { id };
        track.clips.push_back({ { id }, track.id, "Z:/does-not-exist/sparse.wav", "sparse", 0.0, 8.0, 0.0 });
        sparseIds.tracks.push_back(std::move(track));
    }
    TrackDataModel sparseSource;
    if (! expect(sparseSource.applyProjectState(sparseIds).wasOk()
                 && ProjectSerializer::save(sparseSource, sparseIdFile).wasOk(),
                 "sparse IDs can be saved")) return false;
    TrackDataModel sparseLoaded;
    if (! expect(ProjectSerializer::load(sparseLoaded, sparseIdFile).wasOk()
                 && sparseLoaded.getTrackId(0).value == 3 && sparseLoaded.getTrackId(1).value == 8
                 && sparseLoaded.getTrackId(2).value == 12, "sparse IDs are restored exactly")) return false;
    const auto continuedTrack = sparseLoaded.addTrack();
    const auto continuedClip = sparseLoaded.addClipToTrack(0, {}, 0.0, newAudio);
    if (! expect(continuedTrack.value > 12 && continuedClip.value > 12,
                 "sparse restored IDs advance both allocators safely")) return false;
    projectFile.deleteFile();
    malformedFile.deleteFile();
    unsupportedFile.deleteFile();
    sparseIdFile.deleteFile();
    return true;
}

bool testAutosaveRecovery()
{
    const auto temporaryRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("StudioForgeAutosaveTests");
    temporaryRoot.deleteRecursively();

    TrackDataModel source;
    source.addTrack(TrackType::audio);
    const auto projectFile = temporaryRoot.getChildFile("Session.studioforge");
    AutosaveService autosave(source, temporaryRoot.getChildFile("Recovery"));
    autosave.setActiveProject(projectFile);

    if (! expect(autosave.saveRecoveryNow() && autosave.hasRecoverySnapshot(),
                 "autosave writes a lightweight recovery snapshot"))
    {
        temporaryRoot.deleteRecursively();
        return false;
    }

    TrackDataModel recovered;
    const auto recoveredFile = autosave.getRecoveryFile();
    const auto recoveredOk = ProjectSerializer::load(recovered, recoveredFile).wasOk()
        && recovered.getTrackCount() == 1;
    autosave.markProjectSaved();
    const auto clearedOk = ! autosave.hasRecoverySnapshot();
    temporaryRoot.deleteRecursively();
    return expect(recoveredOk, "recovery snapshot restores the project model")
        && expect(clearedOk, "successful explicit save clears the recovery snapshot");
}

bool testRecentProjectsStore()
{
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("StudioForgeRecentProjectsTest");
    const auto first = directory.getChildFile("first.studioforge");
    const auto second = directory.getChildFile("second.studioforge");
    const auto storage = directory.getChildFile("recent-projects.txt");
    directory.deleteRecursively();
    if (! directory.createDirectory() || ! first.replaceWithText("first") || ! second.replaceWithText("second"))
        return expect(false, "recent-project test files are created");

    RecentProjectsStore store(storage);
    store.add(first);
    store.add(second);
    store.add(first);
    auto entries = store.load();
    if (! expect(entries.size() == 2 && entries[0] == first.getFullPathName() && entries[1] == second.getFullPathName(),
                 "recent projects are deduplicated and ordered by last open")) return false;
    second.deleteFile();
    entries = store.load();
    if (! expect(entries.size() == 1 && entries[0] == first.getFullPathName(),
                 "missing recent project paths are filtered")) return false;
    store.clear();
    const auto cleared = store.load();
    directory.deleteRecursively();
    return expect(cleared.isEmpty(), "recent projects can be cleared");
}

bool testMediaReloadAndRelink()
{
    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto mediaA = tempDirectory.getChildFile("StudioForgePhase5A.wav");
    const auto mediaB = tempDirectory.getChildFile("StudioForgePhase5B.wav");
    const auto projectA = tempDirectory.getChildFile("StudioForgePhase5A.studioforge");
    const auto projectB = tempDirectory.getChildFile("StudioForgePhase5B.studioforge");
    const auto portableMediaA = tempDirectory.getChildFile("StudioForgePhase5A Media").getChildFile("clip-1.wav");
    const auto portableMediaB = tempDirectory.getChildFile("StudioForgePhase5B Media").getChildFile("clip-1.wav");
    const auto missingFile = tempDirectory.getChildFile("StudioForgePhase5Missing.wav");
    mediaA.deleteFile();
    mediaB.deleteFile();
    projectA.deleteFile();
    projectB.deleteFile();
    portableMediaA.getParentDirectory().deleteRecursively();
    portableMediaB.getParentDirectory().deleteRecursively();
    missingFile.deleteFile();
    if (! expect(writeTestWav(mediaA, 0.75f) && writeTestWav(mediaB, 0.25f), "test WAV files are written")) return false;

    auto original = std::make_shared<juce::AudioBuffer<float>>(2, 4);
    original->clear();
    TrackDataModel sourceA;
    sourceA.ensureTrackCount(1);
    const auto firstClip = sourceA.addClipToTrack(0, mediaA, 0.0, original);
    const auto secondClip = sourceA.addClipToTrack(0, mediaA, 4.0, original);
    if (! expect(ProjectSerializer::save(sourceA, projectA).wasOk() && portableMediaA.existsAsFile(),
                 "media project A saves a portable copy")) return false;

    TrackDataModel loaded;
    if (! expect(ProjectSerializer::load(loaded, projectA).wasOk(), "valid media reloads after project load")) return false;
    const auto& loadedClips = loaded.getTrack(0).clips;
    if (! expect(loadedClips.size() == 2 && loaded.getClipMediaStatus(firstClip) == AudioMediaStatus::Ready
                 && loadedClips[0].cachedBuffer != nullptr && loadedClips[0].cachedBuffer == loadedClips[1].cachedBuffer,
                 "multiple clips share decoded media ownership")) return false;
    OfflineAudioDevice device;
    AudioEngine engine(&loaded);
    engine.audioDeviceAboutToStart(&device);
    loaded.setPlayheadPosition(0.0);
    loaded.setPlaying(true);
    float left[4] {}, right[4] {};
    float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(left[0], 0.5303301f) && approximatelyEqual(right[0], 0.5303301f),
                 "reloaded media plays through production audio path")) return false;

    TrackDataModel sourceB;
    sourceB.ensureTrackCount(1);
    sourceB.addClipToTrack(0, mediaB, 0.0, original);
    if (! expect(ProjectSerializer::save(sourceB, projectB).wasOk()
                 && ProjectSerializer::load(loaded, projectB).wasOk(), "project B replaces project A resources")) return false;
    const auto& projectBClip = loaded.getTrack(0).clips.front();
    if (! expect(projectBClip.sourceFile == portableMediaB && projectBClip.cachedBuffer != nullptr
                 && approximatelyEqual(projectBClip.cachedBuffer->getSample(0, 0), 0.25f),
                 "project B decoded resource replaces old project resource")) return false;

    TrackDataModel missingModel;
    missingModel.ensureTrackCount(1);
    const auto missingClip = missingModel.addClipToTrack(0, missingFile, 0.0, original);
    if (! expect(ProjectSerializer::save(missingModel, projectA).wasOk()
                 && ProjectSerializer::load(loaded, projectA).wasOk()
                 && loaded.getClipMediaStatus(missingClip) == AudioMediaStatus::Missing,
                 "missing media remains identifiable and safe")) return false;
    const auto oldPath = loaded.getTrack(0).clips.front().sourceFile;
    if (! expect(MediaReloadService::relinkClip(loaded, missingClip, missingFile).failed()
                 && loaded.getTrack(0).clips.front().sourceFile == oldPath
                 && loaded.getClipMediaStatus(missingClip) == AudioMediaStatus::Missing,
                 "failed relink leaves existing clip state unchanged")) return false;
    if (! expect(MediaReloadService::relinkClip(loaded, missingClip, mediaA).wasOk()
                 && loaded.getClipMediaStatus(missingClip) == AudioMediaStatus::Ready
                 && loaded.getTrack(0).clips.front().sourceFile == mediaA,
                 "successful relink validates then commits decoded media")) return false;

    mediaA.deleteFile();
    mediaB.deleteFile();
    projectA.deleteFile();
    projectB.deleteFile();
    portableMediaA.getParentDirectory().deleteRecursively();
    portableMediaB.getParentDirectory().deleteRecursively();
    return true;
}

bool testUndoRedoHistory()
{
    TrackDataModel model;
    const auto trackA = model.addTrack();
    const auto trackB = model.addTrack();
    auto audio = std::make_shared<juce::AudioBuffer<float>>(2, 32);
    audio->clear();
    const auto clip = model.addClipToTrack(0, {}, 0.0, audio);
    model.clearUndoHistory();

    if (! expect(model.moveAudioClip(clip, trackB, 4.0) && model.trimAudioClip(clip, 6.0, 16.0),
                 "move and trim are undoable edits")) return false;
    const auto trackBIndex = static_cast<size_t>(model.getTrackIndex(trackB));
    model.splitAudioClip(trackBIndex, 0, 12.0);
    const auto rightClip = model.getTrack(trackBIndex).clips[1].id;
    if (! expect(model.deleteAudioClip(rightClip) && model.canUndo(), "split and delete create undo history")) return false;
    if (! expect(model.undo() && model.getTrack(trackBIndex).clips.size() == 2,
                 "undo restores deleted split clip")) return false;
    if (! expect(model.undo() && model.getTrack(trackBIndex).clips.size() == 1
                 && model.getTrack(trackBIndex).clips.front().id == clip,
                 "undo restores pre-split clip identity")) return false;
    if (! expect(model.undo() && model.getTrack(trackBIndex).clips.front().startSample == 4.0,
                 "undo restores pre-trim clip position")) return false;
    if (! expect(model.undo() && model.getTrack(0).clips.front().id == clip
                 && model.getTrack(0).clips.front().cachedBuffer == audio,
                 "undo restores move without losing decoded media")) return false;
    if (! expect(model.redo(), "redo move succeeds")) return false;
    if (! expect(model.redo(), "redo trim succeeds")) return false;
    if (! expect(model.redo(), "redo split succeeds")) return false;
    if (! expect(model.redo() && model.getTrack(trackBIndex).clips.size() == 1
                 && model.getTrack(trackBIndex).clips.front().id == clip, "redo restores edit sequence")) return false;

    model.setTrackVolume(0, 0.4f);
    model.setTrackPan(0, 0.5f);
    if (! expect(model.undo() && model.getTrack(0).pan.load() == 0.0f
                 && model.undo() && model.getTrack(0).volume.load() == 1.0f,
                 "mixer changes undo independently")) return false;
    if (! expect(model.redo() && model.redo() && model.getTrack(0).volume.load() == 0.4f
                 && model.getTrack(0).pan.load() == 0.5f, "mixer changes redo independently")) return false;

    model.clearUndoHistory();
    const auto trackC = model.addTrack();
    if (! expect(model.reorderTrack(trackC, 0) && model.removeTrack(trackB), "track structural edits are undoable")) return false;
    if (! expect(model.undo() && model.getTrackIndex(trackB) >= 0, "undo restores removed track ID")) return false;
    if (! expect(model.undo() && model.getTrackId(2) == trackC, "undo restores original track order")) return false;
    if (! expect(model.undo() && model.getTrackIndex(trackC) < 0, "undo removes added track")) return false;
    const auto snapshots = model.acquireRealtimeSnapshot();
    return expect(snapshots.getRenderStructure() != nullptr
                      && snapshots.getRenderStructure()->trackCount == model.getTrackCount(),
                  "undo publishes a coherent realtime render structure");
}

bool testRecordingFoundation()
{
    const auto recordingFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("StudioForgePhase7Recording.wav");
    recordingFile.deleteFile();
    TrackDataModel model;
    model.ensureTrackCount(1);
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    const auto externalMidi = model.addTrack(TrackType::externalMidi);
    if (! expect(externalMidi.isValid()
                 && engine.startRecording(1, recordingFile).failed(),
                 "recording rejects non-audio tracks")) return false;
    if (! expect(engine.startRecording(0, recordingFile).wasOk() && engine.isRecording(),
                 "recording session starts with current input configuration")) return false;
    float inputLeft[4] { 0.1f, 0.2f, 0.3f, 0.4f };
    float inputRight[4] { 0.4f, 0.3f, 0.2f, 0.1f };
    const float* inputs[] { inputLeft, inputRight };
    float outputLeft[4] {}, outputRight[4] {};
    float* outputs[] { outputLeft, outputRight };
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 4, {});
    if (! expect(engine.stopRecording().wasOk() && ! engine.isRecording() && recordingFile.existsAsFile(),
                 "recording stops and finalizes file outside callback")) return false;
    const auto& clips = model.getTrack(0).clips;
    if (! expect(clips.size() == 1 && clips.front().cachedBuffer != nullptr
                 && approximatelyEqual(clips.front().cachedBuffer->getSample(0, 2), 0.3f)
                 && approximatelyEqual(clips.front().cachedBuffer->getSample(1, 2), 0.2f),
                 "recorded data becomes a playable clip")) return false;
    const auto clipCount = clips.size();
    if (! expect(engine.startRecording(0, recordingFile).wasOk()
                 && engine.stopRecording().wasOk() && model.getTrack(0).clips.size() == clipCount,
                 "empty recording finalizes without creating an invalid clip")) return false;
    recordingFile.deleteFile();
    return true;
}

bool testPunchRecordingCapture()
{
    const auto recordingFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("StudioForgePunchRecording.wav");
    recordingFile.deleteFile();

    TrackDataModel model;
    model.addTrack(TrackType::audio);
    model.setPlaying(true);
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    engine.setRecordingCaptureRange(1.0, 3.0);
    if (! expect(engine.startRecording(0, recordingFile, 1.0).wasOk(),
                 "punch recording pre-arms its destination on the control thread")) return false;

    float inputLeft[4] { 0.1f, 0.2f, 0.3f, 0.4f };
    float inputRight[4] { 0.4f, 0.3f, 0.2f, 0.1f };
    const float* inputs[] { inputLeft, inputRight };
    float outputLeft[4] {}, outputRight[4] {};
    float* outputs[] { outputLeft, outputRight };
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 4, {});
    if (! expect(engine.stopRecording().wasOk() && model.getTrack(0).clips.size() == 1,
                 "punch recording finalizes its captured range")) return false;

    const auto& recorded = *model.getTrack(0).clips.front().cachedBuffer;
    const auto correctSamples = recorded.getNumSamples() == 2
        && approximatelyEqual(recorded.getSample(0, 0), 0.2f)
        && approximatelyEqual(recorded.getSample(0, 1), 0.3f)
        && approximatelyEqual(recorded.getSample(1, 0), 0.3f)
        && approximatelyEqual(recorded.getSample(1, 1), 0.2f);
    recordingFile.deleteFile();
    return expect(correctSamples, "punch recording writes only samples inside the exact punch range");
}

bool testAutomationWriteModes()
{
    TrackDataModel model;
    model.addTrack(TrackType::audio);
    model.setTrackAutomationMode(0, AutomationParameter::trackVolume, AutomationMode::write);
    model.setTrackAutomationMode(0, AutomationParameter::trackPan, AutomationMode::touch);
    model.setPlaying(true);
    model.setPlayheadPosition(320.0);
    model.setTrackVolume(0, 0.45f);
    model.setTrackPan(0, -0.25f);

    const auto* volume = model.getTrackAutomationLane(0, AutomationParameter::trackVolume);
    const auto* pan = model.getTrackAutomationLane(0, AutomationParameter::trackPan);
    if (! expect(volume != nullptr && pan != nullptr && volume->getPoints().size() == 1
                 && pan->getPoints().size() == 1
                 && approximatelyEqual(volume->getPoints().front().value, 0.45f)
                 && approximatelyEqual(pan->getPoints().front().value, -0.25f),
                 "write and touch automation record control changes while transport plays")) return false;

    model.setPlaying(false);
    model.setPlayheadPosition(640.0);
    model.setTrackVolume(0, 0.80f);
    if (! expect(volume->getPoints().size() == 1,
                 "automation controls do not write new points while transport is stopped")) return false;

    model.setTrackAutomationMode(0, AutomationParameter::trackVolume, AutomationMode::read);
    const auto snapshot = model.acquireRealtimeSnapshot();
    const auto* structure = snapshot.getRenderStructure();
    return expect(structure != nullptr && structure->tracks[0].volumeAutomation.mode == AutomationMode::read
                      && approximatelyEqual(structure->tracks[0].volumeAutomation.evaluate(320.0), 0.45f),
                  "recorded automation becomes an immutable render lane in read mode");
}

bool testInputMonitoring()
{
    TrackDataModel model;
    model.addTrack(TrackType::audio);
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    model.setTrackInputMonitoring(0, true);
    float inputLeft[4] { 0.1f, 0.2f, 0.3f, 0.4f };
    float inputRight[4] { 0.4f, 0.3f, 0.2f, 0.1f };
    const float* inputs[] { inputLeft, inputRight };
    float outputLeft[4] {}, outputRight[4] {};
    float* outputs[] { outputLeft, outputRight };
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(outputLeft[2], 0.2121320f) && approximatelyEqual(outputRight[2], 0.1414213f),
                 "input monitoring passes selected device input")) return false;
    if (! expect(model.getPlayheadPosition() == 0.0,
                 "input monitoring does not move stopped transport")) return false;
    model.setTrackMuted(0, true);
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(outputLeft[0], 0.0f) && approximatelyEqual(outputRight[0], 0.0f),
                 "mute suppresses monitored input through the normal mixer path")) return false;
    model.setTrackInputMonitoring(0, false);
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 4, {});
    return expect(approximatelyEqual(outputLeft[0], 0.0f) && approximatelyEqual(outputRight[0], 0.0f),
                  "disabled input monitoring produces no stopped-transport audio");
}

bool testMidiInputMonitoring()
{
    TrackDataModel model;
    model.addTrack(TrackType::instrument);
    model.setTrackArmed(0, true);
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);

    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 69, 1.0f));
    float outputLeft[4] {}, outputRight[4] {};
    float* outputs[] { outputLeft, outputRight };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    const auto heardInputNote = std::any_of(std::begin(outputLeft), std::end(outputLeft),
                                            [] (float sample) { return std::abs(sample) > 1.0e-6f; });
    if (! expect(heardInputNote && model.getPlayheadPosition() == 0.0,
                 "armed instrument monitors MIDI input without starting transport")) return false;

    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 69));
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    const auto stoppedNote = std::all_of(std::begin(outputLeft), std::end(outputLeft),
                                         [] (float sample) { return std::abs(sample) <= 1.0e-6f; });
    return expect(stoppedNote, "MIDI note-off reaches the realtime instrument voice");
}

bool testMidiInputRoutesToArmedInstrument()
{
    TrackDataModel model;
    model.addTrack(TrackType::instrument);
    model.addTrack(TrackType::instrument);
    model.setTrackMuted(0, true);
    model.setTrackArmed(1, true);
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);

    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 69, 1.0f));
    float outputLeft[4] {}, outputRight[4] {};
    float* outputs[] { outputLeft, outputRight };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    const auto heardArmedTrack = std::any_of(std::begin(outputLeft), std::end(outputLeft),
                                             [] (float sample) { return std::abs(sample) > 1.0e-6f; });
    return expect(heardArmedTrack,
                  "changing record arm publishes MIDI input routing to the realtime instrument snapshot");
}

bool testMidiRecordingFoundation()
{
    TrackDataModel model;
    model.addTrack(TrackType::instrument);
    model.setPlaying(true);
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    model.clearUndoHistory();
    if (! expect(engine.startMidiRecording(0).wasOk() && engine.isMidiRecording(),
                 "MIDI recording starts for an instrument track")) return false;

    float outputLeft[4] {}, outputRight[4] {};
    float* outputs[] { outputLeft, outputRight };
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 64, 0.75f));
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 64));
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    const auto recordedClip = engine.stopMidiRecording();
    if (! expect(recordedClip.isValid() && ! engine.isMidiRecording() && model.getMidiClips().size() == 1,
                 "MIDI recording finalizes one clip on the control thread")) return false;

    const auto& note = model.getMidiClips().front().notes.front();
    return expect(note.pitch == 64 && std::abs(note.velocity - 0.75f) < 0.01f
                      && note.startSample == 0.0 && note.durationSamples == 4.0
                      && model.canUndo(),
                  "MIDI recording converts realtime note-on/off to a timed undoable note");
}

bool testOfflineBounce()
{
    const auto outputFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("StudioForgePhase8Bounce.wav");
    const auto flacFile = outputFile.withFileExtension(".flac");
    outputFile.deleteFile();
    flacFile.deleteFile();
    TrackDataModel model;
    model.ensureTrackCount(1);
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 4);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 4; ++sample) source->setSample(channel, sample, 0.5f);
    model.addClipToTrack(0, {}, 0.0, source);
    model.setTrackVolume(0, 0.5f);
    model.setCycle(1.0, 3.0);
    const auto revisionBeforeBounce = model.getProjectRevision();
    AudioEngine engine(&model);
    engine.setMasterGain(0.5f);
    if (! expect(engine.renderOfflineWav(outputFile, 44100.0, 2).wasOk() && outputFile.existsAsFile(),
                 "offline bounce writes WAV without an audio device")) return false;
    std::shared_ptr<juce::AudioBuffer<float>> rendered;
    if (! expect(MediaReloadService::decode(outputFile, rendered).wasOk()
                 && rendered->getNumSamples() == 4
                 && approximatelyEqual(rendered->getSample(0, 0), 0.0883883f)
                 && approximatelyEqual(rendered->getSample(1, 3), 0.0883883f),
                 "offline bounce uses production track and master processing")) return false;
    if (! expect(model.getProjectRevision() == revisionBeforeBounce && model.isCycleActive()
                 && model.getCycleStartSample() == 1.0 && model.getCycleEndSample() == 3.0,
                 "offline bounce restores transient transport without marking project dirty")) return false;
    AudioEngine::OfflineRenderOptions flacOptions;
    flacOptions.sampleRate = 44100.0;
    flacOptions.blockSize = 2;
    flacOptions.bitDepth = 24;
    if (! expect(engine.renderOfflineAudio(flacFile, flacOptions).wasOk() && flacFile.existsAsFile()
                 && MediaReloadService::decode(flacFile, rendered).wasOk() && rendered->getNumSamples() == 4,
                 "offline bounce exports a decodable FLAC through the shared render pipeline")) return false;
    outputFile.deleteFile();
    flacFile.deleteFile();
    return true;
}

bool testMidiOnlyOfflineBounce()
{
    const auto outputFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("StudioForgeMidiOnlyBounce.wav");
    outputFile.deleteFile();
    TrackDataModel model;
    const auto track = model.addTrack(TrackType::instrument);
    const auto clip = model.addMidiClip(track, 0.0);
    model.addMidiNote(clip, 69, 1.0f, 0.0, 128.0, 1);
    AudioEngine engine(&model);
    AudioEngine::OfflineRenderOptions options;
    options.sampleRate = 44100.0;
    options.blockSize = 64;
    const auto result = engine.renderOfflineAudio(outputFile, options);
    std::shared_ptr<juce::AudioBuffer<float>> rendered;
    const auto decoded = result.wasOk() && MediaReloadService::decode(outputFile, rendered).wasOk()
        && rendered != nullptr && rendered->getNumSamples() >= 128
        && std::any_of(rendered->getReadPointer(0), rendered->getReadPointer(0) + rendered->getNumSamples(),
                       [] (float sample) { return std::abs(sample) > 1.0e-6f; });
    outputFile.deleteFile();
    return expect(decoded, "MIDI-only project bounces its rendered instrument audio");
}

bool testClipGainAndFades()
{
    TrackDataModel model;
    model.ensureTrackCount(1);
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 4);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 4; ++sample) source->setSample(channel, sample, 1.0f);
    const auto clip = model.addClipToTrack(0, {}, 0.0, source);
    model.clearUndoHistory();
    model.setClipGain(clip, 0.5f);
    model.setClipFades(clip, 2.0, 2.0);
    model.setPlaying(true);
    float left[4] {}, right[4] {};
    float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(left[0], 0.0f) && approximatelyEqual(left[1], 0.1767767f)
                 && approximatelyEqual(left[2], 0.3535534f) && approximatelyEqual(left[3], 0.1767767f),
                 "clip gain and fades change rendered DSP amplitude")) return false;
    if (! expect(model.undo() && model.getTrack(0).clips.front().fadeInSamples == 0.0
                 && model.undo() && model.getTrack(0).clips.front().gain == 1.0f,
                 "clip gain and fade edits are undoable")) return false;
    return true;
}

bool testClipDuplicate()
{
    TrackDataModel model;
    const auto track = model.addTrack();
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 4);
    const auto original = model.addClipToTrack(0, {}, 10.0, source);
    const auto copy = model.duplicateAudioClip(original, track, 20.0);
    if (! expect(copy.isValid() && copy != original, "duplicate creates a distinct clip ID")) return false;
    const auto& clips = model.getTrack(0).clips;
    if (! expect(clips.size() == 2 && clips[1].cachedBuffer == source && clips[1].startSample == 20.0,
                 "duplicate shares media resource and preserves requested position")) return false;
    return expect(model.undo() && model.getTrack(0).clips.size() == 1, "duplicate is one undoable edit");
}

bool testAudioMediaSourceOwnership()
{
    TrackDataModel model;
    const auto firstTrack = model.addTrack(TrackType::audio);
    const auto secondTrack = model.addTrack(TrackType::audio);
    const auto sourceFile = juce::File("C:/StudioForgeTests/shared-vocal.wav");
    auto firstDecode = std::make_shared<juce::AudioBuffer<float>>(1, 8);
    auto secondDecode = std::make_shared<juce::AudioBuffer<float>>(1, 8);
    firstDecode->setSample(0, 0, 0.75f);
    secondDecode->setSample(0, 0, 0.25f);

    const auto firstClip = model.addClipToTrack(model.getTrackIndex(firstTrack), sourceFile, 0.0, firstDecode);
    const auto secondClip = model.addClipToTrack(model.getTrackIndex(secondTrack), sourceFile, 64.0, secondDecode);
    const auto& first = model.getTrack(static_cast<size_t>(model.getTrackIndex(firstTrack))).clips.front();
    const auto& second = model.getTrack(static_cast<size_t>(model.getTrackIndex(secondTrack))).clips.front();
    if (! expect(firstClip.isValid() && secondClip.isValid() && first.sourceId.isValid()
                 && first.sourceId == second.sourceId && first.cachedBuffer == second.cachedBuffer
                 && model.getAudioMediaSourceCount() == 1,
                 "same file imports share one source identity and immutable playback buffer")) return false;

    if (! expect(model.deleteAudioClip(firstClip), "one of two clips using a source can be deleted")) return false;
    const auto& remaining = model.getTrack(static_cast<size_t>(model.getTrackIndex(secondTrack))).clips.front();
    return expect(remaining.cachedBuffer == firstDecode && model.getAudioMediaSourceCount() == 1,
                  "deleting one clip preserves the remaining source-backed playback clip");
}

bool testBusAndSendRouting()
{
    TrackDataModel model;
    const auto trackA = model.addTrack();
    const auto trackB = model.addTrack();
    const auto bus = model.addBus();
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 4);
    source->clear(); source->addSample(0, 0, 1.0f); source->addSample(1, 0, 1.0f);
    model.addClipToTrack(model.getTrackIndex(trackA), {}, 0.0, source);
    model.addClipToTrack(model.getTrackIndex(trackB), {}, 0.0, source);
    model.setTrackSend(trackA, bus, 0.5f);
    model.setTrackOutputBus(trackB, bus);
    OfflineAudioDevice device;
    AudioEngine engine(&model); engine.audioDeviceAboutToStart(&device);
    model.setPlaying(true); model.setPlayheadPosition(0.0);
    float left[4] {}, right[4] {}; float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(left[0], 1.7677670f) && approximatelyEqual(right[0], 1.7677670f)
                 && approximatelyEqual(engine.getBusPeak(0), 1.0606602f),
                 "direct track, bus output and post-fader send mix exactly once")) return false;
    model.setBusGain(bus, 0.5f);
    model.setPlayheadPosition(0.0);
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(left[0], 1.2374369f) && approximatelyEqual(engine.getBusPeak(0), 0.5303301f),
                 "bus gain changes the routed audio and bus meter")) return false;
    model.setBusMuted(bus, true);
    model.setPlayheadPosition(0.0);
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    return expect(approximatelyEqual(left[0], 0.7071068f) && approximatelyEqual(engine.getBusPeak(0), 0.0f),
                  "bus mute removes only the bus return from the master");
}

bool testPostPanSendRouting()
{
    TrackDataModel model;
    const auto track = model.addTrack(TrackType::audio);
    const auto bus = model.addBus();
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 4);
    source->clear();
    source->setSample(0, 0, 1.0f);
    source->setSample(1, 0, 1.0f);
    model.addClipToTrack(0, {}, 0.0, source);
    model.setTrackSendRoute(track, 0, bus, 1.0f, false);
    model.setTrackPan(0, -1.0f);
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    model.setPlaying(true);
    model.setPlayheadPosition(0.0);
    float left[4] {}, right[4] {};
    float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    return expect(approximatelyEqual(engine.getBusPeak(0), 1.0f)
                      && approximatelyEqual(left[0], 2.0f)
                      && approximatelyEqual(right[0], 0.0f),
                  "post-fader sends receive the volume-and-pan processed stereo signal");
}

bool testSendAutomation()
{
    TrackDataModel model;
    const auto track = model.addTrack(TrackType::audio);
    const auto bus = model.addBus();
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 4);
    source->clear();
    source->setSample(0, 0, 1.0f);
    source->setSample(1, 0, 1.0f);
    model.addClipToTrack(0, {}, 0.0, source);
    if (! expect(model.setTrackSendRoute(track, 3, bus, 1.0f, true)
                 && model.upsertTrackSendAutomationPoint(0, 3, 0.0, 0.25f),
                 "send slot accepts an independent automation lane")) return false;

    const auto saved = model.createProjectState();
    const auto& savedLane = saved.tracks[0].automationLanes.back();
    if (! expect(savedLane.parameter == static_cast<int>(AutomationParameter::sendLevel) && savedLane.sendSlot == 3
                 && savedLane.points.size() == 1 && approximatelyEqual(savedLane.points.front().value, 0.25f),
                 "send automation persists with its sparse routing slot")) return false;

    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    model.setPlaying(true);
    float left[4] {}, right[4] {};
    float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(engine.getBusPeak(0), 0.25f),
                 "read-mode pre-fader send automation controls the realtime bus gain")) return false;

    model.setPlayheadPosition(0.0);
    if (! expect(model.setTrackSendAutomationMode(0, 3, AutomationMode::write)
                 && model.setTrackSendRoute(track, 3, bus, 0.60f, true),
                 "send automation write mode accepts UI route-level changes")) return false;
    const auto* lane = model.getTrackSendAutomationLane(0, 3);
    return expect(lane != nullptr && lane->getPoints().size() == 1 && approximatelyEqual(lane->getPoints().front().value, 0.60f),
                  "send automation replaces the point at the current playhead without reallocating routing slots");
}

bool testWaveformThumbnailCache()
{
    auto source = std::make_shared<juce::AudioBuffer<float>>(1, 8);
    source->clear();
    source->setSample(0, 2, -0.75f);
    source->setSample(0, 3, 0.50f);
    const auto sourceFile = juce::File("C:/StudioForgeTests/vocal.wav");
    WaveformThumbnailCache cache;
    const auto first = cache.prepare(sourceFile, source);
    const auto second = cache.prepare(sourceFile, source);
    if (! expect(first != nullptr && first == second, "duplicate clips reuse one source waveform thumbnail")) return false;
    const auto peak = first->peakForSourceRange(2.0, 3.0);
    return expect(peak.minimum < -0.74f && peak.maximum <= 0.01f,
                  "waveform thumbnail maps a clip source offset to prepared source peaks");
}

bool testMasterFxRouting()
{
    TrackDataModel model;
    const auto track = model.addTrack();
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 4);
    source->clear(); source->addSample(0, 0, 1.0f); source->addSample(1, 0, 1.0f);
    model.addClipToTrack(model.getTrackIndex(track), {}, 0.0, source);
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.setMasterFxProcessor(0, std::make_shared<GainUtilityProcessor>());
    engine.audioDeviceAboutToStart(&device);
    model.setPlaying(true); model.setPlayheadPosition(0.0);
    float left[4] {}, right[4] {}; float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(left[0], 0.3535534f) && approximatelyEqual(right[0], 0.3535534f)
                 && approximatelyEqual(engine.getMasterLeftPeak(), 0.3535534f)
                 && approximatelyEqual(engine.getMasterRightPeak(), 0.3535534f),
                 "master FX processes the summed stereo output before its meter")) return false;
    engine.setMasterFxBypassed(0, true);
    model.setPlayheadPosition(0.0);
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    return expect(approximatelyEqual(left[0], 0.7071068f) && approximatelyEqual(right[0], 0.7071068f),
                  "master FX bypass leaves the summed output unprocessed");
}

bool testMidiCoreScheduling()
{
    MidiClipState clip { { 1 }, { 1 }, 100.0, { { { 1 }, 64, 0.8f, 10.0, 20.0, 2 } } };
    MidiEventBuffer events;
    MidiScheduler::scheduleBlock({ clip }, 100.0, 16, events);
    if (! expect(events.size() == 1 && events[0].noteOn && events[0].sampleOffset == 10 && events[0].pitch == 64,
                 "MIDI scheduler emits note-on at clip-relative sample")) return false;
    MidiScheduler::scheduleBlock({ clip }, 120.0, 16, events);
    return expect(events.size() == 1 && !events[0].noteOn && events[0].sampleOffset == 10
                  && MidiScheduler::quantizeSample(123.0, 24.0) == 120.0,
                  "MIDI scheduler emits note-off and quantizes samples");
}

bool testMidiModelToEngineScheduling()
{
    TrackDataModel model;
    const auto track = model.addTrack();
    const auto clip = model.addMidiClip(track, 0.0);
    model.addMidiNote(clip, 60, 1.0f, 1.0, 2.0, 1);
    OfflineAudioDevice device;
    AudioEngine engine(&model); engine.audioDeviceAboutToStart(&device);
    model.setPlaying(true); model.setPlayheadPosition(0.0);
    float left[4] {}, right[4] {}; float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    return expect(engine.getScheduledMidiEventCount() == 2,
                  "engine schedules immutable MIDI model snapshot during audio block");
}

bool testMidiClipEditing()
{
    TrackDataModel model;
    const auto track = model.addTrack(TrackType::instrument);
    const auto clip = model.addMidiClip(track, 0.0);
    const auto note = model.addMidiNote(clip, 60, 0.8f, 13.0, 12.0, 1);
    if (! expect(note.isValid() && model.quantizeMidiClip(clip, 24.0)
                 && model.transposeMidiClip(clip, 2),
                 "MIDI clip accepts domain quantize and transpose edits")) return false;
    const auto& edited = model.getMidiClips().front().notes.front();
    if (! expect(edited.startSample == 24.0 && edited.pitch == 62,
                 "MIDI edit operations quantize time and clamp pitch through the model")) return false;
    return expect(model.undo() && model.getMidiClips().front().notes.front().pitch == 60
                  && model.undo() && model.getMidiClips().front().notes.front().startSample == 13.0,
                  "MIDI clip bulk edits retain undo history");
}

bool testMidiInstrumentPath()
{
    TrackDataModel model;
    const auto track = model.addTrack(TrackType::instrument);
    const auto clip = model.addMidiClip(track, 0.0);
    model.addMidiNote(clip, 69, 1.0f, 0.0, 4.0, 1);
    OfflineAudioDevice device;
    AudioEngine engine(&model); engine.audioDeviceAboutToStart(&device);
    model.setPlaying(true);
    float left[4] {}, right[4] {}; float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    return expect(std::abs(left[1]) > 0.001f && approximatelyEqual(left[1], right[1]),
                  "MIDI event drives internal instrument through track mixer path");
}

bool testTrackTypeCreation()
{
    TrackDataModel model;
    if (! expect(model.getTrackCount() == 0, "new model begins with no tracks")) return false;
    const auto audio = model.addTrack(TrackType::audio);
    const auto instrument = model.addTrack(TrackType::instrument);
    const auto external = model.addTrack(TrackType::externalMidi);
    return expect(audio.isValid() && instrument.isValid() && external.isValid()
                  && model.getTrack(0).type == TrackType::audio
                  && model.getTrack(1).type == TrackType::instrument
                  && model.getTrack(2).type == TrackType::externalMidi,
                  "track creation retains explicit audio, instrument, and external MIDI types");
}

bool testMidiReachesTrackProcessor()
{
    TrackDataModel model;
    const auto track = model.addTrack();
    const auto clip = model.addMidiClip(track, 0.0);
    model.addMidiNote(clip, 64, 0.8f, 1.0, 2.0, 1);
    auto probe = std::make_shared<MidiProbeProcessor>();
    AudioEngine engine(&model);
    engine.setFxProcessor(0, 0, probe);
    OfflineAudioDevice device;
    engine.audioDeviceAboutToStart(&device);
    engine.setPlaybackState(true);
    model.setPlaying(true);
    float left[4] {}, right[4] {};
    float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    return expect(probe->receivedEvents == 2, "scheduled note-on/off reaches the track processor");
}

bool testMidiFxChain()
{
    TrackDataModel model;
    const auto track = model.addTrack(TrackType::instrument);
    const auto clip = model.addMidiClip(track, 0.0);
    model.addMidiNote(clip, 60, 0.8f, 0.0, 4.0, 1);
    auto transpose = std::make_shared<MidiTransposeProcessor>(12);
    auto probe = std::make_shared<MidiPitchProbeProcessor>();
    AudioEngine engine(&model);
    engine.setFxProcessor(0, 0, transpose);
    engine.setFxProcessor(0, 1, probe);
    OfflineAudioDevice device;
    engine.audioDeviceAboutToStart(&device);
    model.setPlaying(true);
    float left[4] {}, right[4] {};
    float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    return expect(probe->receivedPitch == 72,
                  "MIDI FX runs before the instrument and downstream audio plug-ins receive transformed MIDI");
}

bool testMidiFxPersistence()
{
    const auto projectFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("StudioForgeMidiFxPersistence.sfproj");
    const auto catalogFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("StudioForgeMidiFxCatalog.xml");
    projectFile.deleteFile();
    catalogFile.deleteFile();

    TrackDataModel source;
    source.addTrack(TrackType::instrument);
    AudioEngine sourceEngine(&source);
    sourceEngine.setFxProcessor(0, 0, std::make_shared<MidiTransposeProcessor>(7));
    if (! expect(ProjectSerializer::save(source, projectFile).wasOk(), "project saves built-in MIDI FX state")) return false;

    TrackDataModel restored;
    PluginHostService pluginHost(catalogFile);
    if (! expect(ProjectSerializer::load(restored, pluginHost, projectFile).wasOk(),
                 "project restores built-in MIDI FX state")) return false;
    const auto* rack = restored.getFxRackSnapshot(0);
    if (! expect(rack != nullptr && rack->processors[0] != nullptr && rack->processors[0]->isMidiEffect(),
                 "restored MIDI FX is published to the immutable rack")) return false;

    juce::AudioBuffer<float> buffer(2, 1);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    rack->processors[0]->processBlock(buffer, midi);
    juce::MidiBuffer::Iterator iterator(midi);
    juce::MidiMessage message;
    int samplePosition = 0;
    const auto restoredCorrectly = iterator.getNextEvent(message, samplePosition) && message.getNoteNumber() == 67;
    projectFile.deleteFile();
    catalogFile.deleteFile();
    return expect(restoredCorrectly, "restored MIDI FX retains its transpose parameter");
}

bool testPluginHostFoundation()
{
    const auto catalog = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("StudioForgePluginHostFoundation.xml");
    catalog.deleteFile();
    PluginHostService host(catalog);
    const auto result = host.scanVst3(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                      .getChildFile("StudioForge-not-a-plugin.vst3"));
    return expect(result.failed(), "plugin scan rejects a missing VST3 without entering the audio path")
        && expect(host.getKnownPlugins().isEmpty(), "failed scan does not publish a plugin type");
}

bool testVst3EffectIntegration()
{
    const juce::File pluginFile { STUDIOFORGE_TEST_VST3_PATH };
    if (! expect(pluginFile.exists(), "controlled VST3 test effect was built")) return false;

    const auto catalog = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("StudioForgeVst3Catalog.xml");
    catalog.deleteFile();
    PluginHostService host(catalog);
    if (! expect(host.scanVst3(pluginFile).wasOk(), "VST3 effect scans successfully")) return false;
    const auto plugins = host.getKnownPlugins();
    if (! expect(plugins.size() == 1, "VST3 scan returns the controlled effect metadata")) return false;
    PluginHostService restoredCatalog(catalog);
    if (! expect(restoredCatalog.findKnownPlugins("test effect").size() == 1,
                 "plugin catalog reloads searchable VST3 metadata")) return false;

    juce::String error;
    auto effect = host.createEffect(plugins.getFirst(), 44100.0, 4, error);
    if (! expect(effect != nullptr, "VST3 effect instantiates and prepares")) return false;
    juce::AudioBuffer<float> buffer(2, 4);
    buffer.clear();
    buffer.setSample(0, 0, 1.0f); buffer.setSample(1, 0, 1.0f);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    effect->processBlock(buffer, midi);
    if (! expect(approximatelyEqual(buffer.getSample(0, 0), 0.25f)
                 && approximatelyEqual(buffer.getSample(1, 0), 0.25f),
                 "audio and MIDI reach the instantiated VST3 effect")) return false;

    if (! expect(effect->setParameterValue(0, 0.5f), "VST3 parameter changes through the host adapter")) return false;
    buffer.clear(); buffer.setSample(0, 0, 1.0f); buffer.setSample(1, 0, 1.0f);
    effect->processBlock(buffer, midi);
    if (! expect(approximatelyEqual(buffer.getSample(0, 0), 0.5f)
                 && approximatelyEqual(buffer.getSample(1, 0), 0.5f),
                 "VST3 applied state changes source DSP behaviour")) return false;
    juce::MemoryBlock capturedState;
    if (! expect(effect->getState(capturedState), "VST3 state can be captured")) return false;
    auto reloaded = host.createEffect(plugins.getFirst(), 44100.0, 4, error);
    if (! expect(reloaded != nullptr && reloaded->setState(capturedState.getData(), capturedState.getSize()),
                 "VST3 instance can restore captured state")) return false;
    buffer.clear(); buffer.setSample(0, 0, 1.0f); buffer.setSample(1, 0, 1.0f);
    reloaded->processBlock(buffer, midi);
    if (! expect(approximatelyEqual(buffer.getSample(0, 0), 0.5f)
                 && approximatelyEqual(buffer.getSample(1, 0), 0.5f),
                 "restored VST3 state preserves DSP behaviour")) return false;

    TrackDataModel model;
    const auto trackA = model.addTrack();
    const auto trackB = model.addTrack();
    AudioEngine engine(&model);
    engine.setFxProcessor(static_cast<size_t>(model.getTrackIndex(trackA)), 0, effect);
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 4);
    source->clear();
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 4; ++sample)
            source->setSample(channel, sample, 0.5f);
    model.addClipToTrack(model.getTrackIndex(trackA), {}, 0.0, source);
    OfflineAudioDevice device;
    engine.audioDeviceAboutToStart(&device);
    model.setPlaying(true);
    float left[4] {}, right[4] {};
    float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(left[0], 0.1767767f) && approximatelyEqual(right[0], 0.1767767f),
                 "AudioEngine routes track audio through the VST3 FX slot exactly once")) return false;
    if (! expect(model.reorderTrack(trackA, 1), "track reorder succeeds with VST3 attached")) return false;
    const auto reorderedIndex = static_cast<size_t>(model.getTrackIndex(trackA));
    if (! expect(model.getFxRackSnapshot(reorderedIndex)->processors[0] == effect,
                 "VST3 ownership remains with its TrackId after reorder")) return false;
    const auto projectFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("StudioForgePluginStateRoundTrip.sfproj");
    projectFile.deleteFile();
    if (! expect(ProjectSerializer::save(model, projectFile).wasOk(), "project saves VST3 rack identity and state")) return false;
    TrackDataModel restoredModel;
    juce::StringArray unavailableReferences;
    if (! expect(ProjectSerializer::load(restoredModel, host, projectFile, &unavailableReferences).wasOk(),
                 "project loads with VST3 rack restoration")) return false;
    const auto restoredIndex = static_cast<size_t>(restoredModel.getTrackIndex(trackA));
    const auto* restoredRack = restoredModel.getFxRackSnapshot(restoredIndex);
    if (! expect(restoredRack != nullptr && restoredRack->processors[0] != nullptr && unavailableReferences.isEmpty(),
                 "project restore republishes the saved VST3 rack")) return false;
    buffer.clear(); buffer.setSample(0, 0, 1.0f); buffer.setSample(1, 0, 1.0f);
    restoredRack->processors[0]->processBlock(buffer, midi);
    if (! expect(approximatelyEqual(buffer.getSample(0, 0), 0.5f)
                 && approximatelyEqual(buffer.getSample(1, 0), 0.5f),
                 "restored project VST3 rack preserves parameter state")) return false;
    projectFile.deleteFile();
    engine.setFxProcessor(reorderedIndex, 0, nullptr);
    return expect(model.getFxRackSnapshot(reorderedIndex)->processors[0] == nullptr,
                  "VST3 remove releases the FX slot without a dangling pointer")
        && expect(trackB.isValid(), "unrelated track remains valid during VST3 lifecycle");
}

bool testOfflineAudioEnginePath()
{
    TrackDataModel model;
    model.ensureTrackCount(2);
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    if (! expect(std::abs(model.getSampleRate() - 44100.0) < 1.0e-9, "device sample rate reaches model")) return false;
    engine.setTempo(137.0);
    if (! expect(std::abs(model.getBpm() - 137.0) < 1.0e-9, "engine tempo updates transport model")) return false;
    engine.setPlaybackState(false);
    if (! expect(!model.isPlaying(), "engine stop updates transport model")) return false;
    engine.setPlaybackState(true);
    if (! expect(model.isPlaying(), "engine play updates transport model")) return false;

    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 4);
    for (int sample = 0; sample < 4; ++sample)
    {
        source->setSample(0, sample, static_cast<float>(sample + 1));
        source->setSample(1, sample, static_cast<float>(sample + 1));
    }
    model.addClipToTrack(0, {}, 0.0, source);
    model.setTrackVolume(0, 1.0f);
    model.setTrackPan(0, 0.0f);
    model.setCycle(1.0, 3.0);
    model.setPlayheadPosition(1.0);
    model.setPlaying(true);
    engine.setMasterGain(0.5f);

    float left[4] {};
    float right[4] {};
    float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(left[0], 0.7071068f) && approximatelyEqual(left[1], 1.0606602f)
                 && approximatelyEqual(left[2], 0.7071068f) && approximatelyEqual(left[3], 1.0606602f),
                 "engine renders loop boundary inside one block")) return false;
    if (! expect(approximatelyEqual(static_cast<float>(model.getPlayheadPosition()), 1.0f),
                 "engine publishes wrapped playhead")) return false;
    if (! expect(approximatelyEqual(engine.getTrackPeak(0), 2.1213203f), "track meter is pre-master signal")) return false;

    model.clearCycle();
    model.setPlayheadPosition(0.0);
    engine.setMasterGain(1.0f);
    model.setTrackPan(0, 1.0f);
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(left[0], 0.0f) && approximatelyEqual(right[0], 1.0f),
                 "engine applies track pan before master")) return false;

    model.setTrackMuted(0, true);
    model.setPlayheadPosition(0.0);
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(left[0], 0.0f) && approximatelyEqual(right[0], 0.0f)
                 && approximatelyEqual(engine.getTrackPeak(0), 0.0f),
                 "engine mute affects audio and track meter")) return false;

    auto soloSource = std::make_shared<juce::AudioBuffer<float>>(2, 4);
    soloSource->clear();
    soloSource->addSample(0, 0, 0.25f);
    soloSource->addSample(1, 0, 0.25f);
    model.addClipToTrack(1, {}, 0.0, soloSource);
    model.setTrackMuted(0, false);
    model.setTrackPan(0, 0.0f);
    model.setTrackSolo(1, true);
    model.setPlayheadPosition(0.0);
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(left[0], 0.1767767f) && approximatelyEqual(right[0], 0.1767767f),
                 "solo excludes non-soloed tracks across mixer")) return false;
    if (! expect(approximatelyEqual(engine.getTrackPeak(0), 0.0f)
                 && approximatelyEqual(engine.getTrackPeak(1), 0.1767767f),
                 "solo updates independent track meters")) return false;

    model.setTrackSoloSafe(0, true);
    model.setPlayheadPosition(0.0);
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(left[0], 0.8838835f) && approximatelyEqual(right[0], 0.8838835f),
                 "solo-safe track remains audible with an independently soloed track")) return false;
    return expect(approximatelyEqual(engine.getTrackPeak(0), 2.8284271f)
                      && approximatelyEqual(engine.getTrackPeak(1), 0.1767767f),
                  "solo-safe keeps independent track meters correct");
}

#ifdef STUDIOFORGE_PERF_BENCHMARK
bool testRealtimeHostBenchmark()
{
    constexpr int blockSize = 64;
    constexpr int iterations = 2000;
    TrackDataModel model;
    std::array<BusId, TrackDataModel::maxBuses> buses {};
    for (auto& bus : buses) bus = model.addBus();
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, blockSize);
    source->clear();
    for (int sample = 0; sample < blockSize; ++sample) { source->setSample(0, sample, 0.25f); source->setSample(1, sample, 0.25f); }
    for (size_t track = 0; track < TrackDataModel::maxTracks; ++track)
    {
        const auto id = model.addTrack(TrackType::audio);
        model.addClipToTrack(static_cast<int>(track), {}, 0.0, source);
        for (size_t route = 0; route < TrackDataModel::maxSendsPerTrack; ++route)
            if (! model.setTrackSendRoute(id, route, buses[route], 0.125f, (route & 1u) == 0)) return false;
    }
    OfflineAudioDevice device(blockSize);
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    model.setPlaying(true);
    std::array<float, blockSize> left {}, right {};
    float* outputs[] { left.data(), right.data() };
    // Prime the realtime path before measuring steady-state processing. This
    // keeps one-time SIMD/runtime initialisation out of the callback budget.
    for (int warmup = 0; warmup < 4; ++warmup)
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, blockSize, {});
    studioForgeAllocationCount.store(0, std::memory_order_relaxed);
    const auto started = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        model.setPlayheadPosition(0.0);
        studioForgeMeasureAllocations.store(true, std::memory_order_relaxed);
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, blockSize, {});
        studioForgeMeasureAllocations.store(false, std::memory_order_relaxed);
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto milliseconds = std::chrono::duration<double, std::milli>(elapsed).count() / iterations;
    const auto headroom = 100.0 * (1.333 - milliseconds) / 1.333;
    const auto allocations = studioForgeAllocationCount.load(std::memory_order_relaxed);
    std::cout << "StudioForge host benchmark: " << milliseconds << " ms/block, headroom " << headroom << "%, allocations " << allocations << "\n";
    return expect(allocations == 0, "audio callback host allocation count is zero")
        && expect(milliseconds < 0.67, "host callback remains below 0.67 ms at 64 samples");
}
#endif
}

bool testVocalistAndAuxPluginSnapshots()
{
    TrackDataModel model;
    AudioEngine engine(&model);
    uint32_t vocalistId = 0;
    if (! expect(engine.addVocalist(0, vocalistId), "vocalist plugin test creates vocalist")) return false;
    if (! expect(engine.setVocalistFxProcessor(0, 0, std::make_shared<GainUtilityProcessor>()),
                 "vocalist plugin snapshot accepts processor")) return false;
    if (! expect(engine.setAuxFxProcessor(0, 0, std::make_shared<GainUtilityProcessor>()),
                 "aux plugin snapshot accepts processor")) return false;
    if (! expect(engine.setVocalistSend(0, 0.5f), "vocalist send reaches shared aux")) return false;

    return expect(engine.getVocalistFxProcessor(0, 0) != nullptr,
                  "vocalist plugin snapshot remains published after aux configuration");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    const auto passed = testTrackMixing()
        && testGlobalScaleContext()
        && testVocalistProcessor()
        && testVocalistRackSnapshots()
        && testAuxRoutingSnapshots()
        && testProjectTemplates()
        && testProjectTemplateStore()
        && testProjectAlternativeStore()
        && testMetronomeRendering()
        && testMidiFileImport()
        && runAutomationDomainTests()
        && testInspectorViewStateBuilder()
        && testTrackAutomationSnapshots()
        && testRecordArmTargetSelection()
        && testProjectRevision()
        && testMasterGraph()
        && testTransportLoopBoundary()
        && testModelSampleRate()
        && testTrackCapacity()
        && testTrackReadDoesNotCreateTracks()
        && testRealtimeSnapshotOwnership()
        && testRealtimeSnapshotReclaim()
        && testStableTrackAndClipIdentity()
        && testPlaybackStructuralSnapshots()
        && testProjectPersistence()
        && testAutosaveRecovery()
        && testRecentProjectsStore()
        && testMediaReloadAndRelink()
        && testUndoRedoHistory()
        && testAutomationWriteModes()
        && testRecordingFoundation()
        && testPunchRecordingCapture()
        && testInputMonitoring()
        && testMidiInputMonitoring()
        && testMidiInputRoutesToArmedInstrument()
        && testMidiRecordingFoundation()
        && testOfflineBounce()
        && testMidiOnlyOfflineBounce()
        && testClipGainAndFades()
        && testClipDuplicate()
        && testAudioMediaSourceOwnership()
        && testBusAndSendRouting()
        && testPostPanSendRouting()
        && testSendAutomation()
        && testWaveformThumbnailCache()
        && testMasterFxRouting()
        && testMidiCoreScheduling()
        && testMidiModelToEngineScheduling()
        && testMidiClipEditing()
        && testMidiInstrumentPath()
        && testTrackTypeCreation()
        && testMidiReachesTrackProcessor()
        && testMidiFxChain()
        && testMidiFxPersistence()
        && testPluginHostFoundation()
        && testVocalistAndAuxPluginSnapshots()
        && testVst3EffectIntegration()
        && testOfflineAudioEnginePath();
#ifdef STUDIOFORGE_PERF_BENCHMARK
    const auto benchmarkPassed = testRealtimeHostBenchmark();
    return passed && benchmarkPassed ? 0 : 1;
#else
    return passed ? 0 : 1;
#endif
}
