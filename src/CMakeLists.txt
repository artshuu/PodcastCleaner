cmake_minimum_required(VERSION 3.22)

if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS plugin architectures")
endif()

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()

project(PodcastCleaner VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(JUCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Path to the JUCE 7 source tree")
if(NOT EXISTS "${JUCE_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE was not found at '${JUCE_ROOT}'. Set JUCE_ROOT to a JUCE 7 source checkout.")
endif()

add_subdirectory("${JUCE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")

juce_add_plugin(PodcastCleaner
    COMPANY_NAME "Podcast Cleaner"
    BUNDLE_ID "audio.podcastcleaner.plugin"
    PLUGIN_MANUFACTURER_CODE PcLn
    PLUGIN_CODE Pc01
    PRODUCT_NAME "Podcast Cleaner"
    DESCRIPTION "In-place podcast cleanup audio effect"
    FORMATS VST3
    VST3_CATEGORIES Fx Dynamics
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(PodcastCleaner PRIVATE
    AudioProcessor.cpp
    AudioProcessor.h
    AudioProcessorEditor.cpp
    AudioProcessorEditor.h
    modules/PodcastCleanerCore/DSPStages/HighPassFilter.cpp
    modules/PodcastCleanerCore/DSPStages/HighPassFilter.h
)

target_include_directories(PodcastCleaner PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

juce_generate_juce_header(PodcastCleaner)

target_compile_definitions(PodcastCleaner
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(PodcastCleaner
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
