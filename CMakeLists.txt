cmake_minimum_required(VERSION 3.24)
project(MixAgent VERSION 1.0.0)

include(FetchContent)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

add_compile_definitions(JUCE_VST3_CAN_REPLACE_VST2=0)

set(AGM_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

juce_add_plugin(MixAgent
    PRODUCT_NAME "MixAgent"
    COMPANY_NAME "AgentMix"
    PLUGIN_MANUFACTURER_CODE Agmx
    PLUGIN_CODE "AgMx"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    FORMATS VST3 Standalone
    VERSION 1.0.0)

target_sources(MixAgent PRIVATE ${AGM_SOURCES})
juce_generate_juce_header(MixAgent)
target_link_libraries(MixAgent PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_devices)

juce_add_console_app(MixAgentSmokeTest)
juce_generate_juce_header(MixAgentSmokeTest)
target_sources(MixAgentSmokeTest PRIVATE
    Tests/SmokeTest.cpp
    ${AGM_SOURCES})
target_compile_features(MixAgentSmokeTest PRIVATE cxx_std_17)
target_link_libraries(MixAgentSmokeTest PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_devices)
