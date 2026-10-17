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
#include "AudioEngine/AuxRoutingPlan.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/GainUtilityProcessor.h"
#include "AudioEngine/LivePerformanceAssessment.h"
#include "AudioEngine/TrackMixing.h"
#include "AudioEngine/TransportUtils.h"
#include "AudioEngine/GlobalScaleContext.h"
#include "Control/StudioForgeControlBridge.h"
#include "Control/PluginSemanticMapper.h"
#include "Control/PerformanceMacroService.h"
#include "Control/PerformanceSurfaceProtocol.h"
#include "Control/MusicalKey.h"
#include "Control/VibeAutotuneAdapter.h"
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
#include "Project/ProjectBackupStore.h"
#include "UI/Inspector/InspectorViewState.h"
#include "UI/AudioRegionInteraction.h"
#include "UI/TimelineNavigation/TimelineViewportController.h"
#include "UI/TimelineNavigation/TimelineZoomPolicy.h"
#include "AutomationDomainTests.h"

namespace
{
bool expect(bool condition, const char* message);

bool testAudioRegionHitTesting()
{
    const AudioRegionBounds bounds { 100.0f, 20.0f, 200.0f, 60.0f };
    return expect(detectAudioRegionMouseZone(102.0f, 30.0f, bounds) == AudioRegionMouseZone::leftTrim,
                  "audio region upper-left resolves to left trim")
        && expect(detectAudioRegionMouseZone(298.0f, 30.0f, bounds) == AudioRegionMouseZone::rightTrim,
                  "audio region upper-right resolves to right trim")
        && expect(detectAudioRegionMouseZone(102.0f, 70.0f, bounds) == AudioRegionMouseZone::leftTrim,
                  "audio region lower-left resolves to left trim")
        && expect(detectAudioRegionMouseZone(298.0f, 70.0f, bounds) == AudioRegionMouseZone::rightTrim,
                  "audio region lower-right resolves to right trim")
        && expect(detectAudioRegionMouseZone(200.0f, 50.0f, bounds) == AudioRegionMouseZone::move,
                  "audio region body resolves to move")
        && expect(detectAudioRegionMouseZone(99.0f, 50.0f, bounds) == AudioRegionMouseZone::none,
                  "audio region outside resolves to none")
        && expect(applyAudioRegionModifiers(AudioRegionMouseZone::leftTrim, true) == AudioRegionMouseZone::fadeIn
                  && applyAudioRegionModifiers(AudioRegionMouseZone::rightTrim, true) == AudioRegionMouseZone::fadeOut
                  && applyAudioRegionModifiers(AudioRegionMouseZone::move, true) == AudioRegionMouseZone::move,
                  "audio region modifier policy exposes fades without changing moves")
        && expect(audioRegionCursorIntent(AudioRegionMouseZone::move) == AudioRegionCursorIntent::move
                  && audioRegionCursorIntent(AudioRegionMouseZone::leftTrim) == AudioRegionCursorIntent::trim
                  && audioRegionCursorIntent(AudioRegionMouseZone::fadeOut) == AudioRegionCursorIntent::fade,
                  "audio region cursor intents map to their interaction zones")
        && expect(! audioRegionDragThresholdExceeded(0.0f, 0.0f, 3.0f, 0.0f)
                  && audioRegionDragThresholdExceeded(0.0f, 0.0f, 4.0f, 0.0f),
                  "audio region drag threshold is deterministic");
}

bool testTimelineViewportNavigation()
{
    const TimelineViewportState initial { 1000.0, 2.0f, 72, 1.0f };
    const auto pivot = 48000.0;
    const auto zoomed = TimelineZoomPolicy::zoomAroundSample(initial, pivot, 4.0f, 80.0, 48000.0, 240.0f);
    const auto sampleAtPivot = zoomed.viewStartSample + 240.0 / (80.0 * zoomed.horizontalZoom) * 48000.0;
    TimelineViewportController controller(initial);
    controller.setState(zoomed);
    auto waveformZoomed = zoomed;
    waveformZoomed.waveformZoom = 4.0f;
    controller.setState(waveformZoomed);
    return expect(std::abs(sampleAtPivot - pivot) < 0.01, "timeline zoom keeps pointer pivot stable")
        && expect(controller.back() && controller.state().approximatelyEquals(zoomed), "timeline navigation restores prior viewport")
        && expect(controller.forward() && controller.state().approximatelyEquals(waveformZoomed), "timeline navigation retains independent waveform amplitude zoom");
}

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

bool testLivePerformanceAssessment()
{
    const auto ready = assessLivePerformance({ 48000.0, 96, 96, 0, 0.42f, 0 });
    const auto caution = assessLivePerformance({ 48000.0, 512, 512, 0, 0.42f, 0 });
    const auto overloaded = assessLivePerformance({ 48000.0, 96, 96, 0, 0.91f, 1 });
    return expect(ready.rating == LivePerformanceRating::ready
                      && std::abs(ready.roundTripMilliseconds - 4.0) < 1.0e-6,
                  "LIVE check accepts a low-latency, stable path")
        && expect(caution.rating == LivePerformanceRating::caution,
                  "LIVE check warns when reported latency is too high")
        && expect(overloaded.rating == LivePerformanceRating::notReady,
                  "LIVE check rejects callback overloads");
}

bool testProjectTemplates()
{
    const auto empty = ProjectTemplates::create(ProjectTemplate::empty);
    if (! expect(empty.tracks.empty() && empty.tempoMap.size() == 1, "empty template has only a valid tempo map")) return false;

    const auto recording = ProjectTemplates::create(ProjectTemplate::audioRecording);
    if (! expect(recording.tracks.size() == 4 && std::all_of(recording.tracks.begin(), recording.tracks.end(),
        [] (const PersistedTrackState& track) { return track.type == TrackType::audio && track.id.isValid(); }),
        "audio recording template creates valid audio tracks")) return false;
    if (! expect(recording.controlRoutingProfile.routes.size() == 1
                      && recording.controlRoutingProfile.routes.front().target == ControlTarget::mic1
                      && recording.controlRoutingProfile.routes.front().track == recording.tracks.front().id,
                 "audio template routes pitch correction only to its Vocal track")) return false;

    const auto midi = ProjectTemplates::create(ProjectTemplate::midiProduction);
    TrackDataModel model;
    return expect(midi.tracks.size() == 3 && midi.tracks[0].type == TrackType::instrument
                      && midi.tracks[2].type == TrackType::externalMidi,
                  "MIDI production template creates instrument and external MIDI tracks")
        && expect(midi.controlRoutingProfile.routes.empty(),
                  "MIDI template does not route pitch correction to instruments")
        && expect(model.applyProjectState(midi).wasOk() && model.getTrackCount() == 3,
                  "template project state is accepted by the model");
}

class ControlBridgeTestEffect final : public AudioEffectProcessor
{
public:
    void prepareToPlay(double, int, int) override {}
    void processBlock(juce::AudioBuffer<float>&) override {}
    void releaseResources() override {}
    juce::String getName() const override { return "Control bridge test effect"; }
    bool setParameterText(const juce::String& identifier, const juce::String& value) override
    {
        if (identifier != "root" && identifier != "scale" && identifier != "RootNote" && identifier != "Mode") return false;
        values.set(identifier, value);
        return true;
    }
    juce::String getParameterText(const juce::String& identifier) const override { return values[identifier]; }
    std::vector<EffectParameterDescriptor> getParameterDescriptors() const override
    {
        return { { "RootNote", "Key Root" }, { "Mode", "Scale Mode" } };
    }

private:
    juce::HashMap<juce::String, juce::String> values;
};

bool testStudioForgeControlBridge()
{
    TrackDataModel model;
    const auto mic1 = model.addTrack(TrackType::audio);
    const auto mic2 = model.addTrack(TrackType::audio);
    model.setFxProcessor(0, 0, std::make_shared<ControlBridgeTestEffect>());
    model.setFxProcessor(1, 0, std::make_shared<ControlBridgeTestEffect>());
    ControlRoutingProfile profile;
    for (const auto [target, track] : { std::pair { ControlTarget::mic1, mic1 }, std::pair { ControlTarget::mic2, mic2 } })
        profile.routes.push_back({ target, ControlEffect::pitchCorrection, track, 0, {}, "root", "scale" });
    if (! expect(model.setControlRoutingProfile(profile).wasOk(), "control routing profile accepts two mic routes")) return false;
    StudioForgeControlBridge bridge(model);
    const auto first = bridge.apply(ControlCommand::keySignatureFor(ControlTarget::mic1, 9, true));
    const auto second = bridge.apply(ControlCommand::keySignatureFor(ControlTarget::mic2, 9, true));
    if (! expect(first.succeeded && second.succeeded, "control bridge applies a key signature to both configured microphones")) return false;
    const auto* rack1 = model.getFxRackSnapshot(0);
    const auto* rack2 = model.getFxRackSnapshot(1);
    if (! expect(rack1->processors[0]->getParameterText("RootNote") == "A" && rack1->processors[0]->getParameterText("Mode") == "Natural Minor"
                 && rack2->processors[0]->getParameterText("RootNote") == "A", "control bridge resolves exposed plug-in parameter aliases")) return false;
    const auto missing = bridge.apply(ControlCommand::keySignatureFor(ControlTarget::music, 0, false));
    if (! expect(! missing.succeeded && missing.error.contains("No control route"), "control bridge reports a missing route")) return false;
    auto wrongPluginProfile = profile;
    wrongPluginProfile.routes.front().expectedPluginIdentifier = "com.example.not-vibe";
    if (! expect(model.setControlRoutingProfile(wrongPluginProfile).wasOk(), "strict test route is accepted")) return false;
    const auto wrongPlugin = bridge.apply(ControlCommand::keySignatureFor(ControlTarget::mic1, 0, false));
    if (! expect(! wrongPlugin.succeeded && wrongPlugin.error.contains("does not match"), "control bridge rejects a route targeting the wrong plug-in")) return false;
    const auto invalid = ControlCommand::keySignatureFor(ControlTarget::mic1, 12, false);
    return expect(! bridge.apply(invalid).succeeded, "control bridge rejects invalid key commands");
}

bool testPluginSemanticMapping()
{
    const std::vector<EffectParameterDescriptor> reverbParameters {
        { "return_level", "Return Level" }, { "mix", "Dry / Wet" }, { "rt60", "Reverb Time" }
    };
    const auto reverb = PluginSemanticMapper::propose("Modern Hall Reverb", reverbParameters);
    if (! expect(reverb.displayName == "Vocal Space" && reverb.targets.size() == 3
                 && ! reverb.needsReview && reverb.confidence >= 0.9f,
                 "semantic mapper produces a safe high-confidence reverb macro")) return false;

    const std::vector<EffectParameterDescriptor> delayParameters {
        { "output", "Output Level" }, { "feedback", "Feedback" }, { "tone", "Echo Tone" }
    };
    const auto delay = PluginSemanticMapper::propose("Studio Echo", delayParameters);
    const auto feedback = std::find_if(delay.targets.begin(), delay.targets.end(), [] (const SmartMacroTarget& target)
    {
        return target.semantic == PluginParameterSemantic::delayFeedback;
    });
    if (! expect(delay.displayName == "Lead Delay" && feedback != delay.targets.end()
                 && feedback->maximum <= 0.65f,
                 "semantic mapper caps generic delay feedback below unsafe ranges")) return false;

    const auto unknown = PluginSemanticMapper::propose("Opaque Processor", { { "Mode", "Mode" } });
    return expect(unknown.targets.empty() && unknown.needsReview && unknown.displayName == "Unassigned",
                  "semantic mapper leaves unknown plug-ins unassigned instead of guessing");
}

class PerformanceMacroTestEffect final : public AudioEffectProcessor
{
public:
    void prepareToPlay(double, int, int) override {}
    void processBlock(juce::AudioBuffer<float>&) override {}
    void releaseResources() override {}
    juce::String getName() const override { return "Studio Hall Reverb"; }
    juce::String getPersistentIdentifier() const override { return "com.studioforge.test-hall"; }
    bool setParameterValue(const juce::String& identifier, float value) override
    {
        if (identifier != "return" && identifier != "mix" && identifier != "decay") return false;
        values.set(identifier, value);
        return true;
    }
    float valueFor(const juce::String& identifier) const { return values[identifier]; }
    juce::String getParameterText(const juce::String& identifier) const override
    {
        return juce::String(values[identifier], 2);
    }
    std::vector<EffectParameterDescriptor> getParameterDescriptors() const override
    {
        return { { "return", "Return Level" }, { "mix", "Mix" }, { "decay", "Decay" } };
    }

private:
    juce::HashMap<juce::String, float> values;
};

bool testPerformanceMacroService()
{
    TrackDataModel model;
    const auto bus = model.addBus();
    auto effect = std::make_shared<PerformanceMacroTestEffect>();
    model.setBusFxProcessor(bus, 0, effect);
    PerformanceMacroService macros(model);

    const auto preview = macros.preview(bus, 0);
    if (! expect(preview.available && preview.proposal.displayName == "Vocal Space"
                 && preview.proposal.targets.size() == 3,
                 "performance macros preview the actual bus plug-in metadata")) return false;
    const auto applied = macros.apply(0, bus, 0);
    if (! expect(applied.succeeded && applied.snapshot.state == PerformanceMacroService::State::active,
                 "performance macro applies a high-confidence smart mapping")) return false;
    const auto changed = macros.setValue(0, 0.5f);
    if (! expect(changed.succeeded && effect->valueFor("return") > 0.0f && effect->valueFor("mix") > 0.0f,
                 "performance macro updates the selected plug-in through safe mapped targets")) return false;

    model.setBusFxBypassed(bus, 0, true);
    macros.refresh();
    if (! expect(macros.getSnapshot(0).state == PerformanceMacroService::State::bypassed,
                 "performance macro mirrors a bus plug-in bypass state")) return false;

    model.setBusFxProcessor(bus, 0, std::make_shared<ControlBridgeTestEffect>());
    macros.refresh();
    return expect(macros.getSnapshot(0).state == PerformanceMacroService::State::unassigned,
                  "performance macro never retargets a replacement plug-in by slot alone");
}

bool testPerformanceSurfaceProtocol()
{
    TrackDataModel model;
    const auto bus = model.addBus();
    auto effect = std::make_shared<PerformanceMacroTestEffect>();
    model.setBusFxProcessor(bus, 0, effect);
    PerformanceMacroService macros(model);
    PerformanceSurfaceProtocol protocol(model, macros, "local-test-token");

    const auto request = "{\"token\":\"local-test-token\",\"projectRevision\":\""
        + juce::String(model.getProjectRevision())
        + "\",\"command\":\"applyMacroMapping\",\"macroId\":0,\"busId\":\""
        + juce::String(bus.value) + "\",\"pluginSlot\":0,\"pluginInstanceId\":\""
        + effect->getPersistentIdentifier() + "\",\"confirmNeedsReview\":false}";
    const auto reply = juce::JSON::parse(protocol.handleCommand(request));
    const auto* object = reply.getDynamicObject();
    if (! expect(object != nullptr && static_cast<bool>(object->getProperty("ok")),
                 "performance surface accepts an authenticated semantic macro command")) return false;

    const auto payload = protocol.makeStatePayload();
    if (! expect(payload.contains("Vocal Space") && payload.contains("Return Level"),
                 "performance surface publishes DAW-owned macro names and parameter readouts")) return false;

    const auto unauthorised = protocol.handleCommand(request.replace("local-test-token", "wrong-token"));
    return expect(unauthorised.contains("not authorised"),
                  "performance surface rejects unauthorised control-surface commands");
}

bool testMusicalKeyNormalization()
{
    const auto dbMinor = MusicalKey::parse("Db", "aeolian");
    const auto abMajor = MusicalKey::parse("Ab", "ionian");
    if (! expect(dbMinor.has_value() && dbMinor->root == PitchClass::cSharp && dbMinor->isMinor(), "Db normalizes to C# minor")) return false;
    if (! expect(abMajor.has_value() && abMajor->root == PitchClass::gSharp && ! abMajor->isMinor(), "Ab normalizes to G# major")) return false;
    ControlBridgeTestEffect effect;
    const auto result = VibeAutotuneAdapter {}.apply(effect, *dbMinor, "root", "scale");
    return expect(result.succeeded && result.mapping.root == "RootNote" && result.mapping.scale == "Mode"
                      && effect.getParameterText("RootNote") == "C#" && effect.getParameterText("Mode") == "Natural Minor",
                  "Vibe adapter resolves metadata and applies canonical key");
}

bool testPerformanceSetupRouting()
{
    LiveSetupConfig setup;
    setup.sessionMode = LiveSessionMode::audioAndMidi;
    setup.vocalCount = 1;
    setup.guitarCount = 1;
    setup.bassCount = 1;
    setup.keyboardCount = 1;
    setup.drumCount = 1;
    const auto project = ProjectTemplates::createLiveSetup(setup);
    if (! expect(project.tracks.size() == 5, "performance setup creates every selected band role")) return false;
    if (! expect(project.controlRoutingProfile.routes.size() == 1
                      && project.controlRoutingProfile.routes.front().target == ControlTarget::mic1
                      && project.controlRoutingProfile.routes.front().track == project.tracks.front().id,
                 "single-vocal setup never routes Mic 2 pitch correction to Guitar")) return false;

    const auto hasRole = [&project] (const juce::String& name, TrackType type, int channel)
    {
        return std::any_of(project.tracks.begin(), project.tracks.end(), [&] (const PersistedTrackState& track)
        {
            return track.name.startsWith(name) && track.type == type && track.midiInputChannel == channel;
        });
    };
    return expect(hasRole("Guitar", TrackType::audio, 0) && hasRole("Bass", TrackType::audio, 0)
                      && hasRole("Keys", TrackType::instrument, 1) && hasRole("Drums", TrackType::instrument, 10),
                  "performance setup assigns audio and MIDI capabilities by band role");
}

bool testPerformanceSourceChoices()
{
    LiveSetupConfig setup;
    setup.sessionMode = LiveSessionMode::audioAndMidi;
    setup.guitarCount = 1;
    setup.bassCount = 1;
    setup.guitarSource = PerformanceInputSource::softwareInstrument;
    setup.bassSource = PerformanceInputSource::externalMidiHardware;
    const auto project = ProjectTemplates::createLiveSetup(setup);
    const auto hasTrack = [&project] (const juce::String& name, TrackType type, int midiChannel)
    {
        return std::any_of(project.tracks.begin(), project.tracks.end(), [&] (const PersistedTrackState& track)
        {
            return track.name.startsWith(name) && track.type == type && track.midiInputChannel == midiChannel;
        });
    };
    return expect(hasTrack("Guitar", TrackType::instrument, 1)
                      && hasTrack("Bass", TrackType::externalMidi, 2),
                  "performance source choices produce their requested audio engine track types");
}

bool testMidiFileImport()
{
    const auto file = juce::File::getCurrentWorkingDirectory()
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
    const auto directory = juce::File::getCurrentWorkingDirectory()
        .getNonexistentChildFile("StudioForgeTemplateStore", {}, true);
    ProjectTemplateStore store(directory);
    const auto templateFile = store.makeTemplateFile("My Recording Template");
    directory.createDirectory();
    const auto written = directory.isDirectory() && templateFile.replaceWithText("template");
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
    const auto directory = juce::File::getCurrentWorkingDirectory()
        .getNonexistentChildFile("StudioForgeAlternativeStore", {}, true);
    const auto projectFile = directory.getChildFile("Mix.studioforge");
    const auto rootWritten = directory.createDirectory() && projectFile.replaceWithText("project");
    ProjectAlternativeStore store;
    const auto alternativeFile = store.makeAlternativeFile(projectFile, "Vocal / Edit");
    const auto alternativeWritten = alternativeFile.getParentDirectory().createDirectory()
        && alternativeFile.replaceWithText("alternative");
    const auto alternatives = store.load(projectFile);
    const auto secondAlternative = store.makeAlternativeFile(projectFile, "Vocal / Edit");
    const auto valid = rootWritten && alternativeWritten
        && alternativeFile.hasFileExtension("studioforge")
        && alternativeFile.getParentDirectory().getFileName() == "Mix Alternatives"
        && secondAlternative != alternativeFile
        && alternatives.size() == 1
        && alternatives[0] == alternativeFile.getFullPathName();
    directory.deleteRecursively();
    return expect(valid, "alternative store isolates, avoids overwrites, and lists project alternatives");
}

bool testProjectBackupStore()
{
    const auto directory = juce::File::getCurrentWorkingDirectory()
        .getNonexistentChildFile("StudioForgeBackupStore", {}, true);
    const auto projectFile = directory.getChildFile("Mix.studioforge");
    if (! directory.createDirectory() || ! projectFile.replaceWithText("revision 0"))
        return expect(false, "project backup store prepares its project document");

    ProjectBackupStore store;
    auto capturedAllVersions = true;
    for (int revision = 1; revision <= ProjectBackupStore::maximumBackups + 2; ++revision)
    {
        capturedAllVersions = capturedAllVersions && store.captureCurrentVersion(projectFile).wasOk();
        capturedAllVersions = capturedAllVersions && projectFile.replaceWithText("revision " + juce::String(revision));
    }
    const auto backups = store.load(projectFile);
    const auto backupDirectory = projectFile.getParentDirectory().getChildFile("Mix Backups");
    const auto valid = capturedAllVersions
        && backups.size() == ProjectBackupStore::maximumBackups
        && backupDirectory.isDirectory()
        && juce::File(backups[backups.size() - 1]).loadFileAsString().startsWith("revision");
    directory.deleteRecursively();
    return expect(valid, "project backup store keeps a rolling history of the last ten manual save versions");
}

class OfflineAudioDevice final : public juce::AudioIODevice
{
public:
    explicit OfflineAudioDevice(int size = 4, int inputs = 2, int activeInputs = -1)
        : juce::AudioIODevice("Offline", "Test"), blockSize(size), inputCount(inputs),
          activeInputMask(activeInputs >= 0 ? activeInputs : (1 << inputs) - 1) {}

    juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
    juce::StringArray getInputChannelNames() override
    {
        juce::StringArray names;
        for (int channel = 0; channel < inputCount; ++channel)
            names.add("In " + juce::String(channel + 1));
        return names;
    }
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
    juce::BigInteger getActiveInputChannels() const override { return activeInputMask; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
private:
    int blockSize;
    int inputCount;
    int activeInputMask;
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
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
            {
                receivedPitch = message.getNoteNumber();
                break;
            }
        }
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

bool testMasterOutputState()
{
    TrackDataModel model;
    AudioEngine engine(&model);
    AudioEngine::MasterOutputState state;
    state.gain = 0.5f;
    state.pan = -0.35f;
    state.width = 1.6f;
    state.mono = true;
    state.dim = true;
    state.muted = true;
    engine.setMasterOutputState(state);
    const auto restored = engine.getMasterOutputState();
    return expect(approximatelyEqual(restored.gain, 0.5f)
                  && approximatelyEqual(restored.pan, -0.35f)
                  && approximatelyEqual(restored.width, 1.6f)
                  && restored.mono && restored.dim && restored.muted,
                  "master output controls round-trip through one state bundle");
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
    const auto projectFile = juce::File::getCurrentWorkingDirectory()
        .getChildFile("StudioForgeCorePersistenceTest.studioforge");
    const auto malformedFile = projectFile.getSiblingFile("StudioForgeCoreMalformed.studioforge");
    const auto unsupportedFile = projectFile.getSiblingFile("StudioForgeCoreUnsupported.studioforge");
    const auto sparseIdFile = projectFile.getSiblingFile("StudioForgeCoreSparseIds.studioforge");
    projectFile.deleteFile();
    malformedFile.deleteFile();
    unsupportedFile.deleteFile();
    sparseIdFile.deleteFile();

    TrackDataModel source;
    source.setSampleRate(48000.0);
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
    source.setBusName(bus, "Vocal Reverb");
    source.setBusBalance(bus, -0.4f);
    source.setTrackOutputBus(trackA, bus);
    source.setTrackSend(trackB, bus, 0.35f);
    source.setTrackSendPostFaderBeforePan(trackB, 0, true);
    source.setBpm(137.0);
    source.setTimeSignatureNumerator(7);
    source.setTimeSignatureDenominator(8);
    source.setTimeSignatureAtSample(192000.0, 6, 8);
    source.clearTempoMap();
    source.addTempoEvent(960.0, 90.0);
    source.setCycle(100.0, 500.0);
    source.setPunchRange(180.0, 420.0);
    source.addMarker(240.0, "Song A");
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
    ControlRoutingProfile controlProfile;
    controlProfile.routes.push_back({ ControlTarget::mic1, ControlEffect::pitchCorrection, trackA, 0, {}, "root", "scale" });
    if (! expect(source.setControlRoutingProfile(controlProfile).wasOk(), "control routing is accepted for persistence")) return false;
    const auto before = source.createProjectState();
    if (! expect(ProjectSerializer::save(source, projectFile).wasOk(), "project save succeeds")) return false;

    TrackDataModel loaded;
    juce::StringArray missingMedia;
    if (! expect(ProjectSerializer::load(loaded, projectFile, &missingMedia).wasOk(), "project load succeeds")) return false;
    const auto after = loaded.createProjectState();
    if (! expect(after.tracks.size() == before.tracks.size() && after.tempoMap.size() == before.tempoMap.size()
                 && approximatelyEqual(static_cast<float>(after.bpm), static_cast<float>(before.bpm))
                 && after.timeSignatureNumerator == before.timeSignatureNumerator
                 && after.timeSignatureDenominator == before.timeSignatureDenominator
                 && after.timeSignatureMap.size() == 2
                 && after.timeSignatureMap[1].numerator == 6 && after.timeSignatureMap[1].denominator == 8 && after.cycleActive
                 && after.cycleStartSample == before.cycleStartSample && after.cycleEndSample == before.cycleEndSample
                 && after.punchActive && after.punchInSample == before.punchInSample
                 && after.punchOutSample == before.punchOutSample && after.markers.size() == 1
                 && after.markers[0].samplePosition == 240.0 && after.markers[0].name == "Song A",
                 "transport and project state round-trip")) return false;
    if (! expect(after.tracks[0].id == trackC && after.tracks[1].id == trackA && after.tracks[2].id == trackB,
                 "track order and IDs round-trip")) return false;
    if (! expect(after.controlRoutingProfile.routes.size() == 1
                 && after.controlRoutingProfile.routes[0].track == trackA
                 && after.controlRoutingProfile.routes[0].parameterIdentifier == "root"
                 && after.controlRoutingProfile.routes[0].secondaryParameterIdentifier == "scale",
                 "control routing profile round-trips with the project")) return false;
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
    if (! expect(after.buses.size() == 1 && after.buses[0].id == bus && after.buses[0].name == "Vocal Reverb"
                 && approximatelyEqual(after.buses[0].balance, -0.4f)
                 && after.tracks[1].outputBus == bus && after.tracks[2].activeSendCount == 1
                 && after.tracks[2].sends[0].targetBus == bus && after.tracks[2].sends[0].postFaderBeforePan
                 && approximatelyEqual(after.tracks[2].sends[0].level, 0.35f),
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
    const auto temporaryRoot = juce::File::getCurrentWorkingDirectory()
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
    const auto staleTemporary = recoveredFile.getSiblingFile(recoveredFile.getFileName() + ".tmp");
    const auto staleWriterTemporary = staleTemporary.getSiblingFile(staleTemporary.getFileName() + ".write-tmp");
    staleTemporary.replaceWithText("stale recovery temporary");
    staleWriterTemporary.replaceWithText("stale writer temporary");
    autosave.markProjectSaved();
    const auto clearedOk = ! autosave.hasRecoverySnapshot()
        && ! staleTemporary.exists() && ! staleWriterTemporary.exists();
    temporaryRoot.deleteRecursively();
    return expect(recoveredOk, "recovery snapshot restores the project model")
        && expect(clearedOk, "successful explicit save clears recovery snapshots and stale temporary files");
}

bool testRecentProjectsStore()
{
    const auto directory = juce::File::getCurrentWorkingDirectory()
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
    const auto temporaryFile = storage.getSiblingFile(storage.getFileName() + ".tmp");
    if (! expect(! temporaryFile.exists(), "recent-project writes do not leave a temporary file")) return false;
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
    const auto tempDirectory = juce::File::getCurrentWorkingDirectory();
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

bool testMediaReloadCachesFailedSourcesPerBatch()
{
    const auto invalidMedia = juce::File::getCurrentWorkingDirectory()
        .getChildFile("studioforge-invalid-media-reload.wav");
    invalidMedia.deleteFile();
    if (! invalidMedia.replaceWithText("not a WAV file"))
        return expect(false, "media reload test writes an invalid source once");

    TrackDataModel model;
    model.ensureTrackCount(1);
    auto placeholder = std::make_shared<juce::AudioBuffer<float>>(1, 8);
    const auto first = model.addClipToTrack(0, invalidMedia, 0.0, placeholder);
    const auto second = model.addClipToTrack(0, invalidMedia, 16.0, placeholder);
    const auto report = MediaReloadService::reloadProjectMedia(model);
    const auto valid = first.isValid() && second.isValid()
        && model.getClipMediaStatus(first) == AudioMediaStatus::DecodeFailed
        && model.getClipMediaStatus(second) == AudioMediaStatus::DecodeFailed
        && report.failedSourceCount == 1 && report.decodeFailures.size() == 1;
    invalidMedia.deleteFile();
    return expect(valid, "media reload tries a failed shared source once and marks every dependent clip safely");
}

bool testImmediateAudioImportPlaceholder()
{
    TrackDataModel model;
    const auto track = model.addTrack(TrackType::audio);
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("studioforge-import-placeholder.wav");
    const auto clip = model.addUnloadedClipToTrack(model.getTrackIndex(track), file, 960.0, 48000.0);
    if (! expect(clip.isValid() && model.getTrack(0).clips.size() == 1
                 && model.getTrack(0).clips.front().durationSamples == 48000.0
                 && model.getClipMediaStatus(clip) == AudioMediaStatus::Unloaded,
                 "audio import makes a visible unloaded region before decode")) return false;

    auto decoded = std::make_shared<juce::AudioBuffer<float>>(2, 24000);
    decoded->clear();
    return expect(model.setClipMediaResource(clip, file, decoded)
                      && model.getClipMediaStatus(clip) == AudioMediaStatus::Ready
                      && model.getTrack(0).clips.front().durationSamples == 48000.0,
                  "decoded media resolves the existing placeholder without changing timeline duration");
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
    const auto recordingFile = juce::File::getCurrentWorkingDirectory()
        .getChildFile("StudioForgePhase7Recording.wav");
    recordingFile.deleteFile();
    TrackDataModel model;
    model.ensureTrackCount(1);
    // The default live track is mono. This test deliberately verifies a
    // stereo take, so make the intended physical route explicit.
    model.setTrackInputConfiguration(0, false, false, 0, 2);
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
    if (! expect(recordingFile.replaceWithText("existing take is preserved until recording completes"),
                 "recording test prepares an existing destination")) return false;
    if (! expect(engine.startRecording(0, recordingFile).wasOk()
                 && engine.stopRecording().wasOk() && model.getTrack(0).clips.size() == clipCount
                 && recordingFile.loadFileAsString() == "existing take is preserved until recording completes",
                 "empty recording leaves an existing take untouched")) return false;
    if (! expect(engine.startRecording(0, recordingFile).wasOk(),
                 "recording can begin before a benign device renegotiation")) return false;
    engine.audioDeviceStopped();
    engine.audioDeviceAboutToStart(&device);
    if (! expect(! engine.consumeRecordingInterruptedByDeviceDisconnect() && engine.isRecording()
                 && engine.stopRecording().wasOk(),
                 "device stop/start renegotiation does not falsely abort a recording take")) return false;
    recordingFile.deleteFile();
    return true;
}

bool testRecordedTakeGroups()
{
    TrackDataModel model;
    model.addTrack(TrackType::audio);
    auto firstPass = std::make_shared<juce::AudioBuffer<float>>(2, 64);
    auto secondPass = std::make_shared<juce::AudioBuffer<float>>(2, 64);
    firstPass->clear();
    secondPass->clear();

    const auto firstTake = model.addRecordedClipToTrack(0, {}, 0.0, firstPass);
    const auto secondTake = model.addRecordedClipToTrack(0, {}, 0.0, secondPass);
    const auto& takes = model.getTrack(0).clips;
    if (! expect(firstTake.isValid() && secondTake.isValid() && takes.size() == 2,
                 "overdub recording preserves both passes")) return false;
    if (! expect(takes[0].recordedTake && takes[1].recordedTake && takes[0].takeGroupId != 0
                 && takes[0].takeGroupId == takes[1].takeGroupId && ! takes[0].takeActive && takes[1].takeActive,
                 "latest overlapping recording becomes the active take")) return false;
    if (! expect(model.selectActiveTake(firstTake) && takes[0].takeActive && ! takes[1].takeActive,
                 "an earlier recorded take can be reselected without deleting alternatives")) return false;
    model.splitAudioClip(0, 0, 32.0);
    const auto& splitTakes = model.getTrack(0).clips;
    if (! expect(splitTakes.size() == 3 && splitTakes[0].takeId == splitTakes[1].takeId
                 && splitTakes[0].takeActive && splitTakes[1].takeActive,
                 "splitting an active take keeps its fragments as one active pass")) return false;
    const auto folderId = model.getTakeFolders(0).front().id;
    if (! expect(model.selectTakeFolderRange(folderId, splitTakes[2].takeId, 0.0, 16.0),
                 "quick swipe selection replaces a comp time range with another take")) return false;

    TrackDataModel restored;
    const auto savedState = model.createProjectState();
    if (! expect(savedState.tracks[0].takeFolders.size() == 1
                 && savedState.tracks[0].takeFolders[0].state.comps.size() == 1
                 && savedState.tracks[0].takeFolders[0].state.comps[0].segments.size() == 2
                 && savedState.tracks[0].takeFolders[0].state.comps[0].segments[0].sourceTakeId == splitTakes[2].takeId
                 && savedState.tracks[0].takeFolders[0].state.activeCompId.isValid(),
                 "overdub recordings form a persisted take folder with an active comp")) return false;
    if (! expect(restored.applyProjectState(savedState).wasOk(),
                 "take grouping survives the project model round-trip")) return false;
    const auto& restoredTakes = restored.getTrack(0).clips;
    return expect(restoredTakes.size() == 3 && restoredTakes[0].takeGroupId == splitTakes[0].takeGroupId
                  && restoredTakes[0].takeActive && restoredTakes[1].takeActive && ! restoredTakes[2].takeActive,
                  "active take selection survives project restore");
}

bool testMultiTrackRecording()
{
    const auto directory = juce::File::getCurrentWorkingDirectory()
        .getChildFile("StudioForgeMultiTrackRecording");
    directory.deleteRecursively();
    directory.createDirectory();
    const auto firstFile = directory.getChildFile("first.wav");
    const auto secondFile = directory.getChildFile("second.wav");

    TrackDataModel model;
    model.ensureTrackCount(2);
    model.setTrackInputMonitoring(1, false, 2);
    OfflineAudioDevice device(4, 4);
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    const std::vector<AudioEngine::AudioRecordingTarget> targets { { 0, firstFile }, { 1, secondFile } };
    if (! expect(engine.startRecordings(targets).wasOk() && engine.isRecording(),
                 "multiple armed audio targets start as one recording take")) return false;

    float input0[4] { 0.10f, 0.11f, 0.12f, 0.13f };
    float input1[4] { 0.20f, 0.21f, 0.22f, 0.23f };
    float input2[4] { 0.30f, 0.31f, 0.32f, 0.33f };
    float input3[4] { 0.40f, 0.41f, 0.42f, 0.43f };
    const float* inputs[] { input0, input1, input2, input3 };
    float outputLeft[4] {}, outputRight[4] {};
    float* outputs[] { outputLeft, outputRight };
    engine.audioDeviceIOCallbackWithContext(inputs, 4, outputs, 2, 4, {});

    const auto stopped = engine.stopRecording().wasOk() && ! engine.isRecording();
    const auto& firstTrack = model.getTrack(0).clips;
    const auto& secondTrack = model.getTrack(1).clips;
    const auto mappedSeparately = firstTrack.size() == 1 && secondTrack.size() == 1
        && approximatelyEqual(firstTrack.front().cachedBuffer->getSample(0, 1), 0.11f)
        && approximatelyEqual(secondTrack.front().cachedBuffer->getSample(0, 1), 0.31f);
    directory.deleteRecursively();
    return expect(stopped && mappedSeparately,
                  "multi-track recording finalizes a separately mapped clip for every target");
}

bool testPunchRecordingCapture()
{
    const auto recordingFile = juce::File::getCurrentWorkingDirectory()
        .getChildFile("StudioForgePunchRecording.wav");
    recordingFile.deleteFile();

    TrackDataModel model;
    model.addTrack(TrackType::audio);
    model.setTrackInputConfiguration(0, false, false, 0, 2);
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
    model.setTrackInputConfiguration(0, false, true, 0, 2);
    float inputLeft[4] { 0.1f, 0.2f, 0.3f, 0.4f };
    float inputRight[4] { 0.4f, 0.3f, 0.2f, 0.1f };
    const float* inputs[] { inputLeft, inputRight };
    float outputLeft[4] {}, outputRight[4] {};
    float* outputs[] { outputLeft, outputRight };
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(outputLeft[2], 0.0f) && approximatelyEqual(outputRight[2], 0.0f)
                 && engine.getTrackInputPeak(0) > 0.0f,
                 "LIVE OFF meters input without routing it to speakers")) return false;
    engine.setLivePerformanceEnabled(true);
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(outputLeft[2], 0.0f) && approximatelyEqual(outputRight[2], 0.0f),
                 "Auto/In configuration remains silent until runtime permission is explicitly granted")) return false;
    model.setTrackMonitorDestination(0, MonitorDestination::master);
    model.setTrackMonitorPermission(0, true);
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(outputLeft[2], 0.2121320f) && approximatelyEqual(outputRight[2], 0.1414213f),
                 "explicit PA permission passes selected device input")) return false;
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

