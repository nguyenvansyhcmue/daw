#include "PluginSemanticMapper.h"

#include <algorithm>

namespace
{
juce::String normalise(juce::String value)
{
    return value.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
}

bool hasAny(const juce::String& value, std::initializer_list<const char*> tokens)
{
    return std::any_of(tokens.begin(), tokens.end(), [&value] (const char* token)
    {
        return value.contains(token);
    });
}

bool isReverb(const juce::String& plugin)
{
    return hasAny(plugin, { "reverb", "room", "hall", "plate", "chamber", "verb" });
}

bool isDelay(const juce::String& plugin)
{
    return hasAny(plugin, { "delay", "echo", "tape", "repeat" });
}

bool isDynamics(const juce::String& plugin)
{
    return hasAny(plugin, { "compress", "limiter", "gate", "dynamics" });
}

SmartMacroTarget makeTarget(const EffectParameterDescriptor& descriptor, PluginParameterSemantic semantic)
{
    auto target = SmartMacroTarget { descriptor.identifier, descriptor.name, semantic };
    switch (semantic)
    {
        case PluginParameterSemantic::delayFeedback: target.maximum = 0.65f; break;
        case PluginParameterSemantic::level:         target.maximum = 0.80f; break;
        case PluginParameterSemantic::amount:        target.maximum = 0.85f; break;
        case PluginParameterSemantic::dynamics:      target.minimum = 0.15f; target.maximum = 0.78f; break;
        default: break;
    }
    return target;
}

const EffectParameterDescriptor* firstWith(const std::vector<EffectParameterDescriptor>& parameters,
                                           PluginParameterSemantic wanted)
{
    const auto found = std::find_if(parameters.begin(), parameters.end(), [wanted] (const auto& parameter)
    {
        return PluginSemanticMapper::classify(parameter) == wanted;
    });
    return found == parameters.end() ? nullptr : &*found;
}

void addIfPresent(SmartMacroProposal& proposal, const EffectParameterDescriptor* parameter,
                  PluginParameterSemantic semantic)
{
    if (parameter == nullptr || parameter->identifier.isEmpty()) return;
    proposal.targets.push_back(makeTarget(*parameter, semantic));
}
}

PluginParameterSemantic PluginSemanticMapper::classify(const EffectParameterDescriptor& parameter)
{
    const auto name = normalise(parameter.name + parameter.identifier);
    if (hasAny(name, { "bypass", "enable", "onoff" })) return PluginParameterSemantic::unknown;
    if (hasAny(name, { "feedback", "repeat", "regeneration" })) return PluginParameterSemantic::delayFeedback;
    if (hasAny(name, { "decay", "rt60", "reverbtime", "reverberationtime" })) return PluginParameterSemantic::reverbTime;
    if (hasAny(name, { "delaytime", "delay", "synctime" })) return PluginParameterSemantic::delayTime;
    if (hasAny(name, { "retunespeed", "retune", "humanize", "flextune", "tracking" })) return PluginParameterSemantic::vocalTuning;
    if (hasAny(name, { "threshold", "ratio", "makeup", "makeupgain", "compress", "drive" })) return PluginParameterSemantic::dynamics;
    if (hasAny(name, { "highcut", "lowcut", "damping", "tone", "brightness", "filter" })) return PluginParameterSemantic::tone;
    if (hasAny(name, { "send", "return", "output", "outgain", "volume", "level", "gain" })) return PluginParameterSemantic::level;
    if (hasAny(name, { "drywet", "mix", "wet", "blend", "amount" })) return PluginParameterSemantic::amount;
    return PluginParameterSemantic::unknown;
}

juce::String PluginSemanticMapper::displayName(PluginParameterSemantic semantic)
{
    switch (semantic)
    {
        case PluginParameterSemantic::amount: return "Amount";
        case PluginParameterSemantic::level: return "Level";
        case PluginParameterSemantic::reverbTime: return "Space";
        case PluginParameterSemantic::delayFeedback: return "Echo";
        case PluginParameterSemantic::delayTime: return "Delay Time";
        case PluginParameterSemantic::tone: return "Tone";
        case PluginParameterSemantic::dynamics: return "Dynamics";
        case PluginParameterSemantic::vocalTuning: return "Vocal Tune";
        case PluginParameterSemantic::unknown: break;
    }
    return "Unassigned";
}

SmartMacroProposal PluginSemanticMapper::propose(const juce::String& pluginName,
                                                  const std::vector<EffectParameterDescriptor>& parameters)
{
    const auto plugin = normalise(pluginName);
    auto proposal = SmartMacroProposal {};

    if (isReverb(plugin))
    {
        proposal.displayName = "Vocal Space";
        addIfPresent(proposal, firstWith(parameters, PluginParameterSemantic::level), PluginParameterSemantic::level);
        addIfPresent(proposal, firstWith(parameters, PluginParameterSemantic::amount), PluginParameterSemantic::amount);
        addIfPresent(proposal, firstWith(parameters, PluginParameterSemantic::reverbTime), PluginParameterSemantic::reverbTime);
        proposal.confidence = proposal.targets.size() >= 2 ? 0.92f : 0.68f;
    }
    else if (isDelay(plugin))
    {
        proposal.displayName = "Lead Delay";
        addIfPresent(proposal, firstWith(parameters, PluginParameterSemantic::level), PluginParameterSemantic::level);
        addIfPresent(proposal, firstWith(parameters, PluginParameterSemantic::delayFeedback), PluginParameterSemantic::delayFeedback);
        addIfPresent(proposal, firstWith(parameters, PluginParameterSemantic::tone), PluginParameterSemantic::tone);
        proposal.confidence = proposal.targets.size() >= 2 ? 0.90f : 0.66f;
    }
    else if (isDynamics(plugin))
    {
        proposal.displayName = "Vocal Dynamics";
        addIfPresent(proposal, firstWith(parameters, PluginParameterSemantic::dynamics), PluginParameterSemantic::dynamics);
        addIfPresent(proposal, firstWith(parameters, PluginParameterSemantic::level), PluginParameterSemantic::level);
        proposal.confidence = proposal.targets.size() >= 2 ? 0.84f : 0.62f;
    }
    else if (const auto* tuning = firstWith(parameters, PluginParameterSemantic::vocalTuning); tuning != nullptr)
    {
        proposal.displayName = "Vocal Tune";
        addIfPresent(proposal, tuning, PluginParameterSemantic::vocalTuning);
        addIfPresent(proposal, firstWith(parameters, PluginParameterSemantic::amount), PluginParameterSemantic::amount);
        proposal.confidence = 0.80f;
    }
    else
    {
        proposal.displayName = "Plugin Amount";
        addIfPresent(proposal, firstWith(parameters, PluginParameterSemantic::amount), PluginParameterSemantic::amount);
        addIfPresent(proposal, firstWith(parameters, PluginParameterSemantic::level), PluginParameterSemantic::level);
        proposal.confidence = proposal.targets.empty() ? 0.0f : 0.55f;
    }

    proposal.needsReview = proposal.targets.empty() || proposal.confidence < 0.70f;
    if (proposal.targets.empty()) proposal.displayName = "Unassigned";
    return proposal;
}
