#include "RecordingSession.h"

#include "../Media/MediaReloadService.h"
#include "../Models/TrackDataModel.h"

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
                                     double startSample, double sampleRate, int inputChannels, int maximumBlockSize)
{
    if (isRecording()) return juce::Result::fail("Recording is already active");
    if (trackIndex >= model.getTrackCount() || sampleRate <= 0.0 || inputChannels <= 0 || maximumBlockSize <= 0)
        return juce::Result::fail("Recording input configuration is invalid");
    destination.deleteFile();
    auto stream = destination.createOutputStream();
    if (stream == nullptr) return juce::Result::fail("Cannot create recording file");
    juce::WavAudioFormat wav;
    auto* rawStream = stream.release();
    auto* formatWriter = wav.createWriterFor(rawStream, sampleRate, static_cast<unsigned int>(inputChannels), 32, {}, 0);
    if (formatWriter == nullptr)
    {
        delete rawStream;
        return juce::Result::fail("Cannot create WAV recording writer");
    }
    captureBuffer.setSize(inputChannels, maximumBlockSize, false, true, true);
    writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(formatWriter, writerThread,
                                                                         maximumBlockSize * 64);
    destinationFile = destination;
    targetTrack = trackIndex;
    targetStartSample = startSample;
    droppedBlocks.store(0, std::memory_order_relaxed);
    capturedSamples.store(0, std::memory_order_relaxed);
    activeWriter.store(writer.get(), std::memory_order_release);
    return juce::Result::ok();
}

void RecordingSession::capture(const float* const* input, int inputChannels, int numSamples) noexcept
{
    auto* currentWriter = activeWriter.load(std::memory_order_acquire);
    if (currentWriter == nullptr || input == nullptr || inputChannels <= 0 || numSamples <= 0
        || numSamples > captureBuffer.getNumSamples()) return;
    callbackUsers.fetch_add(1, std::memory_order_acq_rel);
    if (currentWriter != activeWriter.load(std::memory_order_acquire))
    {
        callbackUsers.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }
    for (int channel = 0; channel < captureBuffer.getNumChannels(); ++channel)
    {
        const auto sourceChannel = juce::jmin(channel, inputChannels - 1);
        juce::FloatVectorOperations::copy(captureBuffer.getWritePointer(channel), input[sourceChannel], numSamples);
    }
    if (! currentWriter->write(captureBuffer.getArrayOfReadPointers(), numSamples))
        droppedBlocks.fetch_add(1, std::memory_order_relaxed);
    else
        capturedSamples.fetch_add(numSamples, std::memory_order_relaxed);
    callbackUsers.fetch_sub(1, std::memory_order_acq_rel);
}

juce::Result RecordingSession::stop()
{
    auto* currentWriter = activeWriter.exchange(nullptr, std::memory_order_acq_rel);
    if (currentWriter == nullptr) return juce::Result::ok();
    while (callbackUsers.load(std::memory_order_acquire) != 0) juce::Thread::sleep(1);
    writer.reset();
    if (capturedSamples.load(std::memory_order_relaxed) == 0) return juce::Result::ok();
    std::shared_ptr<juce::AudioBuffer<float>> decoded;
    if (const auto result = MediaReloadService::decode(destinationFile, decoded); result.failed()) return result;
    if (! model.addClipToTrack(static_cast<int>(targetTrack), destinationFile, targetStartSample, std::move(decoded)).isValid())
        return juce::Result::fail("Recorded media could not be added to the target track");
    return juce::Result::ok();
}

bool RecordingSession::isRecording() const noexcept { return activeWriter.load(std::memory_order_acquire) != nullptr; }
int RecordingSession::getDroppedBlockCount() const noexcept { return droppedBlocks.load(std::memory_order_relaxed); }
