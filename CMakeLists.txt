cmake_minimum_required(VERSION 3.22)
project(AmphiLoop VERSION 0.3.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

add_library(AmphiLoopEngine Source/LoopEngine.cpp)
target_include_directories(AmphiLoopEngine PUBLIC Source)
if(MSVC)
  target_compile_options(AmphiLoopEngine PRIVATE /W4)
endif()

add_executable(AmphiLoopTests Tests/EngineTests.cpp)
target_link_libraries(AmphiLoopTests PRIVATE AmphiLoopEngine)
add_test(NAME Engine COMMAND AmphiLoopTests)

option(AMPHILOOP_BUILD_PLUGIN "Build VST3" ON)
if(AMPHILOOP_BUILD_PLUGIN)
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)

  juce_add_binary_data(AmphiLoopBranding SOURCES Assets/AmphiLoopWordmark.svg)
  juce_add_plugin(AmphiLoop COMPANY_NAME "amphiox" COMPANY_COPYRIGHT "Copyright (c) 2026 Jesse Manser"
    BUNDLE_ID "com.amphiox.amphiloop" PLUGIN_MANUFACTURER_CODE Amph PLUGIN_CODE AmLp
    FORMATS VST3 PRODUCT_NAME "AmphiLoop" IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE COPY_PLUGIN_AFTER_BUILD FALSE)
  target_sources(AmphiLoop PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_include_directories(AmphiLoop PRIVATE "${juce_SOURCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK")
  target_compile_definitions(AmphiLoop PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
  target_link_libraries(AmphiLoop PRIVATE AmphiLoopEngine AmphiLoopBranding juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
  juce_generate_juce_header(AmphiLoop)

  add_executable(AmphiLoopProcessorTests Tests/ProcessorTests.cpp)
  target_include_directories(AmphiLoopProcessorTests PRIVATE Source "${CMAKE_CURRENT_BINARY_DIR}/AmphiLoop_artefacts/JuceLibraryCode"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK")
  target_compile_definitions(AmphiLoopProcessorTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
  target_link_libraries(AmphiLoopProcessorTests PRIVATE AmphiLoop AmphiLoopBranding juce::juce_audio_utils)
  add_test(NAME Processor COMMAND AmphiLoopProcessorTests)

  add_executable(AmphiLoopHostTests Tests/HostTests.cpp)
  target_include_directories(AmphiLoopHostTests PRIVATE "${juce_SOURCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK")
  target_compile_definitions(AmphiLoopHostTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1
    JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(AmphiLoopHostTests PRIVATE juce::juce_audio_processors)
  add_dependencies(AmphiLoopHostTests AmphiLoop_VST3)
  add_test(NAME VST3Host COMMAND AmphiLoopHostTests "${CMAKE_CURRENT_BINARY_DIR}/AmphiLoop_artefacts/$<CONFIG>/VST3/AmphiLoop.vst3")
endif()

