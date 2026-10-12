#include "Services/StudioForgeCalibrationRunner.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: StudioForgeCalibrationRunner INPUT.wav OUTPUT_DIRECTORY [PLUGIN_QUERY] [VST3_PATH]\n";
        return 64;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    StudioForgeCalibrationRunner::Request request;
    request.input = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    request.outputDirectory = juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
    if (argc >= 4) request.pluginQuery = argv[3];
    if (argc >= 5) request.pluginPath = juce::File::getCurrentWorkingDirectory().getChildFile(argv[4]);
    // One-at-a-time calibration matrix. Case 0 locks the accepted hard-tune
    // baseline; every other case varies exactly one Artist/Vibe control.
    // naturalVibrato is the native -12..+12 control, not a UI percent.
    // Values such as 25/50/75/100 clamp, yielding duplicate bounces.
    // The optional Natural-fit retune is diagnostic-only. Natural residual
    // phase is deliberately inactive at hard-tune 0 ms, so a voiced exercise
    // must use a nonzero Retune setting to identify that surface.
    auto naturalFitRetuneMs = 0.0f;
    if (const auto* text = std::getenv ("STUDIOFORGE_NATURAL_FIT_RETUNE_MS"))
        naturalFitRetuneMs = juce::jlimit (0.0f, 50.0f, std::strtof (text, nullptr));
    request.cases = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    for (const auto value : { 10.0f, 25.0f, 50.0f, 100.0f })
        request.cases.push_back ({ value, 0.0f, 0.0f, 0.0f });
    for (const auto value : { 25.0f, 50.0f, 75.0f, 100.0f })
        request.cases.push_back ({ 0.0f, value, 0.0f, 0.0f });
    for (const auto value : { 25.0f, 50.0f, 75.0f, 100.0f })
        request.cases.push_back ({ 0.0f, 0.0f, value, 0.0f });
    for (const auto value : { -12.0f, -9.0f, -6.0f, 3.0f, 6.0f, 9.0f, 12.0f })
        request.cases.push_back ({ naturalFitRetuneMs, 0.0f, 0.0f, value });

    // Corrected legacy mixed controls: Natural Vibrato remains native.
    request.cases.push_back ({ 10.0f, 25.0f, 25.0f, 0.0f });
    request.cases.push_back ({ 25.0f, 50.0f, 50.0f, 3.0f });
    request.cases.push_back ({ 50.0f, 75.0f, 75.0f, 6.0f });
    request.cases.push_back ({ 100.0f, 100.0f, 100.0f, 12.0f });
    juce::String report;
    if (const auto result = StudioForgeCalibrationRunner::run(request, report); result.failed())
    {
        std::cerr << result.getErrorMessage() << '\n';
        return 1;
    }
    const auto reportFile = request.outputDirectory.getChildFile("calibration-report.csv");
    if (! reportFile.replaceWithText(report))
    {
        std::cerr << "Cannot write " << reportFile.getFullPathName() << '\n';
        return 1;
    }
    std::cout << report;
    return 0;
}
