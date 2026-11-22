cmake_minimum_required(VERSION 3.22)

# macOS: universal binary by default (Apple Silicon + Intel). Must be set
# before project().
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(ModdMudd VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MODDMUDD_BUILD_TESTS "Build unit tests and hosting test plug-ins" ON)
option(MODDMUDD_COPY_AFTER_BUILD "Install the VST3 bundles into the user plug-in folder after building" OFF)
set(MODDMUDD_JUCE_DIR "" CACHE PATH "Use a local JUCE checkout instead of downloading JUCE ${MODDMUDD_JUCE_VERSION}")
set(MODDMUDD_JUCE_VERSION "9.0.6")

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "ModdMudd supports 64-bit builds only")
endif()

#==============================================================================
# JUCE (pinned)
if(MODDMUDD_JUCE_DIR)
    add_subdirectory(${MODDMUDD_JUCE_DIR} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${MODDMUDD_JUCE_VERSION}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

#==============================================================================
# Sources
set(MODDMUDD_ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine/Lfo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine/Randomizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine/EnvelopeFollower.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine/ModEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine/ModMatrix.cpp)

set(MODDMUDD_PLUGIN_SOURCES
    ${MODDMUDD_ENGINE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Host/PluginCatalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Host/HostedSlot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Plugin/Parameters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Plugin/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/MuddLookAndFeel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/Widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/ModulatorPanels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/MatrixPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/HostedView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/PluginEditor.cpp)

set(MODDMUDD_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_ARA=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

#==============================================================================
# Plug-in targets: VST3 only.
function(moddmudd_add_plugin target)
    cmake_parse_arguments(ARG "" "PRODUCT_NAME;PLUGIN_CODE;IS_SYNTH;BUNDLE_SUFFIX;CATEGORIES" "" ${ARGN})

    juce_add_plugin(${target}
        COMPANY_NAME             "ModdMudd"
        COMPANY_WEBSITE          "https://github.com/2ndthunder/moddmudd"
        BUNDLE_ID                "io.github.2ndthunder.moddmudd.${ARG_BUNDLE_SUFFIX}"
        PLUGIN_MANUFACTURER_CODE Mdmd
        PLUGIN_CODE              ${ARG_PLUGIN_CODE}
        PRODUCT_NAME             "${ARG_PRODUCT_NAME}"
        VERSION                  ${PROJECT_VERSION}
        FORMATS                  VST3
        IS_SYNTH                 ${ARG_IS_SYNTH}
        NEEDS_MIDI_INPUT         TRUE
        NEEDS_MIDI_OUTPUT        FALSE
        IS_MIDI_EFFECT           FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES          ${ARG_CATEGORIES}
        COPY_PLUGIN_AFTER_BUILD  ${MODDMUDD_COPY_AFTER_BUILD})

    target_sources(${target} PRIVATE ${MODDMUDD_PLUGIN_SOURCES})

    target_compile_definitions(${target} PUBLIC
        ${MODDMUDD_COMMON_DEFINITIONS}
        MODDMUDD_IS_INSTRUMENT=$<IF:$<BOOL:${ARG_IS_SYNTH}>,1,0>)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_processors
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

moddmudd_add_plugin(ModdMuddFX
    PRODUCT_NAME  "ModdMudd FX"
    PLUGIN_CODE   MmFx
    IS_SYNTH      FALSE
    BUNDLE_SUFFIX fx
    CATEGORIES    Fx Tools)

moddmudd_add_plugin(ModdMuddInstrument
    PRODUCT_NAME  "ModdMudd Instrument"
    PLUGIN_CODE   MmIn
    IS_SYNTH      TRUE
    BUNDLE_SUFFIX instrument
    CATEGORIES    Instrument Synth)

#==============================================================================
if(MODDMUDD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
