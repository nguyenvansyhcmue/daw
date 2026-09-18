#include "AutomationDomainTests.h"

#include "Core/Automation/AutomationLane.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
bool expectAutomation(bool condition, const char* message)
{
    if (! condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

bool runAutomationDomainTests()
{
    AutomationLane volume(AutomationParameter::trackVolume, 1.0f);
    volume.upsertPoint(480.0, 0.5f);
    volume.upsertPoint(0.0, 1.0f);
    volume.upsertPoint(960.0, 1.5f);
    if (! expectAutomation(volume.getPoints().size() == 3 && volume.getPoints()[0].samplePosition == 0.0,
                            "automation points remain ordered")) return false;
    if (! expectAutomation(std::abs(volume.evaluate(720.0) - 1.0f) < 1.0e-5f,
                            "automation evaluates linearly between points")) return false;

    volume.upsertPoint(480.0, 0.75f);
    if (! expectAutomation(volume.getPoints().size() == 3 && std::abs(volume.getPoints()[1].value - 0.75f) < 1.0e-5f,
                            "automation replaces an existing point in place")) return false;
    if (! expectAutomation(volume.removePoint(480.0) && ! volume.removePoint(480.0),
                            "automation removes only existing points")) return false;

    bool rejectedInvalidValue = false;
    try { volume.upsertPoint(120.0, 2.1f); }
    catch (const std::invalid_argument&) { rejectedInvalidValue = true; }
    if (! expectAutomation(rejectedInvalidValue, "automation rejects invalid parameter values")) return false;

    AutomationLane pan(AutomationParameter::trackPan, 0.0f);
    pan.setMode(AutomationMode::touch);
    return expectAutomation(pan.getMode() == AutomationMode::touch && std::abs(pan.evaluate(0.0)) < 1.0e-5f,
                            "automation mode and default value remain independent");
}
