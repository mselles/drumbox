cmake_minimum_required(VERSION 3.22)

project(DrumBox VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build a universal binary on Mac (Apple Silicon + Intel)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# JUCE: use a local checkout if you pass -DJUCE_DIR=/path/to/JUCE,
# otherwise download it automatically the first time you configure.
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(DrumBox
    COMPANY_NAME "Miles Selles"
    PLUGIN_MANUFACTURER_CODE Mslz
    PLUGIN_CODE Drbx
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "DrumBox"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(DrumBox PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(DrumBox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DrumBox
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline render test: plays a pattern through the engine and writes WAVs,
# so you can hear changes without opening a DAW.
juce_add_console_app(RenderTest PRODUCT_NAME "RenderTest")
target_sources(RenderTest PRIVATE Tests/RenderTest.cpp)
target_include_directories(RenderTest PRIVATE Source)
target_compile_definitions(RenderTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(RenderTest PRIVATE juce::juce_audio_formats juce::juce_core
    PUBLIC juce::juce_recommended_config_flags)