class CallbackSpanProbeEffect final : public AudioEffectProcessor
{
public:
    void prepareToPlay(double, int, int) override {}
    void processBlock(juce::AudioBuffer<float>& buffer) override
    {
        lastBlockSamples.store(buffer.getNumSamples(), std::memory_order_relaxed);
        ++calls;
    }
    void releaseResources() override {}
    juce::String getName() const override { return "Callback span probe"; }

    std::atomic<int> lastBlockSamples { 0 };
    std::atomic<int> calls { 0 };
};

class FixedLatencyImpulseEffect final : public AudioEffectProcessor
{
public:
    void prepareToPlay(double, int, int) override
    {
        history = {};
        writePosition = 0;
    }

    void processBlock(juce::AudioBuffer<float>& buffer) override
    {
        const auto channels = juce::jmin(2, buffer.getNumChannels());
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto input = buffer.getSample(channel, sample);
                buffer.setSample(channel, sample, history[static_cast<size_t>(channel)][static_cast<size_t>(writePosition)]);
                history[static_cast<size_t>(channel)][static_cast<size_t>(writePosition)] = input;
            }
            writePosition = (writePosition + 1) % latencySamples;
        }
    }

    void releaseResources() override {}
    juce::String getName() const override { return "Fixed latency impulse"; }
    int getLatencySamples() const noexcept override { return latencySamples; }

