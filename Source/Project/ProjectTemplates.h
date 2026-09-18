#pragma once

#include "ProjectState.h"

enum class ProjectTemplate : uint8_t
{
    empty,
    audioRecording,
    midiProduction
};

class ProjectTemplates final
{
public:
    [[nodiscard]] static ProjectState create(ProjectTemplate projectTemplate);
    [[nodiscard]] static juce::String getName(ProjectTemplate projectTemplate);
    [[nodiscard]] static juce::String getDescription(ProjectTemplate projectTemplate);
};
