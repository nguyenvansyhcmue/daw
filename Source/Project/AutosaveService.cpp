#include "AutosaveService.h"

#include "../Models/TrackDataModel.h"
#include "ProjectSerializer.h"

namespace
{
constexpr auto autosaveIntervalMilliseconds = 60 * 1000;
}

AutosaveService::AutosaveService(TrackDataModel& model, juce::File recoveryDirectory)
    : trackDataModel(model), recoveryRoot(std::move(recoveryDirectory))
{
    startTimer(autosaveIntervalMilliseconds);
}

AutosaveService::~AutosaveService()
{
    stopTimer();
}

juce::File AutosaveService::defaultRecoveryDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("StudioForge")
        .getChildFile("Recovery");
}

void AutosaveService::setActiveProject(const juce::File& projectFile)
{
    activeProject = projectFile;
    lastRecoveredRevision = trackDataModel.getProjectRevision();
}

void AutosaveService::markProjectSaved()
{
    lastRecoveredRevision = trackDataModel.getProjectRevision();
    const auto recoveryFile = getRecoveryFile();
    if (recoveryFile.existsAsFile())
        recoveryFile.deleteFile();
}

bool AutosaveService::saveRecoveryNow()
{
    if (activeProject == juce::File {})
        return false;

    if (! recoveryRoot.exists() && ! recoveryRoot.createDirectory())
        return false;

    const auto recoveryFile = getRecoveryFile();
    const auto temporaryFile = recoveryFile.getSiblingFile(recoveryFile.getFileName() + ".tmp");
    temporaryFile.deleteFile();

    if (ProjectSerializer::saveRecoverySnapshot(trackDataModel, temporaryFile).failed())
        return false;

    if (recoveryFile.existsAsFile() && ! recoveryFile.deleteFile())
    {
        temporaryFile.deleteFile();
        return false;
    }

    if (! temporaryFile.moveFileTo(recoveryFile))
    {
        temporaryFile.deleteFile();
        return false;
    }

    lastRecoveredRevision = trackDataModel.getProjectRevision();
    return true;
}

bool AutosaveService::hasRecoverySnapshot() const
{
    return getRecoveryFile().existsAsFile();
}

bool AutosaveService::hasNewerRecoverySnapshot() const
{
    const auto recoveryFile = getRecoveryFile();
    return recoveryFile.existsAsFile()
        && (! activeProject.existsAsFile()
            || recoveryFile.getLastModificationTime() > activeProject.getLastModificationTime());
}

juce::File AutosaveService::getRecoveryFile() const
{
    return activeProject == juce::File {} ? juce::File {} : makeRecoveryFile(activeProject);
}

void AutosaveService::timerCallback()
{
    if (activeProject != juce::File {}
        && trackDataModel.getProjectRevision() != lastRecoveredRevision)
        saveRecoveryNow();
}

juce::File AutosaveService::makeRecoveryFile(const juce::File& projectFile) const
{
    const auto identity = juce::String::toHexString(projectFile.getFullPathName().hashCode64());
    return recoveryRoot.getChildFile(identity + ".studioforge-recovery");
}
