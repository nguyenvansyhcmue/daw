#include "TuneAiAnalysisService.h"

namespace
{
juce::File findPackagedTuneAiBundle()
{
   #if JUCE_MAC
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto hostBundle = executable.getParentDirectory().getParentDirectory().getParentDirectory();
    const auto sibling = hostBundle.getParentDirectory().getChildFile("Tune-AI.app");
    if (sibling.isDirectory())
        return sibling;
   #endif

   #if defined(STUDIOFORGE_TUNE_AI_SOURCE_DIR)
    // Development builds keep the distributable app beside the Tune-AI source.
    // Release builds use the sibling bundle above, so end users never need a
    // Python interpreter or virtual environment to launch Auto Key.
    const auto developmentBundle = juce::File { STUDIOFORGE_TUNE_AI_SOURCE_DIR }
        .getChildFile("dist")
        .getChildFile("THM Vocal Panel.app");
    if (developmentBundle.isDirectory())
        return developmentBundle;
   #endif

    return {};
}

juce::File packagedTuneAiRoot()
{
    return findPackagedTuneAiBundle().getChildFile("Contents/Resources/tune-AI");
}

TuneAiAnalysisService::Result parseResult(const juce::String& output, int exitCode)
{
    auto result = TuneAiAnalysisService::Result {};
    const auto jsonLine = output.fromLastOccurrenceOf("\n", false, false).trim();
    const auto parsed = juce::JSON::parse(jsonLine.isNotEmpty() ? jsonLine : output.trim());
    if (const auto* object = parsed.getDynamicObject(); object != nullptr)
    {
        result.succeeded = static_cast<bool>(object->getProperty("ok")) && exitCode == 0;
        result.fromCache = object->getProperty("status").toString() == "cached";
        result.title = object->getProperty("title").toString();
        result.confidence = static_cast<float>(object->getProperty("confidence"));
        const auto key = MusicalKey::parse(object->getProperty("key").toString(), object->getProperty("scale").toString());
        result.error = object->getProperty("error").toString();
        if (result.succeeded && key.has_value())
        {
            result.key = *key;
            return result;
        }
        if (result.error.isEmpty()) result.error = "Tune-AI returned an invalid key result.";
        result.succeeded = false;
        return result;
    }
    result.error = output.trim();
    if (result.error.isEmpty()) result.error = "Tune-AI did not return a result.";
    return result;
}

class TuneAiJob final : public juce::ThreadPoolJob
{
public:
    TuneAiJob(juce::File scriptIn, juce::String urlIn, TuneAiAnalysisService::Completion completionIn)
        : ThreadPoolJob("Tune-AI YouTube analysis"), script(std::move(scriptIn)), url(std::move(urlIn)), completion(std::move(completionIn)) {}

    JobStatus runJob() override
    {
        TuneAiAnalysisService::Result result;
        const auto folder = script.getParentDirectory();
       #if JUCE_WINDOWS
        const auto venvPython = folder.getChildFile(".venv").getChildFile("Scripts").getChildFile("python.exe");
       #else
        const auto venvPython = folder.getChildFile(".venv").getChildFile("bin").getChildFile("python3");
       #endif
        const auto interpreter = venvPython.existsAsFile() ? venvPython.getFullPathName()
                                                            : juce::String("python3");
        juce::ChildProcess process;
        if (! process.start({ interpreter, script.getFullPathName(), "--url", url }))
        {
            result.error = "Could not start Tune-AI Python. Install Tune-AI dependencies first.";
        }
        else if (! process.waitForProcessToFinish(180000))
        {
            process.kill();
            result.error = "Tune-AI analysis timed out after 3 minutes.";
        }
        else
        {
            result = parseResult(process.readAllProcessOutput(), process.getExitCode());
        }

        juce::MessageManager::callAsync([completion = std::move(completion), result]() mutable
        {
            if (completion != nullptr) completion(std::move(result));
        });
        return jobHasFinished;
    }

private:
    juce::File script;
    juce::String url;
    TuneAiAnalysisService::Completion completion;
};
}

