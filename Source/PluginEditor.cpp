#include "PluginEditor.h"

namespace
{
    constexpr float kRefW = 1027.0f, kRefH = 577.0f;
}

//==============================================================================
BlackMagicAudioProcessorEditor::BlackMagicAudioProcessorEditor (BlackMagicAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    logo = juce::ImageCache::getFromMemory (BinaryData::dungeon_logo_png, BinaryData::dungeon_logo_pngSize);

    auto setup = [this] (juce::Slider& s, double defaultValue)
    {
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
        s.setMouseDragSensitivity (200);
        s.setDoubleClickReturnValue (true, defaultValue);
        addAndMakeVisible (s);
    };
    setup (retuneSlider,   0.0);
    setup (throatSlider,   50.0);
    setup (trackingSlider, 50.0);
    setup (humanizeSlider, 0.0);

    retuneAtt   = std::make_unique<SliderAtt> (proc.apvts, "retune",   retuneSlider);
    throatAtt   = std::make_unique<SliderAtt> (proc.apvts, "throat",   throatSlider);
    trackingAtt = std::make_unique<SliderAtt> (proc.apvts, "tracking", trackingSlider);
    humanizeAtt = std::make_unique<SliderAtt> (proc.apvts, "humanize", humanizeSlider);

    addAndMakeVisible (throatToggle);
    throatOnAtt = std::make_unique<ButtonAtt> (proc.apvts, "throatOn", throatToggle);
    throatSlider.getProperties().set ("dim", true);

    inputField.onClick = [this] { openMenu ("inputType", inputNames, inputField); };
    keyField.onClick   = [this] { openMenu ("key",       keyNames,   keyField);   };
    scaleField.onClick = [this] { openMenu ("scale",     scaleNames, scaleField); };
    addAndMakeVisible (inputField);
    addAndMakeVisible (keyField);
    addAndMakeVisible (scaleField);
    addAndMakeVisible (lcd);

    setResizable (true, true);
    setResizeLimits (720, 405, 1540, 865);          // primero los limites: crean el constrainer
    getConstrainer()->setFixedAspectRatio ((double) kRefW / (double) kRefH);
    setSize (880, 494);

    timerCallback();
    startTimerHz (30);
}

BlackMagicAudioProcessorEditor::~BlackMagicAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

juce::Rectangle<int> BlackMagicAudioProcessorEditor::r (float x, float y, float w, float h) const
{
    return juce::Rectangle<float> (x * scale, y * scale, w * scale, h * scale).toNearestInt();
}

//==============================================================================
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

    // nota de salida: solo cambia si la nueva se mantiene un rato (promedio visual)
    const int cand = proc.displayTarget.load (std::memory_order_relaxed);
    if (cand == shownNote)          { candidateTicks = 0; candidateNote = cand; }
    else if (cand == candidateNote) { ++candidateTicks; }
    else                            { candidateNote = cand; candidateTicks = 0; }

    const int needed = (candidateNote < 0) ? 9 : 5;
    if (candidateNote != shownNote && candidateTicks >= needed)
    {
        shownNote = candidateNote;
        candidateTicks = 0;
    }

    // medidor TUNING
    const float target = shownNote >= 0 ? juce::jlimit (-1.0f, 1.0f, proc.displayCents.load (std::memory_order_relaxed) / 50.0f) : 0.0f;
    meter += 0.4f * (target - meter);
    lcd.setState (shownNote, meter);

    // Throat apagado => perilla atenuada y sin marcas encendidas
    const bool dim = ! throatToggle.getToggleState();
    if (dim != throatDim)
    {
        throatDim = dim;
        throatSlider.getProperties().set ("dim", dim);
        throatSlider.repaint();
    }

    // latencia real que informa el plugin
    const double sr = proc.getSampleRate();
    const juce::String t = sr > 0.0 ? juce::String (1000.0 * proc.getLatencySamples() / sr, 1) + " ms" : juce::String ("-- ms");
    if (t != latencyText)
    {
        latencyText = t;
        repaint (r (770.0f, 425.0f, 180.0f, 26.0f));
    }
}

