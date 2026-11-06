#include "PluginEditor.h"

namespace
{
    juce::String noteName (int midi)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        if (midi < 0) return "--";
        return juce::String (names[midi % 12]) + juce::String (midi / 12 - 1);
    }
}

BlackMagicAudioProcessorEditor::BlackMagicAudioProcessorEditor (BlackMagicAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    auto setup = [this] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                               juce::MathConstants<float>::pi * 2.8f, true);
        s.setPopupDisplayEnabled (false, false, nullptr);   // sin bocadillos/tooltips
        addAndMakeVisible (s);
    };
    setup (retuneSlider);
    setup (throatSlider);
    setup (trackingSlider);
    setup (humanizeSlider);

    retuneAtt   = std::make_unique<SliderAtt> (proc.apvts, "retune",   retuneSlider);
    throatAtt   = std::make_unique<SliderAtt> (proc.apvts, "throat",   throatSlider);
    trackingAtt = std::make_unique<SliderAtt> (proc.apvts, "tracking", trackingSlider);
    humanizeAtt = std::make_unique<SliderAtt> (proc.apvts, "humanize", humanizeSlider);

    inputField.onClick = [this] { openMenu ("inputType", inputNames, inputField); };
    keyField.onClick   = [this] { openMenu ("key",       keyNames,   keyField);   };
    scaleField.onClick = [this] { openMenu ("scale",     scaleNames, scaleField); };
    addAndMakeVisible (inputField);
    addAndMakeVisible (keyField);
    addAndMakeVisible (scaleField);

    setSize (700, 420);
    setResizable (false, false);
    timerCallback();
    startTimerHz (30);
}

BlackMagicAudioProcessorEditor::~BlackMagicAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void BlackMagicAudioProcessorEditor::openMenu (const juce::String& paramId, const juce::StringArray& items,
                                               juce::Component& target)
{
    auto* param = proc.apvts.getParameter (paramId);
    if (param == nullptr) return;

    const int current = (int) std::lround (proc.apvts.getRawParameterValue (paramId)->load());

    juce::PopupMenu menu;
    menu.setLookAndFeel (&laf);
    for (int i = 0; i < items.size(); ++i)
        menu.addItem (i + 1, items[i], true, i == current);

    juce::Component::SafePointer<BlackMagicAudioProcessorEditor> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target)
                                                  .withMinimumWidth (target.getWidth()),
                        [safeThis, param] (int result)
                        {
                            if (safeThis == nullptr || result <= 0) return;
                            param->beginChangeGesture();
                            param->setValueNotifyingHost (param->convertTo0to1 ((float) (result - 1)));
                            param->endChangeGesture();
                        });
}

void BlackMagicAudioProcessorEditor::timerCallback()
{
    auto idx = [this] (const char* id) { return (int) std::lround (proc.apvts.getRawParameterValue (id)->load()); };
    inputField.setText (inputNames[juce::jlimit (0, inputNames.size() - 1, idx ("inputType"))]);
    keyField.setText   (keyNames  [juce::jlimit (0, keyNames.size()   - 1, idx ("key"))]);
    scaleField.setText (scaleNames[juce::jlimit (0, scaleNames.size() - 1, idx ("scale"))]);

    // nota de salida: solo cambia si la nueva se mantiene un rato
    const int cand = proc.displayTarget.load (std::memory_order_relaxed);
    if (cand == shownNote)            { candidateTicks = 0; candidateNote = cand; }
    else if (cand == candidateNote)   { ++candidateTicks; }
    else                              { candidateNote = cand; candidateTicks = 0; }

    const int needed = (candidateNote < 0) ? 9 : 5;   // ~0.3 s para apagarse, ~0.17 s para cambiar de nota
    if (candidateNote != shownNote && candidateTicks >= needed)
    {
        shownNote = candidateNote;
        candidateTicks = 0;
        repaint (getLcdBounds());
    }
}

void BlackMagicAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace bm;

    g.setGradientFill (juce::ColourGradient (colours::bgTop, 0.0f, 0.0f,
                                             colours::bgBottom, 0.0f, (float) getHeight(), false));
    g.fillAll();

    g.setColour (colours::accent.withAlpha (0.30f));
    g.drawRect (getLocalBounds().reduced (6), 1);

    // titulo
    g.setColour (colours::bone);
    g.setFont (fonts::title (46.0f));
    g.drawText ("Black Magic", 30, 14, 400, 52, juce::Justification::centredLeft, false);
    g.setColour (colours::accent);
    g.setFont (fonts::ui (11.0f));
    g.drawText ("D U N G E O N   E S T U D I O", 34, 68, 360, 16, juce::Justification::centredLeft, false);

    // LCD
    const auto lcd = getLcdBounds();
    g.setColour (colours::lcdBg);
    g.fillRoundedRectangle (lcd.toFloat(), 8.0f);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (lcd);
        g.setColour (juce::Colours::black.withAlpha (0.20f));
        for (int y = lcd.getY(); y < lcd.getBottom(); y += 3)
            g.fillRect (lcd.getX(), y, lcd.getWidth(), 1);
    }
    g.setColour (juce::Colour (0xff2b2236));
    g.drawRoundedRectangle (lcd.toFloat(), 8.0f, 2.0f);

    g.setFont (fonts::ui (11.0f));
    g.setColour (colours::lcdDim.brighter (0.35f));
    g.drawText ("OUTPUT NOTE", lcd.getX() + 20, lcd.getY() + 10, 160, 14, juce::Justification::centredLeft, false);

    g.setFont (fonts::ui (78.0f));
    g.setColour (shownNote < 0 ? colours::lcdDim : colours::lcdOn);
    g.drawText (noteName (shownNote), lcd.getX() + 16, lcd.getY() + 26, 250, 92,
                juce::Justification::centredLeft, false);

    g.setColour (colours::lcdDim.withAlpha (0.6f));
    g.drawVerticalLine (lcd.getX() + 272, (float) lcd.getY() + 14.0f, (float) lcd.getBottom() - 14.0f);

    // etiquetas de perillas
    const int colW = (getWidth() - 56) / 4;
    const char* names[4] = { "RETUNE SPEED", "THROAT", "TRACKING", "HUMANIZE" };
    g.setFont (fonts::ui (13.0f));
    g.setColour (colours::bone);
    for (int i = 0; i < 4; ++i)
        g.drawText (names[i], 28 + i * colW, 244, colW, 18, juce::Justification::centred, false);
}

void BlackMagicAudioProcessorEditor::resized()
{
    const auto lcd = getLcdBounds();
    const int fx = lcd.getX() + 292, fw = lcd.getWidth() - 292 - 18;
    const int fh = 31, gap = 6, fy = lcd.getY() + 12;
    inputField.setBounds (fx, fy,                 fw, fh);
    keyField.setBounds   (fx, fy + (fh + gap),    fw, fh);
    scaleField.setBounds (fx, fy + (fh + gap) * 2, fw, fh);

    const int colW = (getWidth() - 56) / 4;
    juce::Slider* sliders[4] = { &retuneSlider, &throatSlider, &trackingSlider, &humanizeSlider };
    for (int i = 0; i < 4; ++i)
        sliders[i]->setBounds (28 + i * colW + 8, 266, colW - 16, 140);
}
