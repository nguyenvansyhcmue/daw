#pragma once

#include "PluginSemanticMapper.h"
#include "../Models/TrackDataModel.h"
#include "../Project/ProjectState.h"

#include <array>

class PerformanceMacroService final : public juce::ChangeBroadcaster,
                                      private juce::ChangeListener
{
public:
    static constexpr size_t macroCount = 3;

    enum class State : uint8_t { unassigned, needsReview, active, bypassed };

    struct BusOption
    {
        BusId id;
        juce::String name;
    };

    struct PluginOption
    {
        int slot = -1;
        juce::String identifier;
        juce::String name;
        bool bypassed = false;
    };

    struct Preview
    {
        bool available = false;
        bool bypassed = false;
        BusId bus;
        int pluginSlot = -1;
        juce::String busName;
        juce::String pluginIdentifier;
        juce::String pluginName;
        SmartMacroProposal proposal;
        juce::String error;
    };

    struct Snapshot
    {
        struct TargetState
        {
            juce::String parameterIdentifier;
            juce::String displayName;
            PluginParameterSemantic semantic = PluginParameterSemantic::unknown;
            juce::String valueText;
        };

        size_t macroIndex = 0;
        State state = State::unassigned;
        float value = 0.0f;
        BusId bus;
        int pluginSlot = -1;
        juce::String displayName = "Unassigned";
        juce::String busName;
        juce::String pluginIdentifier;
        juce::String pluginName;
        SmartMacroProposal mapping;
        // This is refreshed from the processor on the control/UI thread. It
        // lets a remote surface say exactly what a macro is changing rather
        // than inventing labels such as "short reverb" or "echo".
        std::vector<TargetState> targets;
    };

    struct Result
    {
        bool succeeded = false;
        Snapshot snapshot;
        juce::String error;
    };

    explicit PerformanceMacroService(TrackDataModel& model);
    ~PerformanceMacroService() override;

    std::vector<BusOption> getBusOptions() const;
    std::vector<PluginOption> getPluginOptions(BusId bus) const;
    Preview preview(BusId bus, int pluginSlot) const;
    Result apply(size_t macroIndex, BusId bus, int pluginSlot, bool confirmNeedsReview = false);
    Result clear(size_t macroIndex);
    Result setValue(size_t macroIndex, float normalisedValue);
    Snapshot getSnapshot(size_t macroIndex) const;
    void refresh();
    std::vector<PersistedPerformanceMacroState> createPersistedState() const;
    // Restoring never retargets by name. A missing/replaced plug-in or
    // parameter becomes Needs Review and must be approved by the performer.
    void restorePersistedState(const std::vector<PersistedPerformanceMacroState>& state);

private:
    static bool isValidMacroIndex(size_t macroIndex) noexcept;
    static juce::String identifierFor(const AudioEffectProcessor& processor);
    static bool hasChanged(const Snapshot& before, const Snapshot& after);
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refreshSnapshot(Snapshot& snapshot) const;
    void notifyChanged();

    TrackDataModel& dataModel;
    std::array<Snapshot, macroCount> macros;
    uint64_t revision = 0;
};
