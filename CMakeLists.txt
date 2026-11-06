cmake_minimum_required(VERSION 3.22)

# Runtime de C++ enlazado estaticamente en Windows: el usuario no necesita instalar nada extra
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(BlackMagic VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(BlackMagic
    COMPANY_NAME                "Dungeon Estudio"
    PLUGIN_MANUFACTURER_CODE    Dgne
    PLUGIN_CODE                 Bmgc
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    FORMATS                     VST3
    VST3_CATEGORIES             Fx "Pitch Shift"
    PRODUCT_NAME                "Black Magic")

target_sources(BlackMagic PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# Recursos opcionales (se detectan solos):
#   Assets/dungeon_logo.png   -> logo   (BinaryData::dungeon_logo_png)
#   Assets/ui_font.otf / .ttf -> tipografia de la interfaz (p.ej. Helvetica Neue Display Bold, si tienes la licencia)
set(BM_ASSETS "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/dungeon_logo.png")
    list(APPEND BM_ASSETS Assets/dungeon_logo.png)
    target_compile_definitions(BlackMagic PRIVATE BLACKMAGIC_HAS_LOGO=1)
endif()
# Tipografia gotica del titulo (UnifrakturCook Bold, licencia OFL): Assets/title_font.ttf
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/title_font.ttf")
    list(APPEND BM_ASSETS Assets/title_font.ttf)
    target_compile_definitions(BlackMagic PRIVATE BLACKMAGIC_HAS_TITLE_FONT=1)
endif()
foreach(ext otf ttf)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/ui_font.${ext}")
        list(APPEND BM_ASSETS Assets/ui_font.${ext})
        target_compile_definitions(BlackMagic PRIVATE
            BLACKMAGIC_HAS_FONT=1
            BLACKMAGIC_FONT_DATA=BinaryData::ui_font_${ext}
            BLACKMAGIC_FONT_SIZE=BinaryData::ui_font_${ext}Size)
        break()
    endif()
endforeach()
if(BM_ASSETS)
    juce_add_binary_data(BlackMagicAssets SOURCES ${BM_ASSETS})
    target_link_libraries(BlackMagic PRIVATE BlackMagicAssets)
endif()

target_compile_definitions(BlackMagic PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(BlackMagic
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
