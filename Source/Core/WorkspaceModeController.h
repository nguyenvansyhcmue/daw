#pragma once
#include "WorkspaceTransitionManager.h"

class WorkspaceModeController
{
public:
    WorkspaceModeController(AudioEngine& engine, WorkspaceTransitionManager::LayoutApplier layout)
        : transitions(engine, std::move(layout)) {}
    juce::Result requestMode(WorkspaceMode mode) { return transitions.request(mode); }
    WorkspaceMode getActiveMode() const noexcept { return transitions.getActiveMode(); }
    WorkspaceTransitionManager::State getTransitionState() const noexcept { return transitions.getState(); }
private:
    WorkspaceTransitionManager transitions;
};
