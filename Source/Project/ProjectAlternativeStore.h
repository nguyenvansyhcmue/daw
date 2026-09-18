#pragma once

#include <juce_core/juce_core.h>

class ProjectAlternativeStore final
{
public:
    [[nodiscard]] juce::StringArray load(const juce::File& projectFile) const;
    [[nodiscard]] juce::File makeAlternativeFile(const juce::File& projectFile,
                                                  const juce::String& name) const;
};