//==============================================================================
void BlackMagicAudioProcessorEditor::drawStatic (juce::Graphics& g) const
{
    using namespace bm;
    g.setImageResampling (juce::Graphics::highResamplingQuality);

    // ---- fondo metalico oscuro con cepillado ----
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a1a1c), 0.0f, 0.0f,
                                             juce::Colour (0xff0c0c0e), 0.0f, kRefH, false));
    g.fillRect (0.0f, 0.0f, kRefW, kRefH);
    {
        juce::Random rng (7);
        for (int i = 0; i < 900; ++i)
        {
            const float x = rng.nextFloat() * kRefW, y = rng.nextFloat() * kRefH;
            const float len = 40.0f + rng.nextFloat() * 420.0f;
            g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black)
                             .withAlpha (0.012f + 0.03f * rng.nextFloat()));
            g.fillRect (x, y, len, 0.8f);
        }
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, kRefW * 0.5f, kRefH * 0.5f,
                                             juce::Colours::black.withAlpha (0.45f), 0.0f, 0.0f, true));
    g.fillRect (0.0f, 0.0f, kRefW, kRefH);

    // marco
    g.setColour (juce::Colour (0xff2b2b2f));
    g.drawRoundedRectangle (3.0f, 3.0f, kRefW - 6.0f, kRefH - 6.0f, 16.0f, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawRoundedRectangle (5.0f, 5.0f, kRefW - 10.0f, kRefH - 10.0f, 14.0f, 1.0f);

    // ---- tornillos ----
    auto screw = [&] (float cx, float cy, float rot)
    {
        g.setColour (juce::Colour (0xff050506));
        g.fillEllipse (cx - 12.0f, cy - 10.0f, 24.0f, 24.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a6b70), cx - 4.0f, cy - 5.0f,
                                                 juce::Colour (0xff222326), cx + 5.0f, cy + 6.0f, false));
        g.fillEllipse (cx - 11.0f, cy - 11.0f, 22.0f, 22.0f);
        g.setColour (juce::Colour (0xff4b4c51));
        g.drawEllipse (cx - 11.0f, cy - 11.0f, 22.0f, 22.0f, 1.0f);
        juce::Path slot;
        slot.addRoundedRectangle (-6.5f, -1.4f, 13.0f, 2.8f, 1.2f);
        slot.addRoundedRectangle (-1.4f, -6.5f, 2.8f, 13.0f, 1.2f);
        g.setColour (juce::Colour (0xff111113));
        g.fillPath (slot, juce::AffineTransform::rotation (rot).translated (cx, cy));
    };
    screw (33.0f, 36.0f, 0.78f);   screw (991.0f, 36.0f, 0.78f);
    screw (33.0f, 542.0f, 0.78f);  screw (991.0f, 542.0f, 0.0f);
    screw (290.0f, 437.0f, 0.78f); screw (734.0f, 437.0f, 0.78f);

    // ---- rejillas laterales ----
    auto vent = [&] (float x)
    {
        for (int i = 0; i < 5; ++i)
        {
            const float y = 378.0f + (float) i * 15.0f;
            g.setColour (juce::Colour (0xff040405));
            g.fillRoundedRectangle (x, y, 37.0f, 7.0f, 3.5f);
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.drawHorizontalLine ((int) (y + 7.5f), x + 3.0f, x + 34.0f);
        }
    };
    vent (28.0f);
    vent (962.0f);

    // ---- cabecera: linea, logo, titulo ----
    g.setColour (juce::Colour (0xff3a3a3e));
    g.fillRect (233.0f, 35.5f, 455.0f, 1.2f);

    if (logo.isValid())
        g.drawImageWithin (logo, 62, 18, 143, 64, juce::RectanglePlacement::centred);

    {
        // el titulo se ajusta a su caja, asi se ve igual con cualquier fuente
        auto p = textPath ("BLACK MAGIC", fonts::title (100.0f), { 0.0f, 0.0f, 1400.0f, 200.0f }, juce::Justification::centred);
        const auto bb = p.getBounds();
        if (! bb.isEmpty())
        {
            const float sc = juce::jmin (242.0f / bb.getWidth(), 40.0f / bb.getHeight());
            p.applyTransform (juce::AffineTransform::translation (-bb.getCentreX(), -bb.getCentreY())
                                  .scaled (sc).translated (839.0f, 36.0f));
            drawGlowPath (g, p, colours::red, colours::redGlow, 6.0f, 1.2f);
        }
    }
    g.setFont (fonts::tracked (11.5f, 0.17f));
    g.setColour (juce::Colour (0xff77777c));
    g.drawText ("PITCH CORRECTION PROCESSOR", juce::Rectangle<float> (690.0f, 55.0f, 267.0f, 16.0f),
                juce::Justification::centredRight, false);

    // ---- panel del LCD ----
    const juce::Rectangle<float> panel (55.0f, 95.0f, 648.0f, 208.0f);
    g.setColour (juce::Colour (0xff070708));
    g.fillRoundedRectangle (panel, 14.0f);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (panel, 14.0f);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c1c1f), 0.0f, 95.0f,
                                                 juce::Colour (0xff111113), 0.0f, 172.0f, false));
        g.fillRect (55.0f, 95.0f, 648.0f, 77.0f);
    }
    g.setColour (juce::Colour (0xff2c2c30));
    g.fillRect (66.0f, 172.0f, 627.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.fillRect (66.0f, 173.2f, 627.0f, 1.0f);
    g.setColour (juce::Colour (0xff393a3f));
    g.drawRoundedRectangle (panel.reduced (1.0f), 14.0f, 2.2f);

    g.setFont (fonts::tracked (12.0f, 0.12f));
    g.setColour (colours::grey);
    g.drawText ("INPUT TYPE", juce::Rectangle<float> (75.0f, 107.0f, 200.0f, 16.0f),  juce::Justification::centredLeft, false);
    g.drawText ("KEY",        juce::Rectangle<float> (283.0f, 107.0f, 200.0f, 16.0f), juce::Justification::centredLeft, false);
    g.drawText ("SCALE",      juce::Rectangle<float> (490.0f, 107.0f, 200.0f, 16.0f), juce::Justification::centredLeft, false);
    g.drawText ("TUNING",     juce::Rectangle<float> (78.0f, 179.0f, 200.0f, 16.0f),  juce::Justification::centredLeft, false);
    g.drawText ("NOTE",       juce::Rectangle<float> (441.0f, 179.0f, 200.0f, 16.0f), juce::Justification::centredLeft, false);
    g.drawText ("FLAT",       juce::Rectangle<float> (76.0f, 260.0f, 80.0f, 16.0f),   juce::Justification::centredLeft, false);
    g.drawText ("0",          juce::Rectangle<float> (207.0f, 260.0f, 60.0f, 16.0f),  juce::Justification::centred, false);
    g.drawText ("SHARP",      juce::Rectangle<float> (317.0f, 260.0f, 80.0f, 16.0f),  juce::Justification::centredRight, false);

    g.setColour (juce::Colour (0xff5a5a5f));
    for (int i : { 8, 22 })
        g.fillRect (78.0f + (float) i * 10.4f + 3.0f, 257.5f, 1.0f, 4.0f);

    g.setColour (juce::Colour (0xff2a2a2e));
    g.fillRect (411.0f, 185.0f, 1.2f, 100.0f);

    // ---- nombres de las perillas ----
    g.setFont (fonts::tracked (17.0f, 0.05f));
    g.setColour (colours::white);
    g.drawText ("THROAT",       juce::Rectangle<float> (690.0f, 98.0f, 200.0f, 20.0f),  juce::Justification::centred, false);
    g.drawText ("TRACKING",     juce::Rectangle<float> (831.0f, 98.0f, 200.0f, 20.0f),  juce::Justification::centred, false);
    g.drawText ("RETUNE SPEED", juce::Rectangle<float> (333.0f, 341.0f, 200.0f, 20.0f), juce::Justification::centred, false);
    g.drawText ("HUMANIZE",     juce::Rectangle<float> (493.0f, 341.0f, 200.0f, 20.0f), juce::Justification::centred, false);

    g.setFont (fonts::tracked (12.5f, 0.03f));
    g.setColour (colours::grey);
    auto smallLabel = [&] (const char* t, float cx, float cy)
    {
        g.drawText (t, juce::Rectangle<float> (cx - 30.0f, cy - 8.0f, 60.0f, 16.0f), juce::Justification::centred, false);
    };
    smallLabel ("0%", 748.0f, 246.0f);  smallLabel ("100%", 833.0f, 246.0f);
    smallLabel ("0%", 888.0f, 246.0f);  smallLabel ("100%", 974.0f, 246.0f);
    smallLabel ("0",  390.0f, 492.0f);  smallLabel ("100", 476.0f, 492.0f);
    smallLabel ("0%", 550.0f, 492.0f);  smallLabel ("100%", 635.0f, 492.0f);

    // ---- pie: las lineas se acortan segun el ancho del texto para no taparlo ----
    {
        const juce::String footer ("DESIGNED BY YOUNG BLEAK");
        const auto ff = fonts::tracked (12.5f, 0.32f);
        juce::GlyphArrangement ga;
        ga.addLineOfText (ff, footer, 0.0f, 0.0f);
        const float tw = ga.getBoundingBox (0, ga.getNumGlyphs(), true).getWidth();
        const float cx = kRefW * 0.5f, cy = 527.0f, gap = 26.0f;

        g.setColour (juce::Colour (0xff2e2e32));
        g.fillRect (52.0f, cy - 0.6f, (cx - tw * 0.5f - gap) - 52.0f, 1.2f);
        g.fillRect (cx + tw * 0.5f + gap, cy - 0.6f, 974.0f - (cx + tw * 0.5f + gap), 1.2f);

        g.setFont (ff);
        g.setColour (juce::Colour (0xff68686c));
        g.drawText (footer, juce::Rectangle<float> (cx - 250.0f, cy - 9.0f, 500.0f, 18.0f), juce::Justification::centred, false);
    }
}

