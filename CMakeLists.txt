cmake_minimum_required(VERSION 3.22)

project(StudioForgeDAW VERSION 1.0.0 LANGUAGES CXX)

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

target_sources(StudioForgeDAW PRIVATE
    Source/Core/Main.cpp
    Source/Core/MainWindow.cpp
    Source/AudioEngine/AudioEngine.cpp
    Source/AudioEngine/AudioGraph.cpp
    Source/Models/TrackDataModel.cpp
    Source/UI/MainComponent.cpp
    Source/UI/ControlBar.cpp
    Source/UI/ArrangeWindow.cpp
    Source/UI/MixerPane.cpp
    Source/UI/AudioPeakMeter.cpp
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
)

if (MSVC)
    target_compile_options(StudioForgeDAW PRIVATE
        /O2
        /Oi
    )
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(StudioForgeDAW PRIVATE
        -O3
        -march=native
    )
endif()

if (APPLE)
    target_compile_definitions(StudioForgeDAW PRIVATE
        JUCE_MAC=1
    )
endif()
