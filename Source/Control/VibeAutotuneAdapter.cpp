#include "VibeAutotuneAdapter.h"

namespace
{
juce::String normalized(juce::String value)
{
    return value.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
}

juce::String findAlias(const std::vector<EffectParameterDescriptor>& parameters,
                       const juce::StringArray& aliases)
{
    for (const auto& alias : aliases)
        for (const auto& parameter : parameters)
            if (normalized(parameter.identifier) == alias || normalized(parameter.name) == alias)
                return parameter.identifier.isNotEmpty() ? parameter.identifier : parameter.name;
    return {};
}
}

VibeAutotuneAdapter::Result VibeAutotuneAdapter::resolve(const AudioEffectProcessor& effect,
                                                          const juce::String& configuredRoot,
                                                          const juce::String& configuredScale) const
{
    Result result;
    const auto parameters = effect.getParameterDescriptors();
    if (parameters.empty())
    {
        // Older/custom wrappers do not enumerate. A persisted, explicit route
        // remains a controlled compatibility fallback, never a parameter index.
        if (configuredRoot.isEmpty() || configuredScale.isEmpty())
        {
            result.error = "Pitch-correction plug-in does not expose parameter metadata";
            return result;
        }
        result = { true, { configuredRoot, configuredScale }, {} };
        return result;
    }

    const auto root = findAlias(parameters, { normalized(configuredRoot), "root", "key", "rootnote", "keyroot" });
    const auto scale = findAlias(parameters, { normalized(configuredScale), "scale", "mode", "keyscale" });
    if (root.isEmpty() || scale.isEmpty())
    {
        result.error = root.isEmpty() ? "VibeAutotune root/key parameter is missing"
                                      : "VibeAutotune scale/mode parameter is missing";
        return result;
    }
    result = { true, { root, scale }, {} };
    return result;
}

VibeAutotuneAdapter::Result VibeAutotuneAdapter::apply(AudioEffectProcessor& effect, const MusicalKey& key,
                                                        const juce::String& configuredRoot,
                                                        const juce::String& configuredScale) const
{
    auto result = resolve(effect, configuredRoot, configuredScale);
    if (! result.succeeded) return result;
    if (! effect.setParameterText(result.mapping.root, key.rootName())
        || ! effect.setParameterText(result.mapping.scale, key.scaleName()))
    {
        result.succeeded = false;
        result.error = "VibeAutotune rejected the resolved root or scale value";
        return result;
    }

    const auto appliedRoot = effect.getParameterText(result.mapping.root);
    const auto appliedScale = effect.getParameterText(result.mapping.scale);
    if ((appliedRoot.isNotEmpty() && normalized(appliedRoot) != normalized(key.rootName()))
        || (appliedScale.isNotEmpty() && normalized(appliedScale) != normalized(key.scaleName())))
    {
        result.succeeded = false;
        result.error = "VibeAutotune parameter readback does not match the requested key/scale";
    }
    return result;
}
