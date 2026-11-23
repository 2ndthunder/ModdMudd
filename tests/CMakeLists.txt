#==============================================================================
# Engine unit tests (no plug-in or GUI code)
juce_add_console_app(ModdMuddTests PRODUCT_NAME "ModdMuddTests")

target_sources(ModdMuddTests PRIVATE
    TestMain.cpp
    EngineTests.cpp
    ${MODDMUDD_ENGINE_SOURCES})

target_include_directories(ModdMuddTests PRIVATE ${CMAKE_SOURCE_DIR}/Source)

target_compile_definitions(ModdMuddTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(ModdMuddTests PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_data_structures
    juce::juce_events
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

add_test(NAME EngineTests COMMAND ModdMuddTests)

#==============================================================================
# Small VST3 plug-ins used to test real hosting.
function(moddmudd_add_test_plugin target)
    cmake_parse_arguments(ARG "" "CODE;IS_SYNTH;SOURCE" "" ${ARGN})

    juce_add_plugin(${target}
        COMPANY_NAME             "ModdMuddTest"
        BUNDLE_ID                "io.github.2ndthunder.moddmudd.test.${target}"
        PLUGIN_MANUFACTURER_CODE MmTs
        PLUGIN_CODE              ${ARG_CODE}
        PRODUCT_NAME             "${target}"
        FORMATS                  VST3
        IS_SYNTH                 ${ARG_IS_SYNTH}
        NEEDS_MIDI_INPUT         ${ARG_IS_SYNTH}
        COPY_PLUGIN_AFTER_BUILD  FALSE)

    target_sources(${target} PRIVATE ${ARG_SOURCE})
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(${target} PRIVATE juce::juce_audio_processors
                          PUBLIC juce::juce_recommended_config_flags)
endfunction()

moddmudd_add_test_plugin(MuddTestGain  CODE MtGn IS_SYNTH FALSE SOURCE TestPlugins/TestGain.cpp)
moddmudd_add_test_plugin(MuddTestSynth CODE MtSy IS_SYNTH TRUE  SOURCE TestPlugins/TestSynth.cpp)

#==============================================================================
# End-to-end hosting test: a headless host loads ModdMudd (VST3), which in
# turn hosts the test plug-ins (VST3), and checks the audio.
juce_add_console_app(ModdMuddHostTest PRODUCT_NAME "ModdMuddHostTest")
target_sources(ModdMuddHostTest PRIVATE HostTest.cpp)
target_compile_definitions(ModdMuddHostTest PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_LADSPA=0 JUCE_PLUGINHOST_LV2=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(ModdMuddHostTest PRIVATE
    juce::juce_audio_processors_headless
    juce::juce_events
    juce::juce_data_structures
    juce::juce_recommended_config_flags)

add_dependencies(ModdMuddHostTest ModdMuddFX_VST3 ModdMuddInstrument_VST3 MuddTestGain_VST3 MuddTestSynth_VST3)

add_test(NAME HostingTest
    COMMAND ModdMuddHostTest
        "$<GENEX_EVAL:$<TARGET_PROPERTY:ModdMuddFX_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>"
        "$<GENEX_EVAL:$<TARGET_PROPERTY:ModdMuddInstrument_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>"
        "$<GENEX_EVAL:$<TARGET_PROPERTY:MuddTestGain_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>"
        "$<GENEX_EVAL:$<TARGET_PROPERTY:MuddTestSynth_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")

#==============================================================================
# Development tool (not shipped): renders the editor to PNG files so the
# layout can be checked without a DAW.
juce_add_console_app(ModdMuddUISnapshot PRODUCT_NAME "ModdMuddUISnapshot")
target_sources(ModdMuddUISnapshot PRIVATE UISnapshot.cpp)
# Reuse the FX plug-in's compiled code (including its copy of JUCE) rather
# than compiling JUCE a second time.
target_include_directories(ModdMuddUISnapshot PRIVATE $<TARGET_PROPERTY:ModdMuddFX,INCLUDE_DIRECTORIES>)
target_compile_definitions(ModdMuddUISnapshot PRIVATE $<TARGET_PROPERTY:ModdMuddFX,COMPILE_DEFINITIONS>)
target_link_libraries(ModdMuddUISnapshot PRIVATE ModdMuddFX)

#==============================================================================
# Processor-level test with a running message loop (preset capture etc.).
juce_add_console_app(ModdMuddProcessorTest PRODUCT_NAME "ModdMuddProcessorTest")
target_sources(ModdMuddProcessorTest PRIVATE ProcessorTest.cpp)
target_include_directories(ModdMuddProcessorTest PRIVATE $<TARGET_PROPERTY:ModdMuddFX,INCLUDE_DIRECTORIES>)
target_compile_definitions(ModdMuddProcessorTest PRIVATE $<TARGET_PROPERTY:ModdMuddFX,COMPILE_DEFINITIONS>)
target_link_libraries(ModdMuddProcessorTest PRIVATE ModdMuddFX)
add_dependencies(ModdMuddProcessorTest MuddTestGain_VST3)

add_test(NAME ProcessorTest
    COMMAND ModdMuddProcessorTest "$<GENEX_EVAL:$<TARGET_PROPERTY:MuddTestGain_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
