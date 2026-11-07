#include "PluginProcessor.h"
#include "PluginEditor.h"

BlackMagicAudioProcessor::BlackMagicAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BLACKMAGIC", createLayout())
{
    retuneP    = apvts.getRawParameterValue ("retune");
    keyP       = apvts.getRawParameterValue ("key");
    scaleP     = apvts.getRawParameterValue ("scale");
    inputTypeP = apvts.getRawParameterValue ("inputType");
    throatP    = apvts.getRawParameterValue ("throat");
    throatOnP  = apvts.getRawParameterValue ("throatOn");
    trackingP  = apvts.getRawParameterValue ("tracking");
    humanizeP  = apvts.getRawParameterValue ("humanize");
}

juce::AudioProcessorValueTreeState::ParameterLayout BlackMagicAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto intText = [] (float v, int) { return String (roundToInt (v)); };
    auto knob = [&] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 100.0f, 0.1f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction (intText)));
    };

    knob ("retune",   "Retune Speed", 0.0f);     // 0 = el mas robotico
    knob ("throat",   "Throat",       50.0f);    // 50 = neutro
    knob ("tracking", "Tracking",     50.0f);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "throatOn", 1 }, "Throat On", false));
    knob ("humanize", "Humanize",     0.0f);

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "key", 1 }, "Key",
        StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "scale", 1 }, "Scale",
        StringArray { "Chromatic", "Major", "Minor", "Harmonic Minor", "Pentatonic Major", "Pentatonic Minor" }, 2));   // por defecto: Minor

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "inputType", 1 }, "Input Type",
        StringArray { "Low Male", "Alto/Tenor", "Female" }, 1));

    return layout;
}

void BlackMagicAudioProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumInputChannels()),
                    (int) std::lround (inputTypeP->load()));
    setLatencySamples (engine.getLatency());
}

bool BlackMagicAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void BlackMagicAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    bm::EngineParams p;
    p.retune    = retuneP->load();
    p.key       = (int) std::lround (keyP->load());
    p.scale     = (int) std::lround (scaleP->load());
    p.inputType = (int) std::lround (inputTypeP->load());
    p.throat    = throatP->load();
    p.throatOn  = throatOnP->load() > 0.5f;
    p.tracking  = trackingP->load();
    p.humanize  = humanizeP->load();

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);

    if (p.inputType != engine.getInputType())      // cambia el rango de voz => cambia la latencia
    {
        engine.prepare (getSampleRate(), numCh, p.inputType);
        setLatencySamples (engine.getLatency());
    }

    engine.process (buffer.getArrayOfWritePointers(), numCh, buffer.getNumSamples(), p);
    displayTarget.store (engine.getTargetNote(), std::memory_order_relaxed);
    displayCents.store  (engine.getCents(),      std::memory_order_relaxed);
    displayHz.store     (engine.getInputHz(),    std::memory_order_relaxed);
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
