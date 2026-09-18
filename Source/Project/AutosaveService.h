#pragma once

#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>

class TrackDataModel;

class AutosaveService final : private juce::Timer
{
public:
    explicit AutosaveService(TrackDataModel& model,
                             juce::File recoveryDirectory = defaultRecoveryDirectory());
    ~AutosaveService() override;

    void setActiveProject(const juce::File& projectFile);
    void markProjectSaved();
    bool saveRecoveryNow();
    bool hasRecoverySnapshot() const;
    bool hasNewerRecoverySnapshot() const;
    juce::File getRecoveryFile() const;

    static juce::File defaultRecoveryDirectory();

private:
    void timerCallback() override;
    juce::File makeRecoveryFile(const juce::File& projectFile) const;

    TrackDataModel& trackDataModel;
    juce::File recoveryRoot;
    juce::File activeProject;
    uint64_t lastRecoveredRevision = 0;
};
