#include "CoreAudioInputProbe.h"

#if JUCE_MAC
 #include <CoreAudio/CoreAudio.h>

namespace
{
bool readDeviceName(AudioObjectID id, juce::String& result) noexcept
{
    AudioObjectPropertyAddress address { kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain };
    CFStringRef name = nullptr;
    UInt32 size = sizeof(name);
    if (AudioObjectGetPropertyData(id, &address, 0, nullptr, &size, &name) != noErr || name == nullptr)
        return false;
    result = juce::String::fromCFString(name);
    CFRelease(name);
    return true;
}
}

void CoreAudioInputProbe::selectDevice(const juce::String& name) noexcept
{
    deviceId.store(0, std::memory_order_release);
    AudioObjectPropertyAddress address { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr)
        return;
    juce::Array<AudioObjectID> ids;
    ids.insertMultiple(0, 0, static_cast<int>(size / sizeof(AudioObjectID)));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, ids.getRawDataPointer()) != noErr)
        return;
    for (const auto id : ids)
    {
        juce::String deviceName;
        if (readDeviceName(id, deviceName) && deviceName == name)
        {
            deviceId.store(static_cast<uint32_t>(id), std::memory_order_release);
            return;
        }
    }
}

CoreAudioInputProbe::State CoreAudioInputProbe::read() const noexcept
{
    State state;
    const auto id = static_cast<AudioObjectID>(deviceId.load(std::memory_order_acquire));
    if (id == 0)
        return state;

    AudioObjectPropertyAddress gainAddress { kAudioDevicePropertyVolumeScalar,
                                              kAudioDevicePropertyScopeInput,
                                              kAudioObjectPropertyElementMain };
    Float32 gain = 0.0f;
    UInt32 size = sizeof(gain);
    if (AudioObjectGetPropertyData(id, &gainAddress, 0, nullptr, &size, &gain) == noErr)
    {
        state.gain = gain;
        state.gainAvailable = true;
    }

    AudioObjectPropertyAddress muteAddress { kAudioDevicePropertyMute,
                                              kAudioDevicePropertyScopeInput,
                                              kAudioObjectPropertyElementMain };
    UInt32 muted = 0;
    size = sizeof(muted);
    if (AudioObjectGetPropertyData(id, &muteAddress, 0, nullptr, &size, &muted) == noErr)
        state.muted = static_cast<int>(muted);
    return state;
}
#else
void CoreAudioInputProbe::selectDevice(const juce::String&) noexcept {}
CoreAudioInputProbe::State CoreAudioInputProbe::read() const noexcept { return {}; }
#endif
