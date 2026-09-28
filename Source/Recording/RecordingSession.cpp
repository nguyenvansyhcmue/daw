#include "RecordingSession.h"

#include "../Media/MediaReloadService.h"

RecordingSession::RecordingSession(TrackDataModel& dataModel) : model(dataModel)
{
    writerThread.startThread();
}

RecordingSession::~RecordingSession()
{
    stop();
    writerThread.stopThread(2000);
}

juce::Result RecordingSession::start(size_t trackIndex, const juce::File& destination,
                                     double startSample, double sampleRate, int inputChannel,
                                     int maximumBlockSize)
{
    return startMany({ { trackIndex, destination, inputChannel, 1 } }, startSample, sampleRate, maximumBlockSize);
}

juce::Result RecordingSession::startMany(const std::vector<TrackTarget>& targets, double startSample,
                                         double sampleRate, int maximumBlockSize)
{
    if (isRecording())
        return juce::Result::fail("Recording is already active");
    if (targets.empty() || targets.size() > recorders.size() || sampleRate <= 0.0 || maximumBlockSize <= 0)
        return juce::Result::fail("Recording input configuration is invalid");

    std::array<bool, TrackDataModel::maxTracks> usedTracks {};
    size_t preparedCount = 0;
    for (const auto& target : targets)
    {
        if (target.trackIndex >= model.getTrackCount() || target.inputChannel < 0
            || target.inputChannelCount < 1 || target.inputChannelCount > 2 || usedTracks[target.trackIndex])
        {
            for (size_t index = 0; index < preparedCount; ++index)
                recorders[index].writer.reset();
            return juce::Result::fail("Recording targets must use distinct valid audio tracks");
        }

        usedTracks[target.trackIndex] = true;
        if (const auto result = prepareRecorder(recorders[preparedCount], target, startSample,
                                                sampleRate, maximumBlockSize); result.failed())
        {
            for (size_t index = 0; index <= preparedCount; ++index)
                recorders[index].writer.reset();
            return result;
        }
        ++preparedCount;
    }

    // Publish only after every writer is ready: the realtime callback sees
    // either the complete take or none of it.
    for (size_t index = 0; index < preparedCount; ++index)
        recorders[index].activeWriter.store(recorders[index].writer.get(), std::memory_order_release);
    activeRecorderCount.store(preparedCount, std::memory_order_release);
    return juce::Result::ok();
}

juce::Result RecordingSession::prepareRecorder(TrackRecorder& recorder, const TrackTarget& target,
                                               double startSample, double sampleRate, int maximumBlockSize)
{
    target.destination.deleteFile();
    auto stream = target.destination.createOutputStream();
    if (stream == nullptr)
        return juce::Result::fail("Cannot create recording file: " + target.destination.getFileName());

    juce::WavAudioFormat wav;
    auto* rawStream = stream.release();
    auto* formatWriter = wav.createWriterFor(rawStream, sampleRate, 2, 32, {}, 0);
    if (formatWriter == nullptr)
    {
        delete rawStream;
        return juce::Result::fail("Cannot create WAV recording writer");
    }

    recorder.captureBuffer.setSize(2, maximumBlockSize, false, true, true);
    recorder.writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(formatWriter, writerThread,
                                                                                  maximumBlockSize * 64);
    recorder.destinationFile = target.destination;
    recorder.targetTrack = target.trackIndex;
    recorder.targetStartSample = startSample;
    recorder.sourceInputChannel = target.inputChannel;
    recorder.sourceInputChannelCount = target.inputChannelCount;
    recorder.droppedBlocks.store(0, std::memory_order_relaxed);
    recorder.capturedSamples.store(0, std::memory_order_relaxed);
    return juce::Result::ok();
}

void RecordingSession::capture(const float* const* input, int inputChannels, int sourceOffset, int numSamples) noexcept
{
    if (! isRecording() || input == nullptr || inputChannels <= 0 || numSamples <= 0)
        return;

    callbackUsers.fetch_add(1, std::memory_order_acq_rel);
    for (auto& recorder : recorders)
        if (recorder.activeWriter.load(std::memory_order_acquire) != nullptr)
            captureRecorder(recorder, input, inputChannels, sourceOffset, numSamples);
    callbackUsers.fetch_sub(1, std::memory_order_acq_rel);
}

void RecordingSession::captureRecorder(TrackRecorder& recorder, const float* const* input, int inputChannels,
                                       int sourceOffset, int numSamples) noexcept
{
    auto* writer = recorder.activeWriter.load(std::memory_order_acquire);
    if (writer == nullptr || sourceOffset < 0 || sourceOffset + numSamples > recorder.captureBuffer.getNumSamples())
        return;

    const auto leftInput = recorder.sourceInputChannel;
    if (leftInput >= inputChannels || input[leftInput] == nullptr)
        return;
    const auto rightInput = recorder.sourceInputChannelCount == 2 ? leftInput + 1 : leftInput;
    if (rightInput >= inputChannels)
        return;
    juce::FloatVectorOperations::copy(recorder.captureBuffer.getWritePointer(0), input[leftInput] + sourceOffset, numSamples);
    if (input[rightInput] != nullptr)
        juce::FloatVectorOperations::copy(recorder.captureBuffer.getWritePointer(1), input[rightInput] + sourceOffset, numSamples);
    else
        juce::FloatVectorOperations::copy(recorder.captureBuffer.getWritePointer(1), input[leftInput] + sourceOffset, numSamples);

    if (! writer->write(recorder.captureBuffer.getArrayOfReadPointers(), numSamples))
        recorder.droppedBlocks.fetch_add(1, std::memory_order_relaxed);
    else
        recorder.capturedSamples.fetch_add(numSamples, std::memory_order_relaxed);
}

juce::Result RecordingSession::stop()
{
    if (! isRecording())
        return juce::Result::ok();

    for (auto& recorder : recorders)
        recorder.activeWriter.store(nullptr, std::memory_order_release);
    activeRecorderCount.store(0, std::memory_order_release);
    while (callbackUsers.load(std::memory_order_acquire) != 0)
        juce::Thread::sleep(1);

    auto result = juce::Result::ok();
    for (auto& recorder : recorders)
        if (recorder.writer != nullptr)
        {
            recorder.writer.reset();
            if (const auto finished = finaliseRecorder(recorder); result.wasOk() && finished.failed())
                result = finished;
        }
    return result;
}

juce::Result RecordingSession::finaliseRecorder(TrackRecorder& recorder)
{
    if (recorder.capturedSamples.load(std::memory_order_relaxed) == 0)
        return juce::Result::ok();

    std::shared_ptr<juce::AudioBuffer<float>> decoded;
    if (const auto result = MediaReloadService::decode(recorder.destinationFile, decoded); result.failed())
        return result;
    if (! model.addClipToTrack(static_cast<int>(recorder.targetTrack), recorder.destinationFile,
                               recorder.targetStartSample, std::move(decoded)).isValid())
        return juce::Result::fail("Recorded media could not be added to the target track");
    return juce::Result::ok();
}

bool RecordingSession::isRecording() const noexcept
{
    return activeRecorderCount.load(std::memory_order_acquire) > 0;
}

int RecordingSession::getDroppedBlockCount() const noexcept
{
    auto dropped = 0;
    for (const auto& recorder : recorders)
        dropped += recorder.droppedBlocks.load(std::memory_order_relaxed);
    return dropped;
}
