#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#if defined(BM_HAS_TITLE_FONT) || defined(BM_HAS_UI_FONT)
 #include <BinaryData.h>
#endif

namespace bm
{
namespace colours
{
    const juce::Colour bgTop    (0xff0d0b12);
    const juce::Colour bgBottom (0xff1b1224);
    const juce::Colour accent   (0xffa66bff);
    const juce::Colour bone     (0xffece3d6);
    const juce::Colour lcdBg    (0xff0a1208);
    const juce::Colour lcdOn    (0xff9cf56b);
    const juce::Colour lcdDim   (0xff3f6b2e);
}

namespace fonts
{
    // Helvetica Neue Display Bold si pones ui_font.ttf en Resources; si no, Helvetica Neue / fuente del sistema.
    inline juce::Font ui (float height)
    {
       #if defined(BM_HAS_UI_FONT)
        static const juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (
            BinaryData::ui_font_ttf, (size_t) BinaryData::ui_font_ttfSize);
        if (tf != nullptr)
            return juce::Font (juce::FontOptions (tf).withHeight (height));
       #endif
        return juce::Font (juce::FontOptions ("Helvetica Neue", height, juce::Font::bold));
    }

    // UnifrakturCook si pones title_font.ttf en Resources.
    inline juce::Font title (float height)
    {
       #if defined(BM_HAS_TITLE_FONT)
        static const juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (
            BinaryData::title_font_ttf, (size_t) BinaryData::title_font_ttfSize);
        if (tf != nullptr)
            return juce::Font (juce::FontOptions (tf).withHeight (height));
       #endif
        return juce::Font (juce::FontOptions ("Old English Text MT", height, juce::Font::plain));
    }
}

//==============================================================================
class BMLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BMLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (0xff120e18));
        setColour (juce::PopupMenu::textColourId,                  colours::bone);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.darker (0.35f));
        setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);
        setColour (juce::Slider::textBoxTextColourId,              colours::bone);
        setColour (juce::Slider::textBoxOutlineColourId,           juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId,        juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId,         colours::accent.withAlpha (0.4f));
    }

    juce::Font getPopupMenuFont() override        { return fonts::ui (16.0f); }
    juce::Font getLabelFont (juce::Label&) override { return fonts::ui (14.0f); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto  centre = bounds.getCentre();
        const float angle  = startAngle + pos * (endAngle - startAngle);

        // pista + valor
        const float arcR = radius - 2.0f;
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff2a2133));
        g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path val;
        val.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (colours::accent);
        g.strokePath (val, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // cuerpo
        const float kr = radius * 0.70f;
        g.setColour (juce::Colour (0xff050407));
        g.fillEllipse (centre.x - kr - 2.0f, centre.y - kr + 1.0f, (kr + 2.0f) * 2.0f, (kr + 2.0f) * 2.0f);

        juce::ColourGradient grad (juce::Colour (0xff463d55), centre.x - kr * 0.5f, centre.y - kr * 0.8f,
                                   juce::Colour (0xff0e0c12), centre.x + kr * 0.6f, centre.y + kr, false);
        g.setGradientFill (grad);
        g.fillEllipse (centre.x - kr, centre.y - kr, kr * 2.0f, kr * 2.0f);
        g.setColour (juce::Colour (0xff5e4f78));
        g.drawEllipse (centre.x - kr, centre.y - kr, kr * 2.0f, kr * 2.0f, 1.2f);

        // indicador
        juce::Path pointer;
        pointer.addRoundedRectangle (-1.9f, -kr + 4.0f, 3.8f, kr * 0.42f, 1.6f);
        g.setColour (juce::Colour (0xffeadfff));
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    }
};

//==============================================================================
// Campo del LCD: al hacer click abre un menu con las opciones.
class LcdField : public juce::Component
{
public:
    explicit LcdField (const juce::String& captionText) : caption (captionText)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void()> onClick;

    void setText (const juce::String& t)
    {
        if (t != text) { text = t; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (colours::lcdOn.withAlpha (hover ? 0.16f : 0.06f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (colours::lcdOn.withAlpha (hover ? 0.75f : 0.32f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);

        g.setFont (fonts::ui (11.0f));
        g.setColour (colours::lcdDim.brighter (0.35f));
        g.drawText (caption, getLocalBounds().reduced (10, 0).withWidth (72), juce::Justification::centredLeft, false);

        g.setFont (fonts::ui (16.0f));
        g.setColour (colours::lcdOn);
        g.drawText (text, getLocalBounds().withTrimmedLeft (88).withTrimmedRight (26),
                    juce::Justification::centredLeft, true);

        const float cx = (float) getWidth() - 16.0f, cy = (float) getHeight() * 0.5f;
        juce::Path arrow;
        arrow.addTriangle (cx - 4.5f, cy - 2.5f, cx + 4.5f, cy - 2.5f, cx, cy + 3.0f);
        g.fillPath (arrow);
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && onClick) onClick();
    }

private:
    juce::String caption, text;
    bool hover = false;
};
} // namespace bm