void BlackMagicAudioProcessorEditor::ensureBackground()
{
    const int w = juce::jmax (1, getWidth() * 2), h = juce::jmax (1, getHeight() * 2);
    if (background.isValid() && background.getWidth() == w && background.getHeight() == h)
        return;

    background = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (2.0f * scale));
    drawStatic (g);
}

void BlackMagicAudioProcessorEditor::paint (juce::Graphics& g)
{
    ensureBackground();
    g.setImageResampling (juce::Graphics::highResamplingQuality);
    g.drawImage (background, getLocalBounds().toFloat());

    // lectura de latencia (en el lugar donde estaba el boton LOW LATENCY, que se omitio)
    g.addTransform (juce::AffineTransform::scale (scale));
    g.setFont (bm::fonts::tracked (12.5f, 0.10f));
    g.setColour (bm::colours::grey);
    g.drawText ("LATENCY", juce::Rectangle<float> (770.0f, 429.0f, 90.0f, 16.0f), juce::Justification::centredRight, false);
    g.setColour (bm::colours::white);
    g.drawText (latencyText, juce::Rectangle<float> (868.0f, 429.0f, 90.0f, 16.0f), juce::Justification::centredLeft, false);
}

void BlackMagicAudioProcessorEditor::resized()
{
    scale = (float) getWidth() / kRefW;
    background = juce::Image();   // se vuelve a dibujar a la nueva escala

    lcd.setScale (scale);
    lcd.setBounds (r (bm::LcdDisplay::refX, bm::LcdDisplay::refY, bm::LcdDisplay::refW, bm::LcdDisplay::refH));

    inputField.setBounds (r (75.0f, 128.0f, 193.0f, 32.0f));
    keyField.setBounds   (r (282.0f, 128.0f, 194.0f, 32.0f));
    scaleField.setBounds (r (490.0f, 128.0f, 194.0f, 32.0f));

    throatSlider.setBounds   (r (720.0f, 120.0f, 140.0f, 140.0f));
    trackingSlider.setBounds (r (861.0f, 120.0f, 140.0f, 140.0f));
    retuneSlider.setBounds   (r (362.0f, 367.0f, 140.0f, 140.0f));
    humanizeSlider.setBounds (r (522.0f, 367.0f, 140.0f, 140.0f));

    throatToggle.setBounds (r (750.5f, 268.5f, 81.0f, 24.0f));
}
