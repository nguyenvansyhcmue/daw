#pragma once

#include <juce_core/juce_core.h>

// Immutable save-history snapshots. They are intentionally separate from
// crash recovery: recovery holds the latest unsaved edit while backups retain
// known-good project documents from before manual saves.
class ProjectBackupStore final
{
public:
    static constexpr int maximumBackups = 10;

    [[nodiscard]] juce::Result captureCurrentVersion(const juce::File& projectFile) const;
    [[nodiscard]] juce::StringArray load(const juce::File& projectFile) const;

private:
    [[nodiscard]] static juce::File backupsDirectory(const juce::File& projectFile);
    void prune(const juce::File& projectFile) const;
};
