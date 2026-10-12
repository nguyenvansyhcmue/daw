#pragma once

#include <functional>
#include <juce_core/juce_core.h>
#include "WorkspaceProfile.h"

class AudioEngine;

class WorkspaceTransitionManager
{
public:
    enum class State : uint8_t { idle, validating, preparing, applyingAudio, applyingLayout, failed };
    using LayoutApplier = std::function<void(const WorkspaceProfile&)>;

    WorkspaceTransitionManager(AudioEngine& engine, LayoutApplier applyLayout);
    juce::Result request(WorkspaceMode mode);
    WorkspaceMode getActiveMode() const noexcept { return active.mode; }
    State getState() const noexcept { return state; }
    const juce::String& getLastError() const noexcept { return lastError; }

private:
    juce::Result validate(const WorkspaceProfile& profile) const;
    AudioEngine& audioEngine;
    LayoutApplier applyLayout;
    WorkspaceProfile active;
    State state = State::idle;
    bool transitioning = false;
    bool hasPendingRequest = false;
    WorkspaceMode pendingMode = WorkspaceMode::studio;
    juce::String lastError;
};
