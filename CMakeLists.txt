cmake_minimum_required(VERSION 3.24)
project(MixAgent VERSION 1.0.0)

include(FetchContent)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

add_compile_definitions(JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

# Owner's Windows workflow copies the built plugin into the system VST3 folder.
# Headless / CI builds turn this off: -DMIXAGENT_COPY_PLUGIN=OFF
option(MIXAGENT_COPY_PLUGIN "Copy the plugin into the system plugin folder after build" ON)
# Address/UB sanitizers on the headless test targets: -DMIXAGENT_SANITIZE=ON
option(MIXAGENT_SANITIZE "Build the console test apps with -fsanitize=address,undefined" OFF)

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
    COPY_PLUGIN_AFTER_BUILD ${MIXAGENT_COPY_PLUGIN}
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

enable_testing()

juce_add_console_app(MixAgentSmokeTest)
juce_generate_juce_header(MixAgentSmokeTest)
target_sources(MixAgentSmokeTest PRIVATE
    Tests/SmokeTest.cpp
    ${AGM_SOURCES})
target_compile_features(MixAgentSmokeTest PRIVATE cxx_std_17)
target_compile_definitions(MixAgentSmokeTest PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)   # runDispatchLoopUntil in tests
target_link_libraries(MixAgentSmokeTest PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_devices)

juce_add_console_app(MixAgentAuditTest)
juce_generate_juce_header(MixAgentAuditTest)
target_sources(MixAgentAuditTest PRIVATE
    Tests/AuditTest.cpp
    ${AGM_SOURCES})
target_compile_features(MixAgentAuditTest PRIVATE cxx_std_17)
target_compile_definitions(MixAgentAuditTest PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(MixAgentAuditTest PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_devices)

juce_add_console_app(MixAgentCharacterTest)
juce_generate_juce_header(MixAgentCharacterTest)
target_sources(MixAgentCharacterTest PRIVATE Tests/CharacterTest.cpp)
target_compile_features(MixAgentCharacterTest PRIVATE cxx_std_17)
target_link_libraries(MixAgentCharacterTest PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_dsp)

juce_add_console_app(EditorProbe)
juce_generate_juce_header(EditorProbe)
target_sources(EditorProbe PRIVATE
    Tests/EditorProbe.cpp
    ${AGM_SOURCES})
target_compile_features(EditorProbe PRIVATE cxx_std_17)
target_compile_definitions(EditorProbe PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
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

if(MIXAGENT_SANITIZE AND NOT MSVC)
    foreach(t MixAgentSmokeTest MixAgentAuditTest MixAgentCharacterTest EditorProbe)
        target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${t} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

add_test(NAME MixAgentSmokeTest COMMAND MixAgentSmokeTest)
add_test(NAME MixAgentAuditTest COMMAND MixAgentAuditTest)
add_test(NAME MixAgentCharacterTest COMMAND MixAgentCharacterTest)

# The editor probe needs a display; on Linux run it under xvfb-run when available.
find_program(MIXAGENT_XVFB_RUN xvfb-run)
if(MIXAGENT_XVFB_RUN)
    add_test(NAME EditorProbe COMMAND ${MIXAGENT_XVFB_RUN} -a $<TARGET_FILE:EditorProbe>)
else()
    add_test(NAME EditorProbe COMMAND EditorProbe)
endif()
