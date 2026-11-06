#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "PitchEngine.h"

class BlackMagicAudioProcessor : public juce::AudioProcessor
{
public:
    BlackMagicAudioProcessor();
    ~BlackMagicAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Black Magic"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Nota de salida actual (MIDI) o -1 si no hay voz. La lee la interfaz (LCD).
    std::atomic<int>   displayTarget { -1 };
    // Desviacion de la voz respecto a la nota objetivo en cents (-50..+50): la usa el medidor TUNING.
    std::atomic<float> displayCents  { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    bm::PitchEngine engine;

    std::atomic<float>* retuneP    = nullptr;
    std::atomic<float>* keyP       = nullptr;
    std::atomic<float>* scaleP     = nullptr;
    std::atomic<float>* inputTypeP = nullptr;
    std::atomic<float>* throatP    = nullptr;
    std::atomic<float>* throatOnP  = nullptr;
    std::atomic<float>* trackingP  = nullptr;
    std::atomic<float>* humanizeP  = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlackMagicAudioProcessor)
};
