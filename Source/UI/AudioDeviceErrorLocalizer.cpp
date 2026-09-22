#include "AudioDeviceErrorLocalizer.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
constexpr auto technicalTitle = "Error when trying to open audio device!";

juce::String fromUtf8(const char* text)
{
    return juce::String::fromUTF8(text);
}

juce::String createFocusriteMessage()
{
    return fromUtf8("StudioForge ch\u01B0a th\u1EC3 k\u1EBFt n\u1ED1i v\u1EDBi Focusrite. H\u00E3y ki\u1EC3m tra thi\u1EBFt b\u1ECB \u0111\u00E3 \u0111\u01B0\u1EE3c c\u1EAFm, "
                    "b\u1EADt ngu\u1ED3n v\u00E0 ch\u1ECDn l\u1EA1i Focusrite USB ASIO.");
}

juce::String createSampleRateMessage()
{
    return fromUtf8("Mic v\u00E0 loa \u0111ang d\u00F9ng sample rate kh\u00E1c nhau. H\u00E3y ch\u1ECDn c\u00E1c thi\u1EBFt b\u1ECB c\u00F9ng 48 kHz "
                    "ho\u1EB7c d\u00F9ng Focusrite ASIO cho c\u1EA3 input v\u00E0 output.");
}

juce::String createDriverMessage()
{
    return fromUtf8("Driver \u00E2m thanh ch\u01B0a s\u1EB5n s\u00E0ng. H\u00E3y ki\u1EC3m tra thi\u1EBFt b\u1ECB \u0111\u00E3 k\u1EBFt n\u1ED1i v\u00E0 th\u1EED ch\u1ECDn l\u1EA1i driver.");
}
}

void AudioDeviceErrorLocalizer::localizePendingOpenDeviceFailure()
{
    auto& desktop = juce::Desktop::getInstance();

    for (auto index = 0; index < desktop.getNumComponents(); ++index)
    {
        auto* alert = dynamic_cast<juce::AlertWindow*>(desktop.getComponent(index));
        if (alert == nullptr || ! isAudioDeviceFailure(*alert))
            continue;

        const auto technicalMessage = alert->getDescription();
        alert->setName(fromUtf8("Thi\u1EBFt b\u1ECB \u00E2m thanh ch\u01B0a s\u1EB5n s\u00E0ng"));
        alert->setMessage(createFriendlyMessage(technicalMessage));

        if (auto* button = alert->getButton(0))
            button->setButtonText(fromUtf8("\u0110\u00E3 hi\u1EC3u"));
    }
}

bool AudioDeviceErrorLocalizer::isAudioDeviceFailure(const juce::AlertWindow& alert)
{
    return alert.getName() == technicalTitle
        || alert.getDescription().contains(technicalTitle);
}

juce::String AudioDeviceErrorLocalizer::createFriendlyMessage(const juce::String& technicalMessage)
{
    if (technicalMessage.containsIgnoreCase("focusrite"))
        return createFocusriteMessage();

    if (technicalMessage.containsIgnoreCase("don't share a common sample rate"))
        return createSampleRateMessage();

    if (technicalMessage.containsIgnoreCase("driver failed"))
        return createDriverMessage();

    return fromUtf8("StudioForge ch\u01B0a th\u1EC3 m\u1EDF thi\u1EBFt b\u1ECB \u00E2m thanh n\u00E0y. H\u00E3y ki\u1EC3m tra k\u1EBFt n\u1ED1i, "
                    "sau \u0111\u00F3 ch\u1ECDn l\u1EA1i input v\u00E0 output.");
}