private:
    static constexpr int latencySamples = 2;
    std::array<std::array<float, latencySamples>, 2> history {};
    int writePosition = 0;
};

bool testTrackPluginDelayCompensation()
{
    TrackDataModel model;
    model.addTrack(TrackType::audio);
    model.addTrack(TrackType::audio);
    model.setFxProcessor(1, 0, std::make_shared<FixedLatencyImpulseEffect>());

    auto impulse = std::make_shared<juce::AudioBuffer<float>>(2, 4);
    impulse->clear();
    impulse->setSample(0, 0, 0.25f);
    impulse->setSample(1, 0, 0.25f);
    model.addClipToTrack(0, {}, 0.0, impulse);
    model.addClipToTrack(1, {}, 0.0, impulse);

    OfflineAudioDevice device(4);
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    model.setPlaying(true);
    float left[4] {}, right[4] {};
    float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    engine.audioDeviceStopped();

    constexpr auto centredStereoSum = 2.0f * 0.25f * 0.70710678f;
    return expect(approximatelyEqual(left[0], 0.0f) && approximatelyEqual(right[0], 0.0f)
                      && approximatelyEqual(left[2], centredStereoSum)
                      && approximatelyEqual(right[2], centredStereoSum),
                  "track plugin latency compensation aligns a delayed insert with dry tracks");
}

