#pragma once

#include <array>
#include <cstddef>

#include "../Project/ProjectTemplates.h"

enum class PerformanceRole : unsigned char
{
    vocal,
    guitar,
    bass,
    keyboard,
    drums,
    beat
};

struct PerformanceMember
{
    PerformanceRole role = PerformanceRole::vocal;
    juce::String displayName;
    int quantity = 0;
};

enum class PerformancePreset : unsigned char
{
    soloVocal,
    duet,
    vocalAndBand,
    custom
};

class PerformanceRoomModel final
{
public:
    static constexpr size_t memberCount = 6;

    PerformanceRoomModel()
    {
        reset(PerformancePreset::vocalAndBand);
    }

    void reset(PerformancePreset preset)
    {
        if (preset == PerformancePreset::custom)
        {
            selectedPreset = preset;
            return;
        }
        selectedPreset = preset;
        members = defaultMembers();
        applyPresetQuantities();
    }

    PerformancePreset getSelectedPreset() const noexcept { return selectedPreset; }
    const std::array<PerformanceMember, memberCount>& getMembers() const noexcept { return members; }
    std::array<PerformanceMember, memberCount>& getMembers() noexcept { return members; }

    int getTotalQuantity() const noexcept
    {
        auto total = 0;
        for (const auto& member : members)
            total += member.quantity;
        return total;
    }

    LiveSetupConfig createLiveSetup() const
    {
        LiveSetupConfig setup;
        setup.sessionMode = LiveSessionMode::audioAndMidi;
        setup.midiMode = MidiSessionMode::instrumentsAndBackingTrack;
        setup.vocalCount = quantityFor(PerformanceRole::vocal);
        setup.guitarCount = quantityFor(PerformanceRole::guitar);
        setup.bassCount = quantityFor(PerformanceRole::bass);
        setup.keyboardCount = quantityFor(PerformanceRole::keyboard);
        setup.drumCount = quantityFor(PerformanceRole::drums);
        setup.backingTrackCount = quantityFor(PerformanceRole::beat);
        appendNames(setup.vocalNames, PerformanceRole::vocal);
        appendNames(setup.guitarNames, PerformanceRole::guitar);
        appendNames(setup.bassNames, PerformanceRole::bass);
        appendNames(setup.keyboardNames, PerformanceRole::keyboard);
        appendNames(setup.drumNames, PerformanceRole::drums);
        appendNames(setup.backingTrackNames, PerformanceRole::beat);
        return setup;
    }

private:
    static std::array<PerformanceMember, memberCount> defaultMembers()
    {
        return {{
            { PerformanceRole::vocal, "Vocal", 1 },
            { PerformanceRole::guitar, "Guitar", 0 },
            { PerformanceRole::bass, "Bass", 0 },
            { PerformanceRole::keyboard, "Keyboard", 0 },
            { PerformanceRole::drums, "Drums", 0 },
            { PerformanceRole::beat, "Beat", 0 }
        }};
    }

    void applyPresetQuantities()
    {
        for (auto& member : members)
            member.quantity = 0;

        memberFor(PerformanceRole::vocal).quantity = selectedPreset == PerformancePreset::duet ? 2 : 1;
        if (selectedPreset == PerformancePreset::vocalAndBand)
        {
            memberFor(PerformanceRole::guitar).quantity = 1;
            memberFor(PerformanceRole::bass).quantity = 1;
            memberFor(PerformanceRole::keyboard).quantity = 1;
            memberFor(PerformanceRole::drums).quantity = 1;
        }
        if (selectedPreset == PerformancePreset::soloVocal || selectedPreset == PerformancePreset::duet)
            memberFor(PerformanceRole::beat).quantity = 1;
    }

    PerformanceMember& memberFor(PerformanceRole role)
    {
        for (auto& member : members)
            if (member.role == role)
                return member;
        return members.front();
    }

    int quantityFor(PerformanceRole role) const
    {
        for (const auto& member : members)
            if (member.role == role)
                return member.quantity;
        return 0;
    }

    void appendNames(juce::StringArray& destination, PerformanceRole role) const
    {
        for (const auto& member : members)
            if (member.role == role && member.quantity > 0)
                for (int index = 0; index < member.quantity; ++index)
                    destination.add(member.displayName + " " + juce::String(index + 1));
    }

    PerformancePreset selectedPreset = PerformancePreset::vocalAndBand;
    std::array<PerformanceMember, memberCount> members {};
};
