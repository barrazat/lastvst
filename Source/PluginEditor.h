#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Aspecto de BLACK MAGIC: perillas metalicas negras y campos desplegables estilo pantalla LCD
class BlackMagicLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BlackMagicLookAndFeel();

    void setUiScale (float s) noexcept { uiScale = s; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

private:
    float uiScale = 1.0f;
};

// Slider rotativo que solo responde al clic dentro de la perilla
class KnobSlider : public juce::Slider
{
public:
    bool hitTest (int x, int y) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        return c.getDistanceFrom (juce::Point<float> ((float) x, (float) y)) <= (float) getWidth() * 0.40f;
    }
};

class BlackMagicAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit BlackMagicAudioProcessorEditor (BlackMagicAudioProcessor&);
    ~BlackMagicAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void renderBackground();
    void drawStatic (juce::Graphics&);
    void drawDynamic (juce::Graphics&);                // elementos del LCD (en espacio de diseno)
    void drawLcdGlow (juce::Graphics&);                // resplandor del medidor y la nota (en pixeles)
    void setupButton (juce::ToggleButton&, const juce::String& textOn, const juce::String& textOff);
    void setupKnob (KnobSlider&, int ticks);
    void setupCombo (juce::ComboBox&, const juce::StringArray& items);

    BlackMagicAudioProcessor& audioProcessor;
    BlackMagicLookAndFeel laf;

    KnobSlider throatSlider, trackingSlider, retuneSpeedSlider, humanizeSlider;
    juce::ComboBox inputTypeBox, keyBox, scaleBox;
    juce::ToggleButton throatButton, lowLatencyButton;

    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttach> throatAttach, trackingAttach, retuneSpeedAttachment, humanizeAttachment;
    std::unique_ptr<ComboAttach>  inputTypeAttach, keyAttach, scaleAttach;
    std::unique_ptr<ButtonAttach> throatOnAttach, lowLatencyAttach;

    juce::Image dungeonLogo, background;
    float scale = 1.0f;

    // Estado del LCD
    float devCents = 0.0f, lastDevDrawn = 99.0f;    // desviacion de afinacion suavizada (cents)
    bool  haveDev = false;
    int   shownNote = -1;                 // nota mostrada (MIDI), -1 = ninguna
    int   lastShownNote = -2, lastLatency = -1;
    std::array<int, 10> voteRing {};      // ultimas notas objetivo (para el promedio de la nota)
    int   voteIdx = 0;
    bool  lastThroatOn = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlackMagicAudioProcessorEditor)
};