bool testAuxOutputLatencyCompensation()
{
    TrackDataModel model;
    const auto directTrack = model.addTrack(TrackType::audio);
    const auto auxTrack = model.addTrack(TrackType::audio);
    const auto aux = model.addBus();
    model.setBusFxProcessor(aux, 0, std::make_shared<FixedLatencyImpulseEffect>());
    model.setTrackOutputBus(auxTrack, aux);

    auto impulse = std::make_shared<juce::AudioBuffer<float>>(2, 4);
    impulse->clear();
    impulse->setSample(0, 0, 0.25f);
    impulse->setSample(1, 0, 0.25f);
    model.addClipToTrack(model.getTrackIndex(directTrack), {}, 0.0, impulse);
    model.addClipToTrack(model.getTrackIndex(auxTrack), {}, 0.0, impulse);

    OfflineAudioDevice device(4);
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    model.setPlaying(true);
    float left[4] {}, right[4] {};
    float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    engine.audioDeviceStopped();

    constexpr auto alignedStereoSum = 2.0f * 0.25f * 0.70710678f;
    return expect(approximatelyEqual(left[0], 0.0f) && approximatelyEqual(right[0], 0.0f)
                      && approximatelyEqual(left[2], alignedStereoSum)
                      && approximatelyEqual(right[2], alignedStereoSum),
                  "Studio PDC aligns a direct track with an AUX-output track that owns latency");
}

bool testMonitoringPolicyGates()
{
    TrackDataModel model;
    model.addTrack(TrackType::audio);
    model.addTrack(TrackType::audio);
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    engine.setLivePerformanceEnabled(true);
    model.setTrackInputConfiguration(0, false, true, 0, 1);
    model.setTrackInputConfiguration(1, false, true, 1, 1);
    model.setTrackMonitorDestination(0, MonitorDestination::master);
    model.setTrackMonitorDestination(1, MonitorDestination::master);
    model.setTrackMonitorPermission(0, true);
    model.setTrackMonitorPermission(1, true);
    const float inputA[4] { 0.2f, 0.2f, 0.2f, 0.2f };
    const float inputB[4] { 0.4f, 0.4f, 0.4f, 0.4f };
    const float* inputs[] { inputA, inputB };
    float left[4] {}, right[4] {};
    float* outputs[] { left, right };

    engine.setSoftwareMonitoringEnabled(false);
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(left[0], 0.0f),
                 "global software-monitor gate silences monitor output without changing track state")) return false;
    engine.setSoftwareMonitoringEnabled(true);
    engine.setFocusedMonitorTrack(model.getTrackId(0));
    engine.setFocusedInputMonitoringEnabled(true);
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 4, {});
    engine.audioDeviceStopped();
    return expect(approximatelyEqual(left[0], 0.2f * 0.70710678f),
                  "focused-monitor policy emits only the selected track input");
}

bool testPluginReceivesExactDeviceCallbackSpan()
{
    TrackDataModel model;
    model.addTrack(TrackType::audio);
    auto probe = std::make_shared<CallbackSpanProbeEffect>();
    model.setFxProcessor(0, 0, probe);
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    engine.setLivePerformanceEnabled(true);
    model.setTrackInputConfiguration(0, false, true, 0, 1);
    model.setTrackMonitorDestination(0, MonitorDestination::master);
    model.setTrackMonitorPermission(0, true);

    float input[4] { 0.2f, 0.2f, 0.2f, 0.2f };
    const float* inputs[] { input };
    float outputLeft[4] {}, outputRight[4] {};
    float* outputs[] { outputLeft, outputRight };
    engine.audioDeviceIOCallbackWithContext(inputs, 1, outputs, 2, 4, {});
    if (! expect(probe->calls.load(std::memory_order_relaxed) == 1,
                 "an insert processes one time per device callback")) return false;
    if (! expect(probe->lastBlockSamples.load(std::memory_order_relaxed) == 4,
                 "an insert receives the exact device callback span, not backing-buffer capacity")) return false;
    return expect(approximatelyEqual(outputLeft[0], 0.2f * 0.70710678f)
                      && approximatelyEqual(outputRight[0], 0.2f * 0.70710678f),
                  "the exact-span adapter preserves monitored audio");
}

