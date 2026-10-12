cmake_minimum_required(VERSION 3.22)

project(StudioForgeDAW VERSION 1.0.0 LANGUAGES C CXX)
option(STUDIOFORGE_PERF_BENCHMARK "Enable deterministic host audio benchmark" OFF)
set(STUDIOFORGE_ASIO_SDK_DIR "" CACHE PATH "Path to a licensed Steinberg ASIO SDK root")

find_file(STUDIOFORGE_ASIO_HEADER
    NAMES iasiodrv.h
    PATHS "${STUDIOFORGE_ASIO_SDK_DIR}"
    PATH_SUFFIXES common
)

set(STUDIOFORGE_ENABLE_ASIO 0)
if (STUDIOFORGE_ASIO_HEADER)
    get_filename_component(STUDIOFORGE_ASIO_INCLUDE_DIR
        "${STUDIOFORGE_ASIO_HEADER}" DIRECTORY
    )
    set(STUDIOFORGE_ENABLE_ASIO 1)
    message(STATUS "StudioForge: ASIO enabled using ${STUDIOFORGE_ASIO_INCLUDE_DIR}")
else()
    message(STATUS "StudioForge: ASIO disabled (set STUDIOFORGE_ASIO_SDK_DIR to enable it)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.7
)

FetchContent_MakeAvailable(JUCE)

# Vibe's production renderer is header-only but its CMake targets carry the
# platform math dependency (Accelerate on macOS). Share those exact pinned
# sources with the native StudioForge adapter.
if (NOT TARGET signalsmith-linear)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/autotune/.third_party/signalsmith_linear-src"
                     "${CMAKE_CURRENT_BINARY_DIR}/signalsmith-linear")
endif()
if (NOT TARGET signalsmith-stretch)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/autotune/.third_party/signalsmith_stretch-src"
                     "${CMAKE_CURRENT_BINARY_DIR}/signalsmith-stretch")
endif()

# VST3 discovery runs in this disposable helper. A third-party plug-in can
# crash during inspection; that must never take down the DAW process.
add_executable(StudioForgeVst3Probe Tools/StudioForgeVst3Probe.cpp)
target_link_libraries(StudioForgeVst3Probe PRIVATE
    juce::juce_audio_processors
    juce::juce_core
    juce::juce_events
)
target_compile_definitions(StudioForgeVst3Probe PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
)

juce_add_gui_app(StudioForgeDAW
    COMPANY_NAME "StudioForge"
    PRODUCT_NAME "StudioForge DAW"
    VERSION "1.0.0"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "StudioForge needs microphone access to record and process your audio interface inputs."
    ICON_BIG Assets/LiveDawAppIcon.png
    ICON_SMALL Assets/LiveDawAppIcon.png
)

juce_add_binary_data(StudioForgeBrandAssets
    SOURCES
        Assets/LiveDawLogo.png
        Assets/ControlBar/countin-metronome-console-v2.png
        Assets/Knobs/analog-pan-knob-v2.png
        Assets/MixerSkin/button_neutral.svg
        Assets/MixerSkin/channel_faceplate.svg
        Assets/MixerSkin/empty_insert_slot.svg
        Assets/MixerSkin/empty_nameplate.svg
        Assets/MixerSkin/fader_cap_silver.svg
        Assets/MixerSkin/fader_rail.svg
        Assets/MixerSkin/meter_off.svg
        Assets/MixerSkin/rotary_knob_neutral.svg
        Assets/MixerSkin/analog_button_neutral.svg
        Assets/MixerSkin/analog_channel_label_well.svg
        Assets/MixerSkin/analog_fader_cap.svg
        Assets/MixerSkin/analog_insert_slot_empty.svg
        Assets/MixerSkin/analog_knob_pan.svg
        Assets/MixerSkin/analog_send_knob_generated.png
        Assets/MixerSkin/analog_meter_well.svg
        Assets/MixerSkin/analog_panel_texture.svg
        Assets/MixerSkin/analog_screw.svg
)

