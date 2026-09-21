cmake_minimum_required(VERSION 3.22)

project(StudioForgeDAW VERSION 1.0.0 LANGUAGES CXX)
option(STUDIOFORGE_PERF_BENCHMARK "Enable deterministic host audio benchmark" OFF)

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

juce_add_gui_app(StudioForgeDAW
    COMPANY_NAME "StudioForge"
    PRODUCT_NAME "StudioForge DAW"
    VERSION "1.0.0"
)

# Keep the development binary distinct from the distributable.  On Windows this
# also prevents an antivirus scan of a just-run Debug executable from blocking
# the next incremental link of the Release-named product.
set_target_properties(StudioForgeDAW PROPERTIES
    DEBUG_OUTPUT_NAME "StudioForge DAW Debug"
)

target_sources(StudioForgeDAW PRIVATE
    Source/Core/Main.cpp
    Source/Core/MainWindow.cpp
    Source/Core/Automation/AutomationLane.cpp
    Source/AudioEngine/AudioEngine.cpp
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
    Source/Recording/RecordingSession.cpp
    Source/Midi/MidiCore.cpp
    Source/Midi/MidiFileImporter.cpp
    Source/Midi/MidiTransposeProcessor.cpp
    Source/Plugins/PluginHostService.cpp
    Source/UI/MainComponent.cpp
    Source/UI/DeviceSettingsPanel.cpp
    Source/UI/ProjectSettingsPanel.cpp
    Source/UI/PluginBrowserPanel.cpp
    Source/UI/Theme/StudioForgeLookAndFeel.cpp
    Source/UI/ControlBar.cpp
    Source/UI/ArrangeWindow.cpp
    Source/UI/MixerPane.cpp
    Source/UI/PianoRoll.cpp
    Source/UI/Inspector/InspectorViewState.cpp
    Source/UI/InspectorPane.cpp
    Source/UI/InspectorComponents.cpp
    Source/UI/AudioPeakMeter.cpp
    Source/UI/AssetBrowserPane.cpp
    Source/UI/PerformanceFooter.cpp
    Source/UI/StartupWorkflowPane.cpp
    Source/UI/VocalistRackComponent.cpp
)

target_include_directories(StudioForgeDAW PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_link_libraries(StudioForgeDAW PRIVATE
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
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PROJUCER_INCLUDE_JUCE_HEADER=0
    JUCE_PLUGINHOST_VST3=1
)

if (MSVC)
    target_compile_options(StudioForgeDAW PRIVATE
        /Oi
        /FS
        $<$<CONFIG:Release>:/O2>
    )
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(StudioForgeDAW PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
    )
endif()

if (APPLE)
    target_compile_definitions(StudioForgeDAW PRIVATE
        JUCE_MAC=1
    )
endif()

include(CTest)

if (BUILD_TESTING)
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
        Source/Recording/RecordingSession.cpp
        Source/Midi/MidiCore.cpp
        Source/Midi/MidiFileImporter.cpp
        Source/Midi/MidiTransposeProcessor.cpp
        Source/Plugins/PluginHostService.cpp
        Source/UI/Inspector/InspectorViewState.cpp
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
        $<$<BOOL:${STUDIOFORGE_PERF_BENCHMARK}>:STUDIOFORGE_PERF_BENCHMARK=1>
    )

    if (MSVC)
        target_compile_options(StudioForgeCoreTests PRIVATE /FS)
    endif()

    add_dependencies(StudioForgeCoreTests StudioForgeVst3TestEffect_VST3)

    add_test(NAME StudioForgeCoreTests COMMAND StudioForgeCoreTests)
endif()
