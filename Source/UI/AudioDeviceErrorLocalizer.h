#pragma once

namespace juce
{
class AlertWindow;
class String;
}

class AudioDeviceErrorLocalizer final
{
public:
    static void localizePendingOpenDeviceFailure();

private:
    static bool isAudioDeviceFailure(const juce::AlertWindow& alert);
    static juce::String createFriendlyMessage(const juce::String& technicalMessage);
};
