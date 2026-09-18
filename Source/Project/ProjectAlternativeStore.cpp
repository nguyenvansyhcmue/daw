#include "ProjectAlternativeStore.h"

namespace
{
juce::File alternativesDirectory(const juce::File& projectFile)
{
    return projectFile.getParentDirectory().getChildFile(projectFile.getFileNameWithoutExtension() + " Alternatives");
}
}

juce::StringArray ProjectAlternativeStore::load(const juce::File& projectFile) const
{
    juce::StringArray alternatives;
    if (! projectFile.existsAsFile())
        return alternatives;

    juce::Array<juce::File> files;
    alternativesDirectory(projectFile).findChildFiles(files, juce::File::findFiles, false, "*.studioforge");
    files.sort();
    for (const auto& file : files)
        alternatives.add(file.getFullPathName());
    return alternatives;
}

juce::File ProjectAlternativeStore::makeAlternativeFile(const juce::File& projectFile,
                                                         const juce::String& name) const
{
    const auto requestedName = name.trim().isEmpty() ? "Alternative" : name.trim();
    const auto safeName = juce::File::createLegalFileName(requestedName);
    return alternativesDirectory(projectFile).getChildFile(safeName).withFileExtension("studioforge");
}
