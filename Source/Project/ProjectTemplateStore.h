#pragma once

#include <juce_core/juce_core.h>

class ProjectTemplateStore final
{
public:
    explicit ProjectTemplateStore(juce::File directory = defaultTemplateDirectory());

    [[nodiscard]] juce::StringArray load() const;
    [[nodiscard]] juce::File makeTemplateFile(const juce::String& name) const;
    [[nodiscard]] static juce::File defaultTemplateDirectory();

private:
    juce::File templateDirectory;
};
