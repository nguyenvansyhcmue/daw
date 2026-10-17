#include "ProjectBackupStore.h"

juce::File ProjectBackupStore::backupsDirectory(const juce::File& projectFile)
{
    return projectFile.getParentDirectory().getChildFile(projectFile.getFileNameWithoutExtension() + " Backups");
}

juce::StringArray ProjectBackupStore::load(const juce::File& projectFile) const
{
    juce::StringArray backups;
    if (projectFile == juce::File {})
        return backups;

    juce::Array<juce::File> files;
    backupsDirectory(projectFile).findChildFiles(files, juce::File::findFiles, false, "*.studioforge");
    files.sort();
    for (const auto& file : files)
        backups.add(file.getFullPathName());
    return backups;
}

juce::Result ProjectBackupStore::captureCurrentVersion(const juce::File& projectFile) const
{
    if (! projectFile.existsAsFile())
        return juce::Result::ok();

    const auto directory = backupsDirectory(projectFile);
    if (! directory.exists() && ! directory.createDirectory())
        return juce::Result::fail("Cannot create the project backup directory");

    const auto timestamp = juce::Time::getCurrentTime().formatted("yyyy-MM-dd HH.mm.ss.SSS");
    const auto destination = directory.getNonexistentChildFile(timestamp, ".studioforge", false);
    if (! projectFile.copyFileTo(destination))
        return juce::Result::fail("Cannot create a project backup");

    prune(projectFile);
    return juce::Result::ok();
}

void ProjectBackupStore::prune(const juce::File& projectFile) const
{
    auto backups = load(projectFile);
    while (backups.size() > maximumBackups)
    {
        juce::File(backups[0]).deleteFile();
        backups.remove(0);
    }
}
