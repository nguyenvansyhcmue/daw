#pragma once

#include <cstdint>

// Workspace mode is intentionally separate from track monitoring. It selects
// a global presentation and audio policy; individual tracks still own their
// input routing and monitor state.
enum class WorkspaceMode : uint8_t { studio, stage, stream };
enum class WorkspaceModeTransition : uint8_t { idle, preparing, applyingAudio, applyingLayout, ready, failed };
