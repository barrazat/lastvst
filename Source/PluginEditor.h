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
    juce::Rectangle<int> getLcdBounds() const { return { 28, 100, getWidth() - 56, 128 }; }

    BlackMagicAudioProcessor& proc;
    bm::BMLookAndFeel laf;

    juce::Slider retuneSlider, throatSlider, trackingSlider, humanizeSlider;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAtt> retuneAtt, throatAtt, trackingAtt, humanizeAtt;

    bm::LcdField inputField { "INPUT" }, keyField { "KEY" }, scaleField { "SCALE" };

    juce::StringArray inputNames { "Low Male", "Alto/Tenor", "Female" };
    juce::StringArray keyNames   { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    juce::StringArray scaleNames { "Chromatic", "Major", "Minor", "Harmonic Minor", "Pentatonic Major", "Pentatonic Minor" };

    // LCD: nota de salida promediada (no cambia de golpe)
    int shownNote = -1, candidateNote = -1, candidateTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlackMagicAudioProcessorEditor)
};
