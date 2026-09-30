#include "TuneAiAnalysisService.h"

namespace
{
int rootFromName(juce::String name)
{
    name = name.trim().toUpperCase();
    static const std::pair<const char*, int> notes[] {
        { "C", 0 }, { "C#", 1 }, { "DB", 1 }, { "D", 2 }, { "D#", 3 }, { "EB", 3 },
        { "E", 4 }, { "F", 5 }, { "F#", 6 }, { "GB", 6 }, { "G", 7 }, { "G#", 8 },
        { "AB", 8 }, { "A", 9 }, { "A#", 10 }, { "BB", 10 }, { "B", 11 }
    };
    for (const auto& [label, root] : notes)
        if (name == label)
            return root;
    return -1;
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
        result.minor = object->getProperty("scale").toString().containsIgnoreCase("minor");
        result.rootNote = rootFromName(object->getProperty("key").toString());
        result.error = object->getProperty("error").toString();
        if (result.succeeded && result.rootNote >= 0)
            return result;
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

   #if defined(STUDIOFORGE_TUNE_AI_SOURCE_DIR)
    const auto fromBuildSource = juce::File(STUDIOFORGE_TUNE_AI_SOURCE_DIR).getChildFile("daw_bridge.py");
    if (fromBuildSource.existsAsFile()) return fromBuildSource;
   #endif

    const auto sourceRoot = juce::File(__FILE__).getParentDirectory().getParentDirectory().getParentDirectory();
    return sourceRoot.getChildFile("tune-AI/daw_bridge.py");
}

void TuneAiAnalysisService::analyseYouTubeUrlAsync(juce::String url, Completion completion)
{
    const auto script = findBridgeScript();
    if (! script.existsAsFile())
    {
        if (completion != nullptr)
            completion({ false, false, 0, false, 0.0f, {}, "Tune-AI bridge was not found beside this DAW source." });
        return;
    }
    workers.addJob(new TuneAiJob(script, std::move(url), std::move(completion)), true);
}
