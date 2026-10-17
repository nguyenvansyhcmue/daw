#pragma once

#include <juce_core/juce_core.h>

class RecentProjectsStore final
{
public:
    explicit RecentProjectsStore(juce::File storageFile = defaultStorageFile());

    juce::StringArray load() const;
    void add(const juce::File& projectFile) const;
    void clear() const;

    static juce::File defaultStorageFile();

private:
    bool writeEntries(const juce::StringArray& entries) const;

    juce::File storage;
    static constexpr int maximumEntries = 12;
};
