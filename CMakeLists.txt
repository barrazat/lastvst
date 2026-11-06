cmake_minimum_required(VERSION 3.22)
project(BlackMagic VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(BlackMagic
    COMPANY_NAME "Dungeon Estudio"
    PRODUCT_NAME "Black Magic"
    PLUGIN_MANUFACTURER_CODE Dgne
    PLUGIN_CODE Bmgc
    FORMATS VST3 Standalone
    VST3_CATEGORIES Fx "Pitch Shift"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(BlackMagic PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# ---- Recursos incrustados: logo + cualquier fuente .otf/.ttf que pongas en Resources/ ----
file(GLOB BM_FONT_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/*.otf"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/*.ttf")

juce_add_binary_data(BlackMagicAssets SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/dungeon_logo.png"
    ${BM_FONT_FILES})
set_target_properties(BlackMagicAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
target_link_libraries(BlackMagic PRIVATE BlackMagicAssets)

target_compile_definitions(BlackMagic PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(BlackMagic
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags)
