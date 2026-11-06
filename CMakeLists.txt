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

# ---- Fuentes opcionales (si existen en Resources/ se incrustan en el plugin) ----
set(BM_ASSETS "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/title_font.ttf")
    list(APPEND BM_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/title_font.ttf")
    target_compile_definitions(BlackMagic PRIVATE BM_HAS_TITLE_FONT=1)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ui_font.ttf")
    list(APPEND BM_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ui_font.ttf")
    target_compile_definitions(BlackMagic PRIVATE BM_HAS_UI_FONT=1)
endif()
if(BM_ASSETS)
    juce_add_binary_data(BlackMagicAssets SOURCES ${BM_ASSETS})
    set_target_properties(BlackMagicAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    target_link_libraries(BlackMagic PRIVATE BlackMagicAssets)
endif()

target_compile_definitions(BlackMagic PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(BlackMagic
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags)
