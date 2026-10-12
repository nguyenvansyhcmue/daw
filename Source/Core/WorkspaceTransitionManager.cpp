#include "WorkspaceTransitionManager.h"
#include "../AudioEngine/AudioEngine.h"

WorkspaceTransitionManager::WorkspaceTransitionManager(AudioEngine& engine, LayoutApplier layout)
    : audioEngine(engine), applyLayout(std::move(layout)), active(WorkspaceProfile::forMode(WorkspaceMode::studio)) {}

juce::Result WorkspaceTransitionManager::validate(const WorkspaceProfile& profile) const
{
    if (profile.mode == WorkspaceMode::studio) return juce::Result::ok();
    return audioEngine.validateWorkspaceProfile(profile);
}

juce::Result WorkspaceTransitionManager::request(WorkspaceMode mode)
{
    // Last intent wins. This also makes a future asynchronous device/scene
    // preparation safe without allowing an older request to commit afterward.
    pendingMode = mode;
    hasPendingRequest = true;
    if (transitioning) return juce::Result::ok();

    transitioning = true;
    juce::Result result = juce::Result::ok();
    while (hasPendingRequest)
    {
        const auto targetMode = pendingMode;
        hasPendingRequest = false;
        // Re-selecting the current mode is a deliberate "make this workspace
        // ready" action. It reapplies the hardware-aware monitor preset,
        // which is especially important on initial STUDIO startup.
        if (targetMode == active.mode)
        {
            const auto audioResult = audioEngine.applyWorkspaceProfile(active);
            if (audioResult.failed())
            {
                result = audioResult;
                lastError = audioResult.getErrorMessage();
                state = State::failed;
            }
            continue;
        }
        const auto profile = WorkspaceProfile::forMode(targetMode);
        state = State::validating;
        if (auto validation = validate(profile); validation.failed()) { result = validation; lastError = validation.getErrorMessage(); state = State::failed; continue; }
        state = State::preparing;
        state = State::applyingAudio;
        if (auto audioResult = audioEngine.applyWorkspaceProfile(profile); audioResult.failed()) { result = audioResult; lastError = audioResult.getErrorMessage(); state = State::failed; continue; }
        state = State::applyingLayout;
        applyLayout(profile);
        active = profile;
        state = State::idle;
        lastError.clear();
    }
    transitioning = false;
    return result;
}