# This is the same Vibe UI kit used by the shipped VST3, with a distinct
# BinaryData namespace so it can coexist with StudioForge's own skin assets.
juce_add_binary_data(StudioForgeVibeAssets
    NAMESPACE VibeAutotuneBinaryData
    HEADER_NAME VibeAutotuneBinaryData.h
    SOURCES
        autotune/VibeAutotune/Assets/ui_02_black_glass_frame_exact.png
        autotune/VibeAutotune/Assets/ui_knob_face_neutral_v1-square.png
        autotune/VibeAutotune/Assets/ui_knob_assembly_neutral_v2.png
        autotune/VibeAutotune/Assets/ui_champagne_brushed_neutral_v1.png
)

add_library(StudioForgeNativeVibe STATIC
    autotune/VibeAutotune/AnalogHardwareButton.cpp
    autotune/VibeAutotune/AnalogRotaryControl.cpp
    autotune/VibeAutotune/PluginEditor.cpp
    autotune/VibeAutotune/PluginProcessor.cpp
    Source/Plugins/NativeVibeAutotuneProcessor.cpp
)
target_include_directories(StudioForgeNativeVibe PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/autotune/VibeAutotune"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
)
target_compile_definitions(StudioForgeNativeVibe PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    $<$<CONFIG:Debug>:VIBE_UI_REFERENCE_IMAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/autotune/VibeAutotune/Assets/Reference/LIVE_TUNE_TARGET_MASTER.png">
)
target_link_libraries(StudioForgeNativeVibe PUBLIC
    StudioForgeVibeAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    signalsmith-stretch
)

# Keep the macOS bundle executable name aligned with JUCE's generated
# CFBundleExecutable entry.  A different Debug filename leaves macOS unable to
# launch the bundle because Info.plist still points at the product executable.
set_target_properties(StudioForgeDAW PROPERTIES
    DEBUG_OUTPUT_NAME "StudioForge DAW"
)

target_sources(StudioForgeDAW PRIVATE
    Source/Core/Main.cpp
    Source/Core/MainWindow.cpp
    Source/Core/WorkspaceTransitionManager.cpp
    Source/Core/Automation/AutomationLane.cpp
    Source/AudioEngine/AudioEngine.cpp
    Source/AudioEngine/AudioDiagnosticTracer.cpp
    Source/AudioEngine/CoreAudioInputProbe.cpp
    Source/AudioEngine/LiveKeyDetector.cpp
    Source/AudioEngine/AudioGraph.cpp
    Source/Models/TrackDataModel.cpp
    Source/Project/ProjectSerializer.cpp
    Source/Project/ProjectTemplates.cpp
    Source/Project/ProjectTemplateStore.cpp
    Source/Project/ProjectAlternativeStore.cpp
    Source/Project/RecentProjectsStore.cpp
    Source/Project/AutosaveService.cpp
    Source/Media/MediaReloadService.cpp
    Source/Media/AudioMediaPool.cpp
    Source/Media/WaveformThumbnailCache.cpp
    Source/Media/Waveform/WaveformCacheStore.cpp
    Source/Recording/RecordingSession.cpp
    Source/Control/ControlRoutingProfile.cpp
    Source/Control/StudioForgeControlBridge.cpp
    Source/Control/VibeAutotuneAdapter.cpp
    Source/Control/PitchCorrectionIntegrationService.cpp
    Source/Services/TuneAiAnalysisService.cpp
    Source/Midi/MidiCore.cpp
    Source/Midi/MidiFileImporter.cpp
    Source/Midi/MidiTransposeProcessor.cpp
    Source/Plugins/PluginHostService.cpp
    Source/UI/MainComponent.cpp
    Source/UI/DeviceSettingsPanel.cpp
    Source/UI/ProjectSettingsPanel.cpp
    Source/UI/PluginBrowserPanel.cpp
    Source/UI/Theme/StudioForgeLookAndFeel.cpp
    Source/UI/TransportLCD.cpp
    Source/UI/ControlBar.cpp
    Source/UI/ArrangeWindow.cpp
    Source/UI/TimelineNavigation/TimelineViewportController.cpp
    Source/UI/TimelineNavigation/TimelineZoomPolicy.cpp
    Source/UI/Waveform/WaveformRenderer.cpp
    Source/UI/MixerPane.cpp
    Source/UI/PianoRoll.cpp
    Source/UI/Inspector/InspectorViewState.cpp
    Source/UI/InspectorPane.cpp
    Source/UI/InspectorComponents.cpp
    Source/UI/AudioPeakMeter.cpp
    Source/UI/AudioDeviceErrorLocalizer.cpp
    Source/UI/StudioForgeDialog.cpp
    Source/UI/AssetBrowserPane.cpp
    Source/UI/PerformanceRoomIconLibrary.cpp
    Source/UI/StartupWorkflowPane.cpp
)

