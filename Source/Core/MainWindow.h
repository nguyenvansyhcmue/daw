#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>

#include "UI/MainComponent.h"

class MainWindow final : public juce::DocumentWindow, private juce::OpenGLRenderer
{
public:
    explicit MainWindow(const juce::String& name);
    ~MainWindow() override;

    void closeButtonPressed() override;

    void renderOpenGL() override;
    void newOpenGLContextCreated() override {}
    void openGLContextClosing() override {}

private:
    std::unique_ptr<juce::OpenGLContext> openGLContext;
    MainComponent* mainComponent = nullptr;
};
