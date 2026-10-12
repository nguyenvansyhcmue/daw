#pragma once

#include <juce_core/juce_core.h>

#include "../AudioEngine/GlobalScaleContext.h"

enum class PitchClass : uint8_t { c = 0, cSharp, d, dSharp, e, f, fSharp, g, gSharp, a, aSharp, b };
struct MusicalKey
{
    PitchClass root = PitchClass::c;
    MusicalScale scale = MusicalScale::major;

    int rootNote() const noexcept { return static_cast<int>(root); }
    bool isMinor() const noexcept { return scale == MusicalScale::minor; }
    juce::String rootName() const
    {
        static constexpr const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[static_cast<size_t>(root)];
    }
    juce::String scaleName() const { return scale == MusicalScale::minor ? "Natural Minor" : "Major"; }
    juce::String displayName() const { return rootName() + (isMinor() ? " minor" : " major"); }

    static std::optional<MusicalKey> parse(juce::String rootText, juce::String scaleText)
    {
        rootText = rootText.trim().toUpperCase().replaceCharacter(juce::juce_wchar(0x266f), '#').replaceCharacter(juce::juce_wchar(0x266d), 'B');
        static const std::pair<const char*, PitchClass> aliases[] {
            { "C", PitchClass::c }, { "C#", PitchClass::cSharp }, { "DB", PitchClass::cSharp }, { "D", PitchClass::d },
            { "D#", PitchClass::dSharp }, { "EB", PitchClass::dSharp }, { "E", PitchClass::e }, { "F", PitchClass::f },
            { "F#", PitchClass::fSharp }, { "GB", PitchClass::fSharp }, { "G", PitchClass::g }, { "G#", PitchClass::gSharp },
            { "AB", PitchClass::gSharp }, { "A", PitchClass::a }, { "A#", PitchClass::aSharp }, { "BB", PitchClass::aSharp }, { "B", PitchClass::b }
        };
        for (const auto& [name, note] : aliases)
            if (rootText == name)
            {
                const auto normalizedScale = scaleText.trim().toLowerCase();
                if (normalizedScale.contains("minor") || normalizedScale == "min" || normalizedScale == "aeolian") return MusicalKey { note, MusicalScale::minor };
                if (normalizedScale.contains("major") || normalizedScale == "maj" || normalizedScale == "ionian") return MusicalKey { note, MusicalScale::major };
                return std::nullopt;
            }
        return std::nullopt;
    }
};
