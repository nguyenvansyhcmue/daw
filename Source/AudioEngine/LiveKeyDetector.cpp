#include "LiveKeyDetector.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr std::array<float, 12> majorProfile { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
                                                 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
constexpr std::array<float, 12> minorProfile { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
                                                 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

float correlation(const std::array<float, 12>& values, const std::array<float, 12>& profile, int shift) noexcept
{
    float sumValues = 0.0f, sumProfile = 0.0f;
    for (int index = 0; index < 12; ++index)
    {
        sumValues += values[static_cast<size_t>(index)];
        sumProfile += profile[static_cast<size_t>((index - shift + 12) % 12)];
    }
    const auto meanValues = sumValues / 12.0f;
    const auto meanProfile = sumProfile / 12.0f;
    float numerator = 0.0f, valueEnergy = 0.0f, profileEnergy = 0.0f;
    for (int index = 0; index < 12; ++index)
    {
        const auto value = values[static_cast<size_t>(index)] - meanValues;
        const auto target = profile[static_cast<size_t>((index - shift + 12) % 12)] - meanProfile;
        numerator += value * target;
        valueEnergy += value * value;
        profileEnergy += target * target;
    }
    return numerator / std::sqrt(juce::jmax(1.0e-12f, valueEnergy * profileEnergy));
}
}

LiveKeyDetector::LiveKeyDetector() : juce::Thread("StudioForge Live Key Detector")
{
    startThread(juce::Thread::Priority::low);
}

LiveKeyDetector::~LiveKeyDetector()
{
    signalThreadShouldExit();
    stopThread(1500);
}

void LiveKeyDetector::push(const juce::AudioBuffer<float>& source, int numSamples, double sampleRate) noexcept
{
    if (numSamples <= 0 || source.getNumChannels() <= 0 || sampleRate <= 0.0)
        return;
    sourceSampleRate.store(sampleRate, std::memory_order_relaxed);

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite(juce::jmin(numSamples, fifo.getFreeSpace()), start1, size1, start2, size2);
    const auto copy = [&source] (float* destination, int destinationStart, int sourceStart, int count) noexcept
    {
        const auto* left = source.getReadPointer(0, sourceStart);
        const auto* right = source.getNumChannels() > 1 ? source.getReadPointer(1, sourceStart) : left;
        for (int sample = 0; sample < count; ++sample)
            destination[destinationStart + sample] = 0.5f * (left[sample] + right[sample]);
    };
    copy(fifoSamples.data(), start1, 0, size1);
    copy(fifoSamples.data(), start2, size1, size2);
    fifo.finishedWrite(size1 + size2);
}

LiveKeyDetector::Result LiveKeyDetector::getLatestResult() const noexcept
{
    const auto packed = packedResult.load(std::memory_order_acquire);
    return { static_cast<int>(packed & 0x0fu), (packed & 0x10u) != 0,
             static_cast<float>((packed >> 5u) & 0xffu) / 255.0f, packed >> 13u };
}

void LiveKeyDetector::run()
{
    std::vector<float> history;
    double historyRate = 0.0;
    uint64_t revision = 0;
    int candidateRoot = -1;
    bool candidateMinor = false;
    int candidateHits = 0;
    int confirmedRoot = -1;
    bool confirmedMinor = false;
    auto lastAnalysis = juce::Time::getMillisecondCounterHiRes();
    while (! threadShouldExit())
    {
        const auto rate = sourceSampleRate.load(std::memory_order_relaxed);
        if (rate != historyRate)
        {
            history.clear();
            historyRate = rate;
        }
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead(fifo.getNumReady(), start1, size1, start2, size2);
        history.insert(history.end(), fifoSamples.begin() + start1, fifoSamples.begin() + start1 + size1);
        history.insert(history.end(), fifoSamples.begin() + start2, fifoSamples.begin() + start2 + size2);
        fifo.finishedRead(size1 + size2);

        const auto wantedSamples = static_cast<size_t>(juce::jmax(1.0, historyRate * 3.0));
        if (history.size() > wantedSamples)
            history.erase(history.begin(), history.begin()
                          + static_cast<std::vector<float>::difference_type>(history.size() - wantedSamples));

        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (history.size() >= wantedSamples && now - lastAnalysis >= 1000.0)
        {
            lastAnalysis = now;
            const auto result = analyse(history, historyRate, revision);
            if (result.confidence < 0.45f)
            {
                candidateRoot = -1;
                candidateHits = 0;
                continue;
            }

            if (result.root == candidateRoot && result.minor == candidateMinor)
                ++candidateHits;
            else
            {
                candidateRoot = result.root;
                candidateMinor = result.minor;
                candidateHits = 1;
            }

            // Tune-AI waited for three matching observations. Keep that useful
            // safeguard, but publish only a confirmed *change* so plug-in
            // parameters are not rewritten every second.
            if (candidateHits < 3 || (candidateRoot == confirmedRoot && candidateMinor == confirmedMinor))
                continue;

            confirmedRoot = candidateRoot;
            confirmedMinor = candidateMinor;
            ++revision;
            const auto confidence = static_cast<uint64_t>(juce::jlimit(0, 255, juce::roundToInt(result.confidence * 255.0f)));
            packedResult.store((revision << 13u) | (confidence << 5u)
                                   | (confirmedMinor ? 0x10u : 0u) | static_cast<uint64_t>(confirmedRoot),
                               std::memory_order_release);
        }
        wait(20);
    }
}

LiveKeyDetector::Result LiveKeyDetector::analyse(const std::vector<float>& audio, double sampleRate, uint64_t revision) noexcept
{
    std::array<float, 12> chroma {};
    constexpr int frameSize = 4096;
    constexpr int hopSize = 2048;
    if (audio.size() < frameSize || sampleRate <= 0.0)
        return { 0, false, 0.0f, revision };

    for (size_t frame = 0; frame + frameSize <= audio.size(); frame += hopSize)
        for (int midi = 36; midi <= 84; ++midi)
        {
            const auto frequency = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
            const auto step = juce::MathConstants<double>::twoPi * frequency / sampleRate;
            double real = 0.0, imaginary = 0.0;
            for (int sample = 0; sample < frameSize; ++sample)
            {
                const auto window = 0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * sample / (frameSize - 1));
                const auto value = static_cast<double>(audio[frame + static_cast<size_t>(sample)]) * window;
                const auto phase = step * sample;
                real += value * std::cos(phase);
                imaginary -= value * std::sin(phase);
            }
            chroma[static_cast<size_t>(midi % 12)] += static_cast<float>(real * real + imaginary * imaginary);
        }

    auto bestScore = -2.0f;
    auto secondScore = -2.0f;
    auto bestRoot = 0;
    auto bestMinor = false;
    const auto consider = [&] (float score, int root, bool isMinor)
    {
            if (score > bestScore)
            {
                secondScore = bestScore;
                bestScore = score;
                bestRoot = root;
                bestMinor = isMinor;
            }
            else if (score > secondScore) secondScore = score;
    };
    for (int root = 0; root < 12; ++root)
    {
        consider(correlation(chroma, majorProfile, root), root, false);
        consider(correlation(chroma, minorProfile, root), root, true);
    }
    const auto confidence = juce::jlimit(0.0f, 1.0f, (bestScore + 1.0f) * 0.5f
                                                     * juce::jlimit(0.0f, 1.0f, (bestScore - secondScore) * 3.0f));
    return { bestRoot, bestMinor, confidence, revision };
}
