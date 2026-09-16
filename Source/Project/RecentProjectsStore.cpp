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
    storage.getParentDirectory().createDirectory();
    storage.replaceWithText(entries.joinIntoString("\n"));
}

void RecentProjectsStore::clear() const
{
    storage.getParentDirectory().createDirectory();
    storage.replaceWithText({});
}
