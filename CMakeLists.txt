cmake_minimum_required(VERSION 3.24)
project(MixAgent VERSION 1.0.0)
include(CTest)
option(MIXAGENT_COPY_PLUGIN_AFTER_BUILD "Copy VST3 into the system plugin directory" ON)

include(FetchContent)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

add_compile_definitions(JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

set(AGM_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

juce_add_plugin(MixAgent
    PRODUCT_NAME "MixAgent"
    COMPANY_NAME "AgentMix"
    PLUGIN_MANUFACTURER_CODE Agmx
    PLUGIN_CODE "AgMx"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD ${MIXAGENT_COPY_PLUGIN_AFTER_BUILD}
    FORMATS VST3 Standalone
    VERSION 1.0.0)

target_sources(MixAgent PRIVATE ${AGM_SOURCES})
target_compile_features(MixAgent PRIVATE cxx_std_17)
juce_generate_juce_header(MixAgent)
target_link_libraries(MixAgent PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_devices)

add_test(NAME MixAgentEditor COMMAND $<TARGET_FILE:EditorProbe> ${CMAKE_CURRENT_BINARY_DIR}/mixagent-ui.png)

add_test(NAME MixAgentSmoke COMMAND $<TARGET_FILE:MixAgentSmokeTest>)

juce_add_console_app(MixAgentDspRegression)
target_sources(MixAgentDspRegression PRIVATE Tests/DspRegression.cpp)
target_compile_features(MixAgentDspRegression PRIVATE cxx_std_17)
target_link_libraries(MixAgentDspRegression PRIVATE juce::juce_dsp)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(MixAgentDspRegression PRIVATE AGM_TEST_ALLOCATIONS=1)
    target_link_options(MixAgentDspRegression PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc)
endif()
add_test(NAME MixAgentDspRegression COMMAND $<TARGET_FILE:MixAgentDspRegression>)

# This target hosts the compiled wrapper; it deliberately does not link AGM_SOURCES.
juce_add_console_app(MixAgentVst3HostSmoke)
target_sources(MixAgentVst3HostSmoke PRIVATE Tests/Vst3HostSmoke.cpp)
target_compile_features(MixAgentVst3HostSmoke PRIVATE cxx_std_17)
target_compile_definitions(MixAgentVst3HostSmoke PRIVATE JUCE_PLUGINHOST_VST3=1)
target_link_libraries(MixAgentVst3HostSmoke PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics)
add_dependencies(MixAgentVst3HostSmoke MixAgent_VST3)
get_target_property(MIXAGENT_VST3_BUNDLE MixAgent_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
add_test(NAME MixAgentVst3HostSmoke
    COMMAND $<TARGET_FILE:MixAgentVst3HostSmoke> "${MIXAGENT_VST3_BUNDLE}")
set_tests_properties(MixAgentVst3HostSmoke PROPERTIES TIMEOUT 90)

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

juce_add_console_app(EditorProbe)
juce_generate_juce_header(EditorProbe)
target_sources(EditorProbe PRIVATE
    Tests/EditorProbe.cpp
    ${AGM_SOURCES})
target_compile_features(EditorProbe PRIVATE cxx_std_17)
target_include_directories(EditorProbe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(EditorProbe PRIVATE
    juce::juce_dsp)
target_link_libraries(EditorProbe PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_devices)
