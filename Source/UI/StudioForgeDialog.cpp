#include "StudioForgeDialog.h"

juce::String StudioForgeDialog::fromUtf8(const char* text)
{
    return juce::String::fromUTF8(text);
}

void StudioForgeDialog::showWarning(const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                           title,
                                           message);
}