target_include_directories(StudioForgeDAW PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

if (STUDIOFORGE_ENABLE_ASIO)
    target_include_directories(StudioForgeDAW PRIVATE ${STUDIOFORGE_ASIO_INCLUDE_DIR})
endif()

target_link_libraries(StudioForgeDAW PRIVATE
    StudioForgeBrandAssets
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_opengl
)

target_compile_definitions(StudioForgeDAW PRIVATE
    STUDIOFORGE_TUNE_AI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tune-AI"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PROJUCER_INCLUDE_JUCE_HEADER=0
    JUCE_PLUGINHOST_VST3=1
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>
    JUCE_ASIO=${STUDIOFORGE_ENABLE_ASIO}
    STUDIOFORGE_VST3_PROBE_PATH="$<TARGET_FILE:StudioForgeVst3Probe>"
)
add_dependencies(StudioForgeDAW StudioForgeVst3Probe)

# A workspace build of VibeAutotune can be hosted directly by StudioForge
# before an AU is installed or registered with macOS. This definition is only
# emitted when the explicit development bundle already exists.
set(STUDIOFORGE_VIBE_AUTOTUNE_VST3
    "${CMAKE_CURRENT_SOURCE_DIR}/autotune/build-studioforge-host/VibeAutotune_artefacts/Release/VST3/VibeAutotune.vst3")
if (EXISTS "${STUDIOFORGE_VIBE_AUTOTUNE_VST3}")
    target_compile_definitions(StudioForgeDAW PRIVATE
        STUDIOFORGE_VIBE_AUTOTUNE_VST3_PATH="${STUDIOFORGE_VIBE_AUTOTUNE_VST3}"
    )
    # The VST3 is built in a separate CMake tree. Keep the embedded copy fresh
    # even when StudioForge itself has no C++ sources to relink.
    add_custom_target(StudioForgeBundledVibeAutotune ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:StudioForgeDAW>/Contents/Resources/PlugIns/VST3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${STUDIOFORGE_VIBE_AUTOTUNE_VST3}"
            "$<TARGET_BUNDLE_DIR:StudioForgeDAW>/Contents/Resources/PlugIns/VST3/VibeAutotune.vst3"
        DEPENDS "${STUDIOFORGE_VIBE_AUTOTUNE_VST3}/Contents/MacOS/VibeAutotune"
        COMMENT "Refreshing embedded VibeAutotune VST3"
    )
    add_dependencies(StudioForgeDAW StudioForgeBundledVibeAutotune)
    add_custom_command(TARGET StudioForgeDAW POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:StudioForgeDAW>/Contents/Resources/PlugIns/VST3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${STUDIOFORGE_VIBE_AUTOTUNE_VST3}"
            "$<TARGET_BUNDLE_DIR:StudioForgeDAW>/Contents/Resources/PlugIns/VST3/VibeAutotune.vst3"
        COMMENT "Embedding VibeAutotune VST3 in StudioForge app bundle"
    )
endif()

if (MSVC)
    target_compile_options(StudioForgeDAW PRIVATE
        /Oi
        /FS
        /utf-8
        $<$<CONFIG:Release>:/O2>
    )
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
    target_compile_options(StudioForgeDAW PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()

if (APPLE)
    target_compile_definitions(StudioForgeDAW PRIVATE
        JUCE_MAC=1
    )
endif()

include(CTest)

if (BUILD_TESTING)
    add_executable(StudioForgeNativeVibeSmokeTest
        Tests/NativeVibeAutotuneSmokeTest.cpp
    )
    target_include_directories(StudioForgeNativeVibeSmokeTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
    )
    target_link_libraries(StudioForgeNativeVibeSmokeTest PRIVATE
        StudioForgeNativeVibe
    )
    target_compile_definitions(StudioForgeNativeVibeSmokeTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    add_test(NAME StudioForgeNativeVibeSmokeTest COMMAND StudioForgeNativeVibeSmokeTest)

    add_executable(StudioForgeLiveTuneConfidenceGateTest
        Tests/LiveTuneConfidenceGateTest.cpp
    )
    target_include_directories(StudioForgeLiveTuneConfidenceGateTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
    )
    target_compile_definitions(StudioForgeLiveTuneConfidenceGateTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    add_test(NAME StudioForgeLiveTuneConfidenceGateTest COMMAND StudioForgeLiveTuneConfidenceGateTest)

    add_executable(StudioForgeLivePerformanceTests
        Tests/LivePerformanceAssessmentTests.cpp
    )
    target_include_directories(StudioForgeLivePerformanceTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
    )
    add_test(NAME StudioForgeLivePerformanceTests COMMAND StudioForgeLivePerformanceTests)

    juce_add_plugin(StudioForgeVst3TestEffect
        COMPANY_NAME "StudioForge"
        PLUGIN_MANUFACTURER_CODE StFg
        PLUGIN_CODE Tsfx
        FORMATS VST3
        PRODUCT_NAME "StudioForge VST3 Test Effect v2"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES Fx
    )

    target_sources(StudioForgeVst3TestEffect PRIVATE Tests/VST3TestEffect.cpp)
    target_link_libraries(StudioForgeVst3TestEffect PRIVATE juce::juce_audio_utils)
    target_compile_definitions(StudioForgeVst3TestEffect PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    )
    if (MSVC)
        target_compile_options(StudioForgeVst3TestEffect PRIVATE /FS)
        target_link_options(StudioForgeVst3TestEffect_VST3 PRIVATE /INCREMENTAL:NO)
    endif()

    add_executable(StudioForgeCoreTests
        Tests/CorePlaybackMixerTests.cpp
        Tests/AutomationDomainTests.cpp
        Source/Core/Automation/AutomationLane.cpp
        Source/AudioEngine/AudioEngine.cpp
        Source/AudioEngine/AudioDiagnosticTracer.cpp
        Source/AudioEngine/CoreAudioInputProbe.cpp
        Source/AudioEngine/LiveKeyDetector.cpp
        Source/AudioEngine/AudioGraph.cpp
        Source/Models/TrackDataModel.cpp
        Source/Project/ProjectSerializer.cpp
        Source/Project/ProjectTemplates.cpp
        Source/Project/ProjectTemplateStore.cpp
        Source/Project/ProjectAlternativeStore.cpp
        Source/Project/RecentProjectsStore.cpp
        Source/Project/AutosaveService.cpp
        Source/Media/MediaReloadService.cpp
        Source/Media/AudioMediaPool.cpp
        Source/Media/WaveformThumbnailCache.cpp
        Source/Media/Waveform/WaveformCacheStore.cpp
        Source/Recording/RecordingSession.cpp
        Source/Control/ControlRoutingProfile.cpp
        Source/Control/StudioForgeControlBridge.cpp
        Source/Control/VibeAutotuneAdapter.cpp
        Source/Control/PitchCorrectionIntegrationService.cpp
        Source/Midi/MidiCore.cpp
        Source/Midi/MidiFileImporter.cpp
        Source/Midi/MidiTransposeProcessor.cpp
        Source/Plugins/PluginHostService.cpp
        Source/UI/Inspector/InspectorViewState.cpp
        Source/UI/TimelineNavigation/TimelineViewportController.cpp
        Source/UI/TimelineNavigation/TimelineZoomPolicy.cpp
    )

    target_include_directories(StudioForgeCoreTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
    )

    target_link_libraries(StudioForgeCoreTests PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_core
        juce::juce_events
    )

    target_compile_definitions(StudioForgeCoreTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        STUDIOFORGE_TEST_VST3_PATH="$<TARGET_FILE:StudioForgeVst3TestEffect_VST3>"
        STUDIOFORGE_VST3_PROBE_PATH="$<TARGET_FILE:StudioForgeVst3Probe>"
        STUDIOFORGE_DISABLE_AUTOMATIC_PLUGIN_SCAN=1
        $<$<BOOL:${STUDIOFORGE_PERF_BENCHMARK}>:STUDIOFORGE_PERF_BENCHMARK=1>
    )

    if (MSVC)
        target_compile_options(StudioForgeCoreTests PRIVATE /FS)
    endif()

    add_dependencies(StudioForgeCoreTests StudioForgeVst3TestEffect_VST3 StudioForgeVst3Probe)

    add_test(NAME StudioForgeCoreTests COMMAND StudioForgeCoreTests)
endif()

# Headless calibration entry point. It deliberately links the same model,
# plug-in host and offline renderer as the app, so automation tests do not
# drift into a second audio-processing path.
add_executable(StudioForgeCalibrationRunner
        Tools/StudioForgeCalibrationRunner.cpp
        Source/Services/StudioForgeCalibrationRunner.cpp
        Source/Core/Automation/AutomationLane.cpp
        Source/AudioEngine/AudioEngine.cpp
        Source/AudioEngine/AudioDiagnosticTracer.cpp
        Source/AudioEngine/CoreAudioInputProbe.cpp
        Source/AudioEngine/LiveKeyDetector.cpp
        Source/AudioEngine/AudioGraph.cpp
        Source/Models/TrackDataModel.cpp
        Source/Project/ProjectSerializer.cpp
        Source/Project/ProjectTemplates.cpp
        Source/Project/ProjectTemplateStore.cpp
        Source/Project/ProjectAlternativeStore.cpp
        Source/Project/RecentProjectsStore.cpp
        Source/Project/AutosaveService.cpp
        Source/Media/MediaReloadService.cpp
        Source/Media/AudioMediaPool.cpp
        Source/Media/WaveformThumbnailCache.cpp
        Source/Media/Waveform/WaveformCacheStore.cpp
        Source/Recording/RecordingSession.cpp
        Source/Control/ControlRoutingProfile.cpp
        Source/Control/StudioForgeControlBridge.cpp
        Source/Control/VibeAutotuneAdapter.cpp
        Source/Control/PitchCorrectionIntegrationService.cpp
        Source/Midi/MidiCore.cpp
        Source/Midi/MidiFileImporter.cpp
        Source/Midi/MidiTransposeProcessor.cpp
        Source/Plugins/PluginHostService.cpp
)
target_include_directories(StudioForgeCalibrationRunner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(StudioForgeCalibrationRunner PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_core
        juce::juce_events
        juce::juce_gui_basics
)
if (APPLE)
    target_link_libraries(StudioForgeCalibrationRunner PRIVATE
        "-framework CoreAudioKit"
    )
endif()

target_compile_definitions(StudioForgeCalibrationRunner PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>
        STUDIOFORGE_VST3_PROBE_PATH="$<TARGET_FILE:StudioForgeVst3Probe>"
        STUDIOFORGE_DISABLE_AUTOMATIC_PLUGIN_SCAN=1
)
add_dependencies(StudioForgeCalibrationRunner StudioForgeVst3Probe)
