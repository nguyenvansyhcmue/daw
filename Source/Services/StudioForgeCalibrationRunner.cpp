#include "StudioForgeCalibrationRunner.h"

#include "../AudioEngine/AudioEngine.h"
#include "../Media/MediaReloadService.h"
#include "../Models/TrackDataModel.h"
#include "../Plugins/PluginHostService.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
juce::Result configure(AudioEffectProcessor& effect, const StudioForgeCalibrationRunner::Settings& settings)
{
    const auto textValue = [&effect] (std::initializer_list<const char*> names, const juce::String& value)
    {
        for (const auto* name : names)
        {
            if (! effect.setParameterText(name, value))
                continue;

            // A text setter can coerce invalid enum text to item zero.
            // Verify the displayed state instead of trusting the setter.
            if (effect.getParameterText(name).containsIgnoreCase(value))
                return true;
        }
        return false;
    };

    const auto text = [&effect] (std::initializer_list<const char*> names, float value)
    {
        for (const auto* name : names)
            if (effect.setParameterText(name, juce::String(value))) return true;
        return false;
    };

    // VibeAutotune and Artist use different display labels for equivalent
    // controls. The runner accepts both so the same matrix can A/B them.
    if (! text({ "Retune", "Retune Speed" }, settings.retuneMs)
        || ! text({ "Flex", "Flex-Tune", "Flex Tune" }, settings.flexTune)
        || ! text({ "Humanize" }, settings.humanize)
        || ! text({ "Vibrato", "Natural Vibrato" }, settings.naturalVibrato))
        return juce::Result::fail("Plug-in does not expose Retune, Flex, Humanize, and Vibrato controls");

    if (effect.getName().containsIgnoreCase("Auto-Tune Artist"))
    {
        // Artist's enums do not parse display strings. These normalized
        // indices are the fixed Alto / F# / Natural Minor reference preset.
        if (! effect.setParameterValue("Input Type", 1.0f / 4.0f)
            || ! effect.setParameterValue("Key", 6.0f / 11.0f)
            || ! effect.setParameterValue("Scale", 1.0f / 27.0f))
            return juce::Result::fail("Auto-Tune Artist does not expose its expected key controls");
    }
    else if (! textValue({ "Input Type", "Voice", "inputType" }, "Alto")
             || ! textValue({ "Key", "key" }, "F#")
             || ! textValue({ "Scale", "scale" }, "Natural Minor"))
    {
        return juce::Result::fail("VibeAutotune could not lock Alto / F# / Natural Minor");
    }

    // AU instances do not reliably inherit the runner environment. The
    // Vibe-only diagnostic parameters make the calibration candidate explicit
    // at the hosted instance while defaults remain -1 in production.
    if (effect.getName().containsIgnoreCase("VibeAutotune"))
    {
        const auto setDiagnosticFromEnvironment = [&effect] (const char* environment,
                                                               const char* parameter,
                                                               float maximum)
        {
            const auto* value = std::getenv (environment);
            if (value == nullptr)
                return true;

            char* end = nullptr;
            const auto requested = std::strtof (value, &end);
            if (end == value || *end != '\0' || ! std::isfinite (requested)
                || requested < 0.0f || requested > maximum)
                return false;

            // The diagnostic parameter range is [-1, maximum]. Do not use
            // getValueForText here: some AU hosts treat bare numeric text as
            // already-normalised, collapsing different candidates together.
            const auto normalised = (requested + 1.0f) / (maximum + 1.0f);
            if (! effect.setParameterValue (parameter, normalised))
                return false;

            const auto observed = effect.getParameterText (parameter).getFloatValue();
            if (std::abs (observed - requested) > 0.11f)
                return false;

            std::cerr << "calibration " << parameter << '=' << requested
                      << " (observed " << observed << ")\n";
            return true;
        };
        if (! setDiagnosticFromEnvironment ("VIBE_DIAG_RETUNE10_T90_MS", "Diagnostic Retune 10 T90", 80.0f)
            || ! setDiagnosticFromEnvironment ("VIBE_DIAG_RETUNE25_T90_MS", "Diagnostic Retune 25 T90", 140.0f))
            return juce::Result::fail("VibeAutotune does not expose Retune-knot diagnostic parameters");

        const auto setUnitDiagnostic = [&effect] (const char* environment, const char* parameter,
                                                   float minimum, float maximum)
        {
            const auto* value = std::getenv (environment);
            if (value == nullptr) return true;
            char* end = nullptr;
            const auto requested = std::strtof (value, &end);
            if (end == value || *end != '\0' || ! std::isfinite (requested)
                || requested < minimum || requested > maximum)
                return false;
            if (! effect.setParameterValue (parameter, (requested - minimum) / (maximum - minimum)))
                return false;
            const auto observed = effect.getParameterText (parameter).getFloatValue();
            std::cerr << "calibration " << parameter << '=' << requested
                      << " (observed " << observed << ")\n";
            return std::abs (observed - requested) <= 0.011f;
        };
        if (! setUnitDiagnostic ("VIBE_DIAG_FLEX_RESIDUAL", "Diagnostic Flex Residual", 0.5f, 1.5f)
            || ! setUnitDiagnostic ("VIBE_DIAG_HUMANIZE_SCALE", "Diagnostic Humanize Scale", 0.5f, 1.5f)
            || ! setUnitDiagnostic ("VIBE_DIAG_NATURAL_PHASE", "Diagnostic Natural Phase", 0.0f, 2.5f)
            || ! setUnitDiagnostic ("VIBE_DIAG_NATURAL_HARD_TUNE_SURFACE", "Diagnostic Natural Hard Tune Surface", 0.0f, 1.0f))
            return juce::Result::fail("VibeAutotune does not expose joint-calibration parameters");
    }
    return juce::Result::ok();
}