bool testLiveInputRunsThroughTrackFx()
{
    TrackDataModel model;
    model.addTrack(TrackType::audio);
    model.setFxProcessor(0, 0, std::make_shared<GainUtilityProcessor>());
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    engine.setLivePerformanceEnabled(true);
    model.setTrackInputConfiguration(0, false, true, 0, 1);
    model.setTrackMonitorDestination(0, MonitorDestination::master);
    model.setTrackMonitorPermission(0, true);

    float input[4] { 0.8f, 0.8f, 0.8f, 0.8f };
    const float* inputs[] { input };
    float outputLeft[4] {}, outputRight[4] {};
    float* outputs[] { outputLeft, outputRight };
    engine.audioDeviceIOCallbackWithContext(inputs, 1, outputs, 2, 4, {});
    constexpr auto centrePan = 0.70710678f;
    return expect(approximatelyEqual(outputLeft[0], 0.8f * 0.5f * centrePan)
                      && approximatelyEqual(outputRight[0], 0.8f * 0.5f * centrePan),
                  "live input reaches master only after the track insert chain");
}

bool testInputMonitoringRecoversAfterWeakSource()
{
    TrackDataModel model;
    model.addTrack(TrackType::audio);
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    engine.setLivePerformanceEnabled(true);
    model.setTrackInputConfiguration(0, false, true, 0, 1);
    model.setTrackMonitorDestination(0, MonitorDestination::master);
    model.setTrackMonitorPermission(0, true);

    float input[4] {};
    const float* inputs[] { input };
    float outputLeft[4] {}, outputRight[4] {};
    float* outputs[] { outputLeft, outputRight };
    for (int block = 0; block < 4096; ++block)
    {
        // Simulates the exact observed source condition: healthy voice level,
        // then a weak USB input, then healthy level again.  The engine must
        // never retain/gate the weak state after source samples recover.
        const auto source = block < 1024 ? 0.80f : block < 3072 ? 0.002f : 0.80f;
        std::fill(std::begin(input), std::end(input), source);
        engine.audioDeviceIOCallbackWithContext(inputs, 1, outputs, 2, 4, {});
        constexpr auto centrePanGain = 0.70710678f;
        if (! approximatelyEqual(outputLeft[0], source * centrePanGain)
            || ! approximatelyEqual(outputRight[0], source * centrePanGain))
            return expect(false, "canonical monitor follows raw input without fading or latching after weak source");
    }
    return expect(approximatelyEqual(outputLeft[0], 0.80f * 0.70710678f),
                  "canonical monitor fully recovers when the raw microphone source recovers");
}

bool testSparsePhysicalInputRouting()
{
    TrackDataModel model;
    model.addTrack(TrackType::audio);
    // Only physical Inputs 3 and 4 are enabled. The callback provides them as
    // compact buffers 0 and 1, which is the exact mapping that used to be
    // confused with UI list positions.
    model.setTrackInputConfiguration(0, false, true, 2, 1);
    OfflineAudioDevice device(4, 4, (1 << 2) | (1 << 3));
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    engine.setLivePerformanceEnabled(true);
    model.setTrackMonitorDestination(0, MonitorDestination::master);
    model.setTrackMonitorPermission(0, true);

    float physicalInput3[4] { 0.31f, 0.32f, 0.33f, 0.34f };
    float physicalInput4[4] { 0.61f, 0.62f, 0.63f, 0.64f };
    const float* inputs[] { physicalInput3, physicalInput4 };
    float outputLeft[4] {}, outputRight[4] {};
    float* outputs[] { outputLeft, outputRight };
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 4, {});

    return expect(approximatelyEqual(outputLeft[2], 0.2333452f)
                      && approximatelyEqual(outputRight[2], 0.2333452f),
                  "physical Input 3 maps to compact callback buffer 0 when Inputs 1-2 are disabled");
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
    const auto outputFile = juce::File::getCurrentWorkingDirectory()
        .getChildFile("StudioForgePhase8Bounce.wav");
    const auto flacFile = outputFile.withFileExtension(".flac");
    outputFile.deleteFile();
    flacFile.deleteFile();
    const auto temporaryWav = outputFile.getSiblingFile(
        "." + outputFile.getFileNameWithoutExtension() + ".studioforge-render.tmp" + outputFile.getFileExtension());
    temporaryWav.deleteFile();
    if (! expect(outputFile.replaceWithText("previous bounce"), "offline bounce test prepares an existing destination")) return false;
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
    if (! expect(engine.renderOfflineWav(outputFile, 44100.0, 2).wasOk() && outputFile.existsAsFile()
                 && ! temporaryWav.exists(),
                 "offline bounce atomically replaces an existing WAV without an audio device")) return false;
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

bool testOfflineStemExport()
{
    const auto directory = juce::File::getCurrentWorkingDirectory()
        .getChildFile("StudioForgeStemExportTest");
    directory.deleteRecursively();
    if (! directory.createDirectory())
        return expect(false, "offline stem export test creates its output directory");

    TrackDataModel model;
    model.ensureTrackCount(2);
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 16);
    source->clear();
    source->setSample(0, 0, 0.5f);
    source->setSample(1, 0, 0.5f);
    model.addClipToTrack(0, {}, 0.0, source);
    model.addClipToTrack(1, {}, 0.0, source);

    AudioEngine engine(&model);
    AudioEngine::OfflineRenderOptions options;
    options.sampleRate = 44100.0;
    options.blockSize = 8;
    const auto result = engine.renderOfflineStems(directory, options);
    juce::Array<juce::File> renderedFiles;
    directory.findChildFiles(renderedFiles, juce::File::findFiles, false, "*.wav");
    juce::Array<juce::File> temporaryFiles;
    directory.findChildFiles(temporaryFiles, juce::File::findFiles, false, "*.studioforge-stem.tmp*");
    const auto wasSuccessful = result.wasOk() && renderedFiles.size() == 2 && temporaryFiles.isEmpty();
    directory.deleteRecursively();
    return expect(wasSuccessful,
                  "offline stem export promotes only completed files and removes staging files");
}

bool testMidiOnlyOfflineBounce()
{
    const auto outputFile = juce::File::getCurrentWorkingDirectory()
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

bool testMarqueeRangeDeletion()
{
    TrackDataModel model;
    model.addTrack(TrackType::audio);
    model.addTrack(TrackType::audio);
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 100);
    source->clear();
    model.addClipToTrack(0, {}, 0.0, source);
    model.addClipToTrack(1, {}, 0.0, source);
    model.clearUndoHistory();

    if (! expect(model.deleteAudioRange(30.0, 60.0, 0, 1),
                 "marquee deletion edits every selected audio lane")) return false;
    for (size_t track = 0; track < 2; ++track)
    {
        const auto& clips = model.getTrack(track).clips;
        if (! expect(clips.size() == 2 && clips[0].startSample == 0.0
                     && clips[0].durationSamples == 30.0 && clips[1].startSample == 60.0
                     && clips[1].sourceOffsetSamples == 60.0 && clips[1].durationSamples == 40.0,
                     "marquee deletion preserves non-destructive media offsets")) return false;
    }
    return expect(model.undo() && model.getTrack(0).clips.size() == 1
                      && model.getTrack(1).clips.size() == 1
                      && model.getTrack(0).clips[0].durationSamples == 100.0,
                  "multi-lane marquee deletion is one undoable edit");
}

bool testClipCrossfades()
{
    TrackDataModel model;
    const auto track = model.addTrack();
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 16);
    const auto first = model.addClipToTrack(0, {}, 0.0, source);
    const auto second = model.addClipToTrack(0, {}, 8.0, source);
    model.clearUndoHistory();
    if (! expect(first.isValid() && second.isValid() && model.createCrossfadesForTrack(track),
                 "overlapping audio clips create a crossfade")) return false;

    const auto& clips = model.getTrack(0).clips;
    if (! expect(clips[0].fadeOutSamples == 8.0 && clips[1].fadeInSamples == 8.0,
                 "crossfade length matches the overlap")) return false;
    return expect(model.undo() && model.getTrack(0).clips[0].fadeOutSamples == 0.0
                  && model.getTrack(0).clips[1].fadeInSamples == 0.0,
                  "crossfade creation is one undoable edit");
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