TuneAiAnalysisService::~TuneAiAnalysisService()
{
    workers.removeAllJobs(true, 5000);
}

juce::File TuneAiAnalysisService::findBridgeScript() const
{
    const auto fromWorkingDirectory = juce::File::getCurrentWorkingDirectory().getChildFile("tune-AI/daw_bridge.py");
    if (fromWorkingDirectory.existsAsFile()) return fromWorkingDirectory;

    const auto fromPackagedApp = packagedTuneAiRoot().getChildFile("daw_bridge.py");
    if (fromPackagedApp.existsAsFile()) return fromPackagedApp;

   #if defined(STUDIOFORGE_TUNE_AI_SOURCE_DIR)
    const auto fromBuildSource = juce::File(STUDIOFORGE_TUNE_AI_SOURCE_DIR).getChildFile("daw_bridge.py");
    if (fromBuildSource.existsAsFile()) return fromBuildSource;
   #endif

    const auto sourceRoot = juce::File(__FILE__).getParentDirectory().getParentDirectory().getParentDirectory();
    return sourceRoot.getChildFile("tune-AI/daw_bridge.py");
}

juce::File TuneAiAnalysisService::findStandaloneAppScript() const
{
    const auto bridge = findBridgeScript();
    return bridge.existsAsFile() ? bridge.getParentDirectory().getChildFile("main.py") : juce::File {};
}

juce::Result TuneAiAnalysisService::launchStandaloneApp(juce::Rectangle<int> hostBounds) const
{
    const auto bundle = findPackagedTuneAiBundle();
    if (bundle.isDirectory())
    {
       #if JUCE_MAC
        if (! hostBounds.isEmpty())
        {
            juce::ChildProcess opener;
            const auto frame = juce::String(hostBounds.getX()) + "," + juce::String(hostBounds.getY()) + ","
                             + juce::String(hostBounds.getWidth()) + "," + juce::String(hostBounds.getHeight());
            if (! opener.start({ "/usr/bin/open", "-a", bundle.getFullPathName(), "--args",
                                 "--studioforge-hosted", "--host-frame", frame }))
                return juce::Result::fail("macOS could not start the packaged Tune-AI app.");
            return juce::Result::ok();
        }
       #endif
        return juce::Process::openDocument(bundle.getFullPathName(), {})
                   ? juce::Result::ok()
                   : juce::Result::fail("macOS could not open the packaged Tune-AI app.");
    }

    const auto app = findStandaloneAppScript();
    if (! app.existsAsFile())
        return juce::Result::fail("Tune-AI app was not found beside the StudioForge source.");

    const auto folder = app.getParentDirectory();
   #if JUCE_WINDOWS
    const auto venvPython = folder.getChildFile(".venv").getChildFile("Scripts").getChildFile("python.exe");
   #else
    const auto venvPython = folder.getChildFile(".venv").getChildFile("bin").getChildFile("python3");
   #endif
    const auto interpreter = venvPython.existsAsFile() ? venvPython.getFullPathName() : juce::String("python3");
    juce::ChildProcess process;
    auto arguments = juce::StringArray { interpreter, app.getFullPathName() };
    if (! hostBounds.isEmpty())
    {
        arguments.add("--studioforge-hosted");
        arguments.add("--host-frame");
        arguments.add(juce::String(hostBounds.getX()) + "," + juce::String(hostBounds.getY()) + ","
                      + juce::String(hostBounds.getWidth()) + "," + juce::String(hostBounds.getHeight()));
    }
    if (! process.start(arguments))
        return juce::Result::fail("Could not open Tune-AI. Install its Python dependencies first.");
    return juce::Result::ok();
}

void TuneAiAnalysisService::analyseYouTubeUrlAsync(juce::String url, Completion completion)
{
    const auto script = findBridgeScript();
    if (! script.existsAsFile())
    {
        if (completion != nullptr)
            completion({ false, false, {}, 0.0f, {}, "Tune-AI bridge was not found beside this DAW source." });
        return;
    }
    workers.addJob(new TuneAiJob(script, std::move(url), std::move(completion)), true);
}
