cmake_minimum_required(VERSION 3.22)
# 0.0.0: the public release starts at zero. The core's state-format lineage
# (PresetIO productVersion, the PRE-0.3 migration) is its own number and does
# not follow this one.
project(JimothyFeral VERSION 0.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Jimothy Feral — the released edition ──────────────────────────────────────
# This project stands alone. It builds from two layouts without edits:
#
#   * inside the Jimothy repo, where the core (FX, Presets), Resources, art
#     and tests live one directory up, and
#   * inside the shipped source drop, where package.py has folded the core
#     into Source/ and everything sits beside this file.
#
# Licensing is split and the split is deliberate: the software is AGPL-3
# (COPYING), the artwork is CC BY-NC-ND 4.0 (LICENSE-ART). See LICENSE for
# the map.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/FX")
    set(JIMOTHY_CORE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    set(JIMOTHY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
else()
    set(JIMOTHY_CORE "${CMAKE_CURRENT_SOURCE_DIR}/../Source")
    set(JIMOTHY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
endif()

# ── JUCE via FetchContent ─────────────────────────────────────────────────────
# JUCE 7 rides its GPL-3 tier here; GPL-3 section 13 is what lets it travel
# with AGPL-3 code. Pinned to match the main Jimothy tree.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.9
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ── Plugin target ─────────────────────────────────────────────────────────────
# Its own plugin code and bundle id: a host must never confuse the feral
# edition with the full one, and a session saved by one must not load into
# the other by accident.
juce_add_plugin(JimothyFeral
    COMPANY_NAME                "Kaamos"
    COMPANY_WEBSITE             ""
    PLUGIN_MANUFACTURER_CODE    Kmos
    PLUGIN_CODE                 Jimf
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Jimothy Feral"
    BUNDLE_ID                   "com.kaamos.jimothy.feral"
    DESCRIPTION                 "Jimothy Feral — a hyperfuzz that will not be tamed"

    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    VST3_CATEGORIES             "Fx|Distortion"
    AU_MAIN_TYPE                kAudioUnitType_Effect

    COPY_PLUGIN_AFTER_BUILD     FALSE
)

# ── Embedded data ─────────────────────────────────────────────────────────────
# warren.wav is the habitat. jimothy-face.svg is the whole front panel — it is
# CC BY-NC-ND 4.0, embedded as data, aggregated with the program rather than
# licensed under its terms. No factory bank: the feral edition has no presets.
juce_add_binary_data(JimothyFeralResources
    SOURCES
        "${JIMOTHY_ROOT}/Resources/warren.wav"
        "${JIMOTHY_ROOT}/art/jimothy-face.svg"
)
set_target_properties(JimothyFeralResources PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# ── Sources ───────────────────────────────────────────────────────────────────
target_sources(JimothyFeral PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/GUI/JimothyLookAndFeel.cpp
    Source/GUI/FeralFace.cpp
    ${JIMOTHY_CORE}/FX/JimothyPedal.cpp
    ${JIMOTHY_CORE}/Presets/PresetIO.cpp
)

target_include_directories(JimothyFeral PRIVATE Source ${JIMOTHY_CORE})

# ── Compile definitions ───────────────────────────────────────────────────────
target_compile_definitions(JimothyFeral PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(JimothyFeral
    PRIVATE
        JimothyFeralResources
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ── The burst harness ─────────────────────────────────────────────────────────
# Runs the same JimothyShape.h the plugin ships through the hardware's
# measurement protocols and enforces the behavioral contract. JUCE-free on
# purpose — the core must stay pure.
option(JIMOTHY_HARNESS "Build the burst harness" ON)
if(JIMOTHY_HARNESS)
    add_executable(jimothy_harness "${JIMOTHY_ROOT}/tests/burst_harness.cpp")
    set_target_properties(jimothy_harness PROPERTIES CXX_STANDARD 20)
endif()
