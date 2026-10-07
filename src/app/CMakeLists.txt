cmake_minimum_required(VERSION 3.22)

project(PodcastCleanerApp VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PODCASTCLEANER_BUILD_GUI "Build the Podcast Cleaner GUI front-end" ON)

# --- whisper.cpp (offline speech recognition) -------------------------------
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(WHISPER_ALL_WARNINGS   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)

set(WHISPER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/whisper.cpp")
if(NOT EXISTS "${WHISPER_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "whisper.cpp not found at '${WHISPER_SOURCE_DIR}'. Clone ggml-org/whisper.cpp there.")
endif()
add_subdirectory("${WHISPER_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/whisper" EXCLUDE_FROM_ALL)

# --- Podcast Cleaner core + ML ----------------------------------------------
set(CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../modules/PodcastCleanerCore")
set(ML_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/../modules/MLProcessingModule")

set(CORE_SOURCES
    "${CORE_DIR}/DSPUtils/Fft.cpp"
    "${CORE_DIR}/DSPUtils/Stft.cpp"
    "${CORE_DIR}/DSPUtils/Resampler.cpp"
    "${CORE_DIR}/Analysis/VoiceActivityDetector.cpp"
    "${CORE_DIR}/Analysis/LoudnessMeter.cpp"
    "${CORE_DIR}/Analysis/ProfanityDetector.cpp"
    "${CORE_DIR}/DSPStages/SpectralDenoiser.cpp"
    "${CORE_DIR}/DSPStages/NonSpeechSilencer.cpp"
    "${CORE_DIR}/DSPStages/CensorStage.cpp"
    "${CORE_DIR}/DSPStages/TruePeakLimiter.cpp"
)

# Shared by the console and GUI front-ends so the two stay in sync.
add_library(podcast-cleaner-lib STATIC
    WavFile.cpp
    OfflinePipeline.cpp
    ${CORE_SOURCES}
    "${ML_DIR}/WhisperTranscriptProvider.cpp"
)

target_include_directories(podcast-cleaner-lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CORE_DIR}"
    "${ML_DIR}"
)

target_link_libraries(podcast-cleaner-lib PRIVATE whisper)

# --- Console front-end ------------------------------------------------------
add_executable(podcast-cleaner main.cpp)

target_link_libraries(podcast-cleaner PRIVATE podcast-cleaner-lib)

# --- Core DSP/analysis tests (do not need whisper or the model) -------------
add_executable(podcast-cleaner-tests
    tests/CoreTests.cpp
    ${CORE_SOURCES}
)

target_include_directories(podcast-cleaner-tests PRIVATE
    "${CORE_DIR}"
    "${ML_DIR}"
)

# --- GUI front-end (JUCE) ---------------------------------------------------
if(PODCASTCLEANER_BUILD_GUI)
    set(JUCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE" CACHE PATH "Path to the JUCE 7 source tree")
    if(NOT EXISTS "${JUCE_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR
            "JUCE was not found at '${JUCE_ROOT}'. Set JUCE_ROOT to a JUCE 7 source checkout, "
            "or configure with -DPODCASTCLEANER_BUILD_GUI=OFF.")
    endif()
    add_subdirectory("${JUCE_ROOT}" "${CMAKE_BINARY_DIR}/JUCE")

    juce_add_gui_app(podcast-cleaner-gui
        PRODUCT_NAME "Podcast Cleaner"
        COMPANY_NAME "Podcast Cleaner"
        BUNDLE_ID "audio.podcastcleaner.app"
        VERSION "0.2.0"
    )

    target_sources(podcast-cleaner-gui PRIVATE
        gui/GuiMain.cpp
        gui/MainComponent.cpp
        gui/MainComponent.h
    )

    juce_generate_juce_header(podcast-cleaner-gui)

    target_compile_definitions(podcast-cleaner-gui PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(podcast-cleaner-gui
        PRIVATE
            podcast-cleaner-lib
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
    )
endif()
