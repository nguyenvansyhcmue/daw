#include <juce_gui_basics/juce_gui_basics.h>

#include "MainWindow.h"

class StudioForgeApplication final : public juce::JUCEApplication
{
public:
    StudioForgeApplication() = default;

    const juce::String getApplicationName() override { return "StudioForge DAW"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>("StudioForge DAW");
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {}

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(StudioForgeApplication)
