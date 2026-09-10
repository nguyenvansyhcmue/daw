#include "MainWindow.h"

MainWindow::MainWindow(const juce::String& name)
    : juce::DocumentWindow(name,
          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
          juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(1200, 720, 3000, 1800);
    setContentOwned(new MainComponent(), true);

    mainComponent = dynamic_cast<MainComponent*>(getContentComponent());
    centreWithSize(1600, 920);
    setVisible(true);

    openGLContext = std::make_unique<juce::OpenGLContext>();
    openGLContext->setRenderer(this);
    openGLContext->attachTo(*this);
}

MainWindow::~MainWindow()
{
    if (openGLContext != nullptr)
    {
        openGLContext->detach();
    }
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainWindow::renderOpenGL()
{
    juce::OpenGLHelpers::clear(juce::Colour(0xff1a1a1a));
}