bool testAudioClipNoOpEditsDoNotPolluteUndoHistory()
{
    TrackDataModel model;
    const auto track = model.addTrack(TrackType::audio);
    auto source = std::make_shared<juce::AudioBuffer<float>>(1, 32);
    const auto clip = model.addClipToTrack(0, {}, 64.0, source);
    model.clearUndoHistory();

    const auto& initial = model.getTrack(0).clips.front();
    const auto moveAccepted = model.moveAudioClip(clip, track, initial.startSample);
    model.trimAudioClip(0, 0, initial.startSample, initial.durationSamples);
    return expect(moveAccepted && ! model.canUndo(),
                  "no-op audio clip move and trim do not create undo entries or render snapshots");
}

bool testAudioMediaPoolDoesNotRetainUnusedDecodes()
{
    AudioMediaPool pool;
    const auto sourceFile = juce::File("C:/StudioForgeTests/expired-cache.wav");
    auto decoded = std::make_shared<juce::AudioBuffer<float>>(2, 64);
    std::weak_ptr<juce::AudioBuffer<float>> weakDecoded = decoded;

    pool.registerDecodedSource(sourceFile, decoded);
    if (! expect(pool.findPlaybackData(sourceFile) == decoded,
                 "media pool exposes a live decoded source")) return false;

    decoded.reset();
    return expect(weakDecoded.expired() && pool.findPlaybackData(sourceFile) == nullptr
                      && pool.size() == 1,
                  "media pool keeps source metadata without retaining unused decoded audio");
}

bool testAuxRoutingPlan()
{
    using Plan = AuxRoutingPlan<4>;
    std::array<Plan::Bus, 4> buses {{{ {1}, {3}, 100, true }, { {2}, {3}, 30, true },
                                     { {3}, {}, 20, true }, { {4}, {}, 500, false } }};
    Plan plan;
    if (! expect(plan.build(buses, buses.size()) && plan.masterDirectDelay == 120
                 && plan.directInputDelay[2] == 100 && plan.outputDelay[0] == 0
                 && plan.outputDelay[1] == 70 && plan.outputDelay[2] == 0,
                 "aux plan aligns parallel inputs and ignores unused buses")) return false;
    buses[2].output = {1};
    if (! expect(! plan.build(buses, buses.size()), "aux plan rejects routing cycles")) return false;
    buses[2].output = {99};
    if (! expect(! plan.build(buses, buses.size()), "aux plan rejects missing destinations")) return false;
    buses[2].output = {3};
    return expect(! plan.build(buses, buses.size()), "aux plan rejects self routing");
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
    model.setBusBalance(bus, -1.0f);
    model.setPlayheadPosition(0.0);
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    if (! expect(approximatelyEqual(left[0], 1.2374369f) && approximatelyEqual(right[0], 0.7071068f),
                 "aux balance changes only the routed stereo return")) return false;
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

bool testPreFaderSendRouting()
{
    TrackDataModel model;
    const auto track = model.addTrack(TrackType::audio);
    const auto bus = model.addBus();
    auto source = std::make_shared<juce::AudioBuffer<float>>(2, 4);
    source->clear();
    source->setSample(0, 0, 1.0f);
    source->setSample(1, 0, 1.0f);
    model.addClipToTrack(0, {}, 0.0, source);
    model.setTrackSendRoute(track, 0, bus, 1.0f, true);
    model.setTrackVolume(0, 0.25f);
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
                      && approximatelyEqual(left[0], 1.25f)
                      && approximatelyEqual(right[0], 1.0f),
                  "pre-fader sends are unaffected by track volume and pan");
}

bool testPostFaderBeforePanSendRouting()
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
    if (! model.setTrackSendPostFaderBeforePan(track, 0, true)) return false;
    if (! expect(model.setTrackSendRoute(track, 0, bus, 0.5f, false)
                 && model.getTrack(0).sends[0].postFaderBeforePan,
                 "adjusting a send preserves its post-fader tap")) return false;
    model.setTrackSendRoute(track, 0, bus, 1.0f, false);
    model.setTrackVolume(0, 0.25f);
    model.setTrackPan(0, -1.0f);
    OfflineAudioDevice device;
    AudioEngine engine(&model);
    engine.audioDeviceAboutToStart(&device);
    model.setPlaying(true);
    model.setPlayheadPosition(0.0);
    float left[4] {}, right[4] {};
    float* outputs[] { left, right };
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 4, {});
    return expect(approximatelyEqual(engine.getBusPeak(0), 0.25f)
                      && approximatelyEqual(left[0], 0.5f)
                      && approximatelyEqual(right[0], 0.25f),
                  "post-fader send follows fader but not pan");
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
    if (! expect(peak.minimum < -0.74f && peak.maximum <= 0.01f,
                 "waveform thumbnail maps a clip source offset to prepared source peaks")) return false;
    const auto energyPeak = first->peakForSourceRange(2.0, 4.0);
    if (! expect(std::abs(energyPeak.rms() - std::sqrt((0.75f * 0.75f + 0.50f * 0.50f) / 2.0f)) < 0.001f,
                 "waveform thumbnail aggregates RMS from sample energy rather than averaging amplitudes")) return false;

    auto stereo = std::make_shared<juce::AudioBuffer<float>>(2, 8);
    stereo->clear();
    stereo->setSample(0, 4, 0.25f);
    stereo->setSample(1, 4, -0.90f);
    const auto stereoThumbnail = cache.prepare(juce::File("C:/StudioForgeTests/stereo.wav"), stereo);
    if (! expect(stereoThumbnail != nullptr && stereoThumbnail->getDisplayChannelCount() == 2,
                 "stereo source retains two display channels")) return false;
    const auto left = stereoThumbnail->peakForSourceRange(4.0, 5.0, 0);
    const auto right = stereoThumbnail->peakForSourceRange(4.0, 5.0, 1);
    if (! expect(left.maximum > 0.24f && left.minimum >= -0.01f
                 && right.minimum < -0.89f && right.maximum <= 0.01f,
                 "stereo waveform peaks retain independent left and right envelopes")) return false;

    juce::MemoryOutputStream payload;
    if (! expect(stereoThumbnail->writePersistent(payload), "waveform peak pyramid serialises for disk cache")) return false;
    juce::MemoryInputStream input(payload.getData(), payload.getDataSize(), false);
    const auto restored = WaveformThumbnail::readPersistent(input);
    if (! expect(restored != nullptr && restored->getDisplayChannelCount() == 2,
                 "waveform peak cache restores a valid stereo payload")) return false;
    const auto restoredRight = restored->peakForSourceRange(4.0, 5.0, 1);
    juce::MemoryInputStream corrupt("bad", 3, false);
    return expect(restoredRight.minimum < -0.89f && WaveformThumbnail::readPersistent(corrupt) == nullptr,
                  "waveform cache preserves peaks and rejects corrupt payloads");
}

bool testWaveformCacheInvalidatesChangedSourceFile()
{
    const auto directory = juce::File::getCurrentWorkingDirectory()
        .getNonexistentChildFile("StudioForgeWaveformFingerprint", {}, true);
    const auto sourceFile = directory.getChildFile("take.wav");
    if (! directory.createDirectory() || ! sourceFile.replaceWithText("first take"))
        return expect(false, "waveform cache test prepares a source file");

    auto firstBuffer = std::make_shared<juce::AudioBuffer<float>>(1, 8);
    firstBuffer->clear();
    firstBuffer->setSample(0, 2, 0.25f);
    WaveformThumbnailCache cache;
    const auto first = cache.prepare(sourceFile, firstBuffer);

    if (! sourceFile.replaceWithText("second take with a different file size"))
    {
        directory.deleteRecursively();
        return expect(false, "waveform cache test modifies its source file");
    }

    auto replacementBuffer = std::make_shared<juce::AudioBuffer<float>>(1, 8);
    replacementBuffer->clear();
    replacementBuffer->setSample(0, 2, -0.80f);
    const auto replacement = cache.prepare(sourceFile, replacementBuffer);
    const auto valid = first != nullptr && replacement != nullptr && first != replacement
        && replacement->peakForSourceRange(2.0, 3.0).minimum < -0.79f;
    directory.deleteRecursively();
    return expect(valid, "waveform cache invalidates its in-memory envelope when source media changes");
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
    MidiClipState clip { { 1 }, { 1 }, 100.0, 0.0,
                         { { { 1 }, 64, 0.8f, 10.0, 20.0, 2 } } };
    MidiEventBuffer events;
    MidiScheduler::scheduleBlock({ clip }, 100.0, 16, events);
    if (! expect(events.size() == 1 && events[0].noteOn && events[0].sampleOffset == 10 && events[0].pitch == 64,
                 "MIDI scheduler emits note-on at clip-relative sample")) return false;
    MidiScheduler::scheduleBlock({ clip }, 120.0, 16, events);
    if (! expect(events.size() == 1 && !events[0].noteOn && events[0].sampleOffset == 10
                    && MidiScheduler::quantizeSample(123.0, 24.0) == 120.0,
                 "MIDI scheduler emits note-off and quantizes samples")) return false;

    clip.durationSamples = 15.0;
    MidiScheduler::scheduleBlock({ clip }, 114.0, 4, events);
    return expect(events.size() == 1 && ! events[0].noteOn && events[0].sampleOffset == 1,
                  "MIDI trim emits a final note-off at the non-destructive clip boundary");
}

