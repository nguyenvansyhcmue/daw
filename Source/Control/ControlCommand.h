#pragma once

#include <juce_core/juce_core.h>

#include <optional>

enum class ControlCommandType : uint8_t { setKeySignature, setTranspose, setEffectValue, setLevel, setBypass };
enum class ControlTarget : uint8_t { mic1, mic2, music, master };
enum class ControlEffect : uint8_t { pitchCorrection, reverbShort, reverbLong, echo, tune, lofi };

struct KeySignatureValue { int rootNote = 0; bool minor = false; };

struct ControlCommand
{
    ControlCommandType type = ControlCommandType::setEffectValue;
    ControlTarget target = ControlTarget::mic1;
    ControlEffect effect = ControlEffect::pitchCorrection;
    std::optional<KeySignatureValue> keySignature;
    float value = 0.0f;
    bool bypassed = false;

    static ControlCommand keySignatureFor(ControlTarget target, int rootNote, bool minor)
    {
        ControlCommand command;
        command.type = ControlCommandType::setKeySignature;
        command.target = target;
        command.effect = ControlEffect::pitchCorrection;
        command.keySignature = { rootNote, minor };
        return command;
    }

    juce::Result validate() const
    {
        if (type == ControlCommandType::setKeySignature)
        {
            if (effect != ControlEffect::pitchCorrection || ! keySignature.has_value()
                || keySignature->rootNote < 0 || keySignature->rootNote > 11)
                return juce::Result::fail("Key-signature commands require pitchCorrection and a root note from 0 to 11");
            return juce::Result::ok();
        }
        if (! std::isfinite(value) || value < 0.0f || value > 1.0f)
            return juce::Result::fail("Control values must be finite and normalized from 0 to 1");
        return juce::Result::ok();
    }
};