juce::String outputName(size_t index, const StudioForgeCalibrationRunner::Settings& settings)
{
    return "case_" + juce::String(static_cast<int>(index))
        + "_retune" + juce::String(settings.retuneMs, 1)
        + "_flex" + juce::String(settings.flexTune, 1)
        + "_humanize" + juce::String(settings.humanize, 1)
        + "_vibrato" + juce::String(settings.naturalVibrato, 1) + ".wav";
}
}

juce::Result StudioForgeCalibrationRunner::run(const Request& request, juce::String& report)
{
    report.clear();
    if (! request.input.existsAsFile()) return juce::Result::fail("Input file does not exist");
    if (request.sampleRate <= 0.0 || request.blockSize <= 0) return juce::Result::fail("Invalid render format");
    if (! request.outputDirectory.createDirectory()) return juce::Result::fail("Cannot create output directory");

    std::shared_ptr<juce::AudioBuffer<float>> input;
    if (const auto decoded = MediaReloadService::decode(request.input, input); decoded.failed()) return decoded;

    PluginHostService host(request.outputDirectory.getChildFile("StudioForgeCalibrationPluginCatalog.xml"));
    if (request.pluginPath != juce::File {})
    {
        if (! request.pluginPath.exists())
            return juce::Result::fail("Plug-in path does not exist: " + request.pluginPath.getFullPathName());
        if (const auto scanned = host.scanVst3(request.pluginPath); scanned.failed())
            return scanned;
    }
    else if (host.getKnownPlugins().isEmpty())
    {
       #if JUCE_MAC
        if (const auto scanned = host.scanAudioUnits(); scanned.failed()) return scanned;
       #else
        if (const auto scanned = host.scanDefaultVst3Locations(); scanned.failed()) return scanned;
       #endif
    }
    const auto matches = host.findKnownPlugins(request.pluginQuery);
    if (matches.isEmpty()) return juce::Result::fail("No plug-in matches: " + request.pluginQuery);
    const auto cases = request.cases.empty() ? std::vector<Settings> { {} } : request.cases;
    report = "case,retune_ms,flex_tune,humanize,natural_vibrato,output\n";

    for (size_t index = 0; index < cases.size(); ++index)
    {
        juce::String error;
        auto effect = host.createEffect(matches[0], request.sampleRate, request.blockSize, error);
        if (effect == nullptr) return juce::Result::fail("Cannot load " + matches[0].name + ": " + error);
        if (const auto configured = configure(*effect, cases[index]); configured.failed()) return configured;

        TrackDataModel model;
        model.setSampleRate(request.sampleRate);
        model.ensureTrackCount(1);
        model.addClipToTrack(0, request.input, 0.0, input);
        AudioEngine engine(&model);
        engine.setFxProcessor(0, 0, std::move(effect));
        const auto output = request.outputDirectory.getChildFile(outputName(index, cases[index]));
        AudioEngine::OfflineRenderOptions options;
        options.sampleRate = request.sampleRate;
        options.blockSize = request.blockSize;
        options.bitDepth = 24;
        if (const auto rendered = engine.renderOfflineAudio(output, options); rendered.failed()) return rendered;
        report << index << ',' << cases[index].retuneMs << ',' << cases[index].flexTune << ','
               << cases[index].humanize << ',' << cases[index].naturalVibrato << ','
               << output.getFullPathName().quoted() << '\n';
    }
    return juce::Result::ok();
}
