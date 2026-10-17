#include "RecentProjectsStore.h"

RecentProjectsStore::RecentProjectsStore(juce::File storageFile) : storage(std::move(storageFile)) {}

juce::File RecentProjectsStore::defaultStorageFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("StudioForge").getChildFile("recent-projects.txt");
}

juce::StringArray RecentProjectsStore::load() const
{
    juce::StringArray entries;
    if (! storage.existsAsFile()) return entries;
    for (const auto& line : juce::StringArray::fromLines(storage.loadFileAsString()))
    {
        const auto path = line.trim();
        const auto file = juce::File(path);
        if (path.isNotEmpty() && file.existsAsFile() && file.hasFileExtension("studioforge")
            && ! entries.contains(path, true))
            entries.add(path);
        if (entries.size() == maximumEntries) break;
    }
    return entries;
}

void RecentProjectsStore::add(const juce::File& projectFile) const
{
    if (! projectFile.existsAsFile() || ! projectFile.hasFileExtension("studioforge")) return;
    auto entries = load();
    entries.removeString(projectFile.getFullPathName(), true);
    entries.insert(0, projectFile.getFullPathName());
    while (entries.size() > maximumEntries) entries.remove(entries.size() - 1);
    writeEntries(entries);
}

void RecentProjectsStore::clear() const
{
    writeEntries({});
}

bool RecentProjectsStore::writeEntries(const juce::StringArray& entries) const
{
    const auto directory = storage.getParentDirectory();
    if (! directory.exists() && ! directory.createDirectory())
        return false;

    const auto temporary = storage.getSiblingFile(storage.getFileName() + ".tmp");
    temporary.deleteFile();
    if (! temporary.replaceWithText(entries.joinIntoString("\n")))
    {
        temporary.deleteFile();
        return false;
    }

    const auto committed = storage.existsAsFile() ? temporary.replaceFileIn(storage)
                                                  : temporary.moveFileTo(storage);
    if (! committed)
        temporary.deleteFile();
    return committed;
}
