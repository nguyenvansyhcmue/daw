#pragma once

#include "../AudioEngine/AudioEffectProcessor.h"

#include <vector>

// A semantic layer between arbitrary AU/VST parameter names and StudioForge's
// performer-facing macros.  This deliberately uses parameter metadata rather
// than plug-in indices or vendor-specific UI names.
enum class PluginParameterSemantic : uint8_t
{
    unknown = 0,
    amount,
    level,
    reverbTime,
    delayFeedback,
    delayTime,
    tone,
    dynamics,
    vocalTuning
};

struct SmartMacroTarget
{
    juce::String parameterIdentifier;
    juce::String displayName;
    PluginParameterSemantic semantic = PluginParameterSemantic::unknown;
    // Macro values are always normalised.  These bounds are intentionally
    // conservative so a generic third-party plug-in cannot be driven into a
    // dangerous feedback/output region by a single live knob.
    float minimum = 0.0f;
    float maximum = 1.0f;
};

struct SmartMacroProposal
{
    juce::String displayName = "Unassigned";
    std::vector<SmartMacroTarget> targets;
    float confidence = 0.0f;
    bool needsReview = true;
};

class PluginSemanticMapper final
{
public:
    static PluginParameterSemantic classify(const EffectParameterDescriptor& parameter);
    static SmartMacroProposal propose(const juce::String& pluginName,
                                      const std::vector<EffectParameterDescriptor>& parameters);
    static juce::String displayName(PluginParameterSemantic semantic);
};
