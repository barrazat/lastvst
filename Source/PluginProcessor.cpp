#include "PluginProcessor.h"
#include "PluginEditor.h"

BlackMagicAudioProcessor::BlackMagicAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pRetune    = apvts.getRawParameterValue ("RETUNE_SPEED");
    pHumanize  = apvts.getRawParameterValue ("HUMANIZE");
    pThroat    = apvts.getRawParameterValue ("THROAT");
    pTracking  = apvts.getRawParameterValue ("TRACKING");
    pInputType = apvts.getRawParameterValue ("INPUT_TYPE");
    pKey       = apvts.getRawParameterValue ("KEY");
    pScale     = apvts.getRawParameterValue ("SCALE");
    pMix       = apvts.getRawParameterValue ("MIX");
    pThroatOn  = apvts.getRawParameterValue ("THROAT_ON");
    pLowLatency = apvts.getRawParameterValue ("LOW_LATENCY");
    apvts.addParameterListener ("LOW_LATENCY", this);
    apvts.addParameterListener ("INPUT_TYPE", this);
}

juce::AudioProcessorValueTreeState::ParameterLayout BlackMagicAudioProcessor::createLayout()
{
    using namespace juce;
    StringArray keys, scales, inputs;
    for (auto* k : blackmagic::kKeyNames)        keys.add (k);
    for (auto* s : blackmagic::kScaleNames)      scales.add (s);
    for (auto* t : blackmagic::kInputTypeNames)  inputs.add (t);

    auto percent = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (roundToInt (v)) + "%"; });
    auto plain = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (roundToInt (v)); });

    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "INPUT_TYPE", 1 }, "Input Type", inputs, 1));
    params.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "KEY", 1 },        "Key",        keys,   0));
    params.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "SCALE", 1 },      "Scale",      scales, 0));

    // 0 = instantaneo (robotico, estilo clasico de Antares) ... 100 = lento y natural
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "RETUNE_SPEED", 1 }, "Retune Speed",
                        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, plain));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "HUMANIZE", 1 }, "Humanize",
                        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, percent));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "THROAT", 1 }, "Throat",
                        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, percent));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "TRACKING", 1 }, "Tracking",
                        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, percent));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "MIX", 1 }, "Mix",
                        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, percent));

    // Botones: Throat desactivado = maxima transparencia; Low Latency = menos retardo para grabar
    params.push_back (std::make_unique<AudioParameterBool> (ParameterID { "THROAT_ON", 1 },   "Throat On",   false));
    params.push_back (std::make_unique<AudioParameterBool> (ParameterID { "LOW_LATENCY", 1 }, "Low Latency", false));

    return { params.begin(), params.end() };
}

void BlackMagicAudioProcessor::prepareToPlay (double sampleRate, int)
{
    uiSampleRate.store (sampleRate);
    engine.prepare (sampleRate);
    reportedLatency = engine.getLatencySamples();
    pendingLatency.store (reportedLatency);
    setLatencySamples (reportedLatency);              // el host compensa la latencia (PDC)
}

bool BlackMagicAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void BlackMagicAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();
    if (numCh < 1 || n < 1) return;

    // Nivel de entrada para el medidor del LCD
    float peak = 0.0f;
    for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, n));
    const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
    uiInputLevel.store (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f));

    blackmagic::AutoTuneEngine::Params p;
    p.retune    = pRetune->load();
    p.humanize  = pHumanize->load();
    p.throat    = pThroat->load();
    p.tracking  = pTracking->load();
    p.inputType = (int) pInputType->load();
    p.mix       = pMix->load() * 0.01f;
    p.throatOn  = pThroatOn->load()   > 0.5f;
    p.lowLatency = pLowLatency->load() > 0.5f;

    bool allowed[12];
    blackmagic::buildAllowedMask ((int) pKey->load(), (int) pScale->load(), allowed);
    for (int i = 0; i < 12; ++i) p.allowed[(size_t) i] = allowed[i];

    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    engine.process (L, R, L, R, n, p);

    for (int ch = 2; ch < numCh; ++ch)
        buffer.clear (ch, 0, n);

    // La latencia depende del modo y del tipo de voz: si cambia, se avisa al host (fuera del hilo de audio)
    if (engine.getLatencySamples() != reportedLatency)
    {
        reportedLatency = engine.getLatencySamples();
        pendingLatency.store (reportedLatency);
        triggerAsyncUpdate();
    }

    uiDetectedMidi.store (engine.getDetectedMidi());
    uiTargetMidi.store   (engine.getTargetMidi());
}

void BlackMagicAudioProcessor::parameterChanged (const juce::String&, float)
{
    // La latencia es funcion del modo y del tipo de voz: se informa al host sin esperar al audio
    const int lat = engine.computeLatency (pLowLatency->load() > 0.5f, (int) pInputType->load());
    if (lat != pendingLatency.load())
    {
        pendingLatency.store (lat);
        triggerAsyncUpdate();
    }
}

void BlackMagicAudioProcessor::handleAsyncUpdate()
{
    setLatencySamples (pendingLatency.load());
}

juce::AudioProcessorEditor* BlackMagicAudioProcessor::createEditor()
{
    return new BlackMagicAudioProcessorEditor (*this);
}

void BlackMagicAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BlackMagicAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BlackMagicAudioProcessor();
}