bool testRealtimeMidiEvents()
{
    const std::array<juce::MidiMessage, 5> messages {
        juce::MidiMessage::noteOn(2, 64, 0.5f),
        juce::MidiMessage::controllerEvent(3, 74, 99),
        juce::MidiMessage::pitchWheel(4, 9000),
        juce::MidiMessage::channelPressureChange(5, 72),
        juce::MidiMessage::programChange(6, 12)
    };
    for (const auto& message : messages)
    {
        MidiRealtimeEvent event;
        if (! MidiRealtimeEvent::fromMessage(message, event))
            return expect(false, "realtime MIDI event accepts supported channel messages");
        const auto restored = event.toMessage();
        if (restored.getChannel() != message.getChannel())
            return expect(false, "realtime MIDI event preserves MIDI channel");
    }

    TrackDataModel model;
    model.addTrack(TrackType::instrument);
    model.setTrackMidiInputChannel(0, 7);
    return expect(model.getTrackMidiInputChannel(0) == 7
                      && model.acquireRealtimeSnapshot().getRenderStructure()->tracks[0].midiInputChannel == 7,
                  "track MIDI input channel publishes into the realtime snapshot");
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

bool testMidiClipArrangementEdits()
{
    TrackDataModel model;
    const auto sourceTrack = model.addTrack(TrackType::instrument);
    const auto destinationTrack = model.addTrack(TrackType::externalMidi);
    const auto clip = model.addMidiClip(sourceTrack, 120.0);
    const auto note = model.addMidiNote(clip, 60, 0.8f, 10.0, 20.0, 1);

    if (! expect(note.isValid() && model.moveMidiClip(clip, destinationTrack, 360.0),
                 "MIDI clips move atomically between MIDI-compatible tracks")) return false;

    const auto moved = std::find_if(model.getMidiClips().begin(), model.getMidiClips().end(), [clip] (const auto& candidate)
    {
        return candidate.id == clip;
    });
    if (! expect(moved != model.getMidiClips().end() && moved->trackId == destinationTrack
                     && moved->startSample == 360.0,
                 "MIDI clip move preserves its clip identity and note content")) return false;

    const auto duplicate = model.duplicateMidiClip(clip, sourceTrack, 720.0);
    if (! expect(duplicate.isValid() && model.getMidiClips().size() == 2,
                 "MIDI clip duplicate creates an independent clip")) return false;

    const auto copied = std::find_if(model.getMidiClips().begin(), model.getMidiClips().end(), [duplicate] (const auto& candidate)
    {
        return candidate.id == duplicate;
    });
    if (! expect(copied != model.getMidiClips().end() && copied->trackId == sourceTrack
                     && copied->startSample == 720.0 && copied->notes.size() == 1
                     && copied->notes.front().id != note,
                 "MIDI duplication assigns new immutable identities to copied notes")) return false;

    if (! expect(model.setMidiClipDuration(clip, 15.0) && model.getMidiClips().front().durationSamples == 15.0,
                 "MIDI clip length is an independent non-destructive edit")) return false;

    return expect(model.deleteMidiClip(duplicate) && model.getMidiClips().size() == 1
                      && model.undo() && model.getMidiClips().size() == 2,
                  "MIDI clip deletion remains undoable");
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
    const auto projectFile = juce::File::getCurrentWorkingDirectory()
        .getChildFile("StudioForgeMidiFxPersistence.sfproj");
    const auto catalogFile = juce::File::getCurrentWorkingDirectory()
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
    auto restoredCorrectly = false;
    for (const auto metadata : midi)
    {
        restoredCorrectly = metadata.getMessage().getNoteNumber() == 67;
        break;
    }
    projectFile.deleteFile();
    catalogFile.deleteFile();
    return expect(restoredCorrectly, "restored MIDI FX retains its transpose parameter");
}

bool testPluginHostFoundation()
{
    const auto catalog = juce::File::getCurrentWorkingDirectory()
        .getChildFile("StudioForgePluginHostFoundation.xml");
    catalog.deleteFile();
    PluginHostService host(catalog);
    const auto result = host.scanVst3(juce::File::getCurrentWorkingDirectory()
                                      .getChildFile("StudioForge-not-a-plugin.vst3"));
    return expect(result.failed(), "plugin scan rejects a missing VST3 without entering the audio path")
        && expect(host.getKnownPlugins().isEmpty(), "failed scan does not publish a plugin type");
}

bool testCorruptPluginCatalogRecovery()
{
    const auto catalog = juce::File::getCurrentWorkingDirectory()
        .getChildFile("StudioForgeCorruptPluginCatalog.xml");
    catalog.deleteFile();
    if (! expect(catalog.replaceWithText("<broken-catalog/>"),
                 "plugin catalog recovery test creates malformed catalog")) return false;

    PluginHostService host(catalog);
    juce::Array<juce::File> preservedCopies;
    catalog.getParentDirectory().findChildFiles(preservedCopies, juce::File::findFiles, false,
                                                catalog.getFileName() + ".corrupt-*");
    const auto recovered = host.getKnownPlugins().isEmpty() && ! preservedCopies.isEmpty();
    catalog.deleteFile();
    for (const auto& file : preservedCopies)
        file.deleteFile();
    return expect(recovered,
                  "a malformed plug-in catalog is preserved and does not block fresh discovery");
}

bool testVst3EffectIntegration()
{
    const juce::File pluginFile { STUDIOFORGE_TEST_VST3_PATH };
    if (! expect(pluginFile.exists(), "controlled VST3 test effect was built")) return false;

    const auto catalog = juce::File::getCurrentWorkingDirectory()
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
    const auto projectFile = juce::File::getCurrentWorkingDirectory()
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

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    const auto passed = testTrackMixing()
        && testAudioRegionHitTesting()
        && testTimelineViewportNavigation()
        && testGlobalScaleContext()
        && testLivePerformanceAssessment()
        && testProjectTemplates()
        && testStudioForgeControlBridge()
        && testPluginSemanticMapping()
        && testPerformanceMacroService()
        && testPerformanceSurfaceProtocol()
        && testMusicalKeyNormalization()
        && testPerformanceSetupRouting()
        && testPerformanceSourceChoices()
        && testProjectTemplateStore()
        && testProjectAlternativeStore()
        && testProjectBackupStore()
        && testMetronomeRendering()
        && testMidiFileImport()
        && runAutomationDomainTests()
        && testInspectorViewStateBuilder()
        && testTrackAutomationSnapshots()
        && testRecordArmTargetSelection()
        && testProjectRevision()
        && testMasterGraph()
        && testMasterOutputState()
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
        && testMediaReloadCachesFailedSourcesPerBatch()
        && testImmediateAudioImportPlaceholder()
        && testUndoRedoHistory()
        && testAutomationWriteModes()
        && testRecordingFoundation()
        && testRecordedTakeGroups()
        && testMultiTrackRecording()
        && testPunchRecordingCapture()
        && testMarqueeRangeDeletion()
        && testTrackPluginDelayCompensation()
        && testAuxOutputLatencyCompensation()
        && testMonitoringPolicyGates()
        && testPluginReceivesExactDeviceCallbackSpan()
        && testInputMonitoring()
        && testLiveInputRunsThroughTrackFx()
        && testInputMonitoringRecoversAfterWeakSource()
        && testSparsePhysicalInputRouting()
        && testMidiInputMonitoring()
        && testMidiInputRoutesToArmedInstrument()
        && testMidiRecordingFoundation()
        && testOfflineBounce()
        && testOfflineStemExport()
        && testMidiOnlyOfflineBounce()
        && testClipGainAndFades()
        && testClipDuplicate()
        && testClipCrossfades()
        && testAudioMediaSourceOwnership()
        && testAudioClipNoOpEditsDoNotPolluteUndoHistory()
        && testAudioMediaPoolDoesNotRetainUnusedDecodes()
        && testAuxRoutingPlan()
        && testBusAndSendRouting()
        && testPostPanSendRouting()
        && testPreFaderSendRouting()
        && testPostFaderBeforePanSendRouting()
        && testSendAutomation()
        && testWaveformThumbnailCache()
        && testWaveformCacheInvalidatesChangedSourceFile()
        && testMasterFxRouting()
        && testMidiCoreScheduling()
        && testRealtimeMidiEvents()
        && testMidiModelToEngineScheduling()
        && testMidiClipEditing()
        && testMidiClipArrangementEdits()
        && testMidiInstrumentPath()
        && testTrackTypeCreation()
        && testMidiReachesTrackProcessor()
        && testMidiFxChain()
        && testMidiFxPersistence()
        && testPluginHostFoundation()
        && testCorruptPluginCatalogRecovery()
        && testVst3EffectIntegration()
        && testOfflineAudioEnginePath();
#ifdef STUDIOFORGE_PERF_BENCHMARK
    const auto benchmarkPassed = testRealtimeHostBenchmark();
    return passed && benchmarkPassed ? 0 : 1;
#else
    return passed ? 0 : 1;
#endif
}
