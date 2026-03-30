cmake_minimum_required(VERSION 3.15)
project(GRID VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Locate Dependencies ───────────────────────────────────────────────────

# JUCE
if(NOT DEFINED JUCE_DIR)
    if(DEFINED ENV{JUCE_DIR})
        set(JUCE_DIR $ENV{JUCE_DIR})
    else()
        set(JUCE_DIR "$ENV{HOME}/SDKs/JUCE")
    endif()
endif()
if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
    message(STATUS "JUCE found at: ${JUCE_DIR}")
else()
    message(FATAL_ERROR "JUCE not found at ${JUCE_DIR}. Set -DJUCE_DIR=<path>")
endif()

# VST3 SDK
if(NOT DEFINED VSTSDK)
    if(DEFINED ENV{VSTSDK})
        set(VSTSDK $ENV{VSTSDK})
    else()
        set(VSTSDK "$ENV{HOME}/SDKs/vst3-sdk")
    endif()
endif()

# ─── Percussa SSP SDK Header ───────────────────────────────────────────────
set(SSP_SDK_DIR "${CMAKE_SOURCE_DIR}/libs/ssp-sdk")

# ─── Plugin Target ─────────────────────────────────────────────────────────
juce_add_plugin(GRID
    COMPANY_NAME           "GridSampler"
    PLUGIN_MANUFACTURER_CODE Grid
    PLUGIN_CODE             Grid
    FORMATS                 VST3
    PRODUCT_NAME            "GRID"
    VST3_CATEGORIES         "Fx|Sampler"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_SDK_PATH           "${VSTSDK}"
)

# ─── Source Files ──────────────────────────────────────────────────────────
target_sources(GRID
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/GridEngine.cpp
        src/SampleSlot.cpp
    PUBLIC
        src/SSPApi.cpp
)

# ─── Include Paths ─────────────────────────────────────────────────────────
target_include_directories(GRID
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${SSP_SDK_DIR}
)

# ─── Compile Definitions ──────────────────────────────────────────────────
target_compile_definitions(GRID
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_USE_FONTCONFIG=0
        SSP_DISPLAY_WIDTH=1600
        SSP_DISPLAY_HEIGHT=480
)

# Link buildroot freetype only (no fontconfig)
set(SYSROOT "${BUILDROOT}/arm-rockchip-linux-gnueabihf/sysroot")
target_link_libraries(GRID PRIVATE
    "${SYSROOT}/usr/lib/libfreetype.so"
    "${SYSROOT}/usr/lib/libasound.so"
)

target_link_libraries(GRID
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
