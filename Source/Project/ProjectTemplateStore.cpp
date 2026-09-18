#include "ProjectTemplateStore.h"

ProjectTemplateStore::ProjectTemplateStore(juce::File directory)
    : templateDirectory(std::move(directory))
{
}

juce::File ProjectTemplateStore::defaultTemplateDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("StudioForge")
        .getChildFile("Templates");
}

juce::StringArray ProjectTemplateStore::load() const
{
    juce::StringArray templates;
    if (! templateDirectory.isDirectory())
        return templates;

    juce::Array<juce::File> files;
    templateDirectory.findChildFiles(files, juce::File::findFiles, false, "*.studioforge-template");
    files.sort();
    for (const auto& file : files)
        templates.add(file.getFullPathName());
    return templates;
}

juce::File ProjectTemplateStore::makeTemplateFile(const juce::String& name) const
{
    const auto requestedName = name.trim().isEmpty() ? "Untitled Template" : name.trim();
    const auto safeName = juce::File::createLegalFileName(requestedName);
    return templateDirectory.getChildFile(safeName).withFileExtension("studioforge-template");
}
