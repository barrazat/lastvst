#pragma once

#include "PluginProcessor.h"
#include "UI.h"

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
    void openMenu (const juce::String& paramId, const juce::StringArray& items, juce::Component& target);
    void drawStatic (juce::Graphics&) const;     // todo lo fijo, en coordenadas de la imagen de referencia
    void ensureBackground();
    juce::Rectangle<int> r (float x, float y, float w, float h) const;

    BlackMagicAudioProcessor& proc;
    bm::BMLookAndFeel laf;

    float scale = 1.0f;
    juce::Image background, logo;

    juce::Slider retuneSlider, throatSlider, trackingSlider, humanizeSlider;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAtt> retuneAtt, throatAtt, trackingAtt, humanizeAtt;
    std::unique_ptr<ButtonAtt> throatOnAtt;

    bm::PillToggle throatToggle;
    bm::DropField  inputField, keyField, scaleField;
    bm::LcdDisplay lcd;

    juce::StringArray inputNames { "Low Male", "Alto/Tenor", "Female" };
    juce::StringArray keyNames   { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    juce::StringArray scaleNames { "Chromatic", "Major", "Minor", "Harmonic Minor", "Pentatonic Major", "Pentatonic Minor" };

    bm::LcdDisplay::State lcdState;
    float smoothCents = 0.0f, heldCents = 0.0f;
    bool  throatDim = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlackMagicAudioProcessorEditor)
};
