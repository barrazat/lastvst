#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/AutoTuneEngine.h"

class BlackMagicAudioProcessor : public juce::AudioProcessor,
                                 private juce::AsyncUpdater,
                                 private juce::AudioProcessorValueTreeState::Listener
{
public:
    BlackMagicAudioProcessor();
    ~BlackMagicAudioProcessor() override
    {
        apvts.removeParameterListener ("LOW_LATENCY", this);
        apvts.removeParameterListener ("INPUT_TYPE", this);
        cancelPendingUpdate();
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Black Magic"; }
    bool acceptsMidi() const override { return false; }
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

    // Para la pantalla LCD de la interfaz (midi -1 = sin voz; nivel 0..1)
    std::atomic<float> uiDetectedMidi { -1.0f }, uiTargetMidi { -1.0f }, uiInputLevel { 0.0f };
    std::atomic<double> uiSampleRate { 44100.0 };

private:
    void handleAsyncUpdate() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;   // recalcula la latencia al instante      // informa al host del cambio de latencia (hilo de mensajes)
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<float> *pRetune = nullptr, *pHumanize = nullptr, *pThroat = nullptr, *pTracking = nullptr,
                       *pInputType = nullptr, *pKey = nullptr, *pScale = nullptr, *pMix = nullptr,
                       *pThroatOn = nullptr, *pLowLatency = nullptr;
    std::atomic<int> pendingLatency { 0 };
    int reportedLatency = -1;

    blackmagic::AutoTuneEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlackMagicAudioProcessor)
};
