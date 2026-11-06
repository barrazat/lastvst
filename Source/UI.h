#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>
#include <cmath>
#include <functional>

// Todas las coordenadas "ref" son las de la imagen de referencia (1027 x 577).
namespace bm
{
namespace colours
{
    const juce::Colour red      (0xffb32d31);
    const juce::Colour redGlow  (0xffff3b3b);
    const juce::Colour grey     (0xff96969b);
    const juce::Colour greyDark (0xff68686c);
    const juce::Colour white    (0xffeef0f4);
    const juce::Colour tickLit  (0xffdcebff);
    const juce::Colour noteCore (0xfff1f5ff);
    const juce::Colour noteHalo (0xff8fb8ff);
}

//==============================================================================
// Fuentes: se buscan entre los archivos .otf/.ttf incrustados desde la carpeta Resources.
//   - nombre con "unifraktur"        -> titulo
//   - nombre con "bold"/"bd"/...     -> texto en negrita (Helvetica)
//   - cualquier otra                 -> texto normal
// Si no hay archivos, se usan fuentes del sistema.
namespace fonts
{
    struct Bank
    {
        juce::Typeface::Ptr title, bold, regular;

        Bank()
        {
            for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            {
                const char* resName = BinaryData::namedResourceList[i];
                const char* orig = BinaryData::getNamedResourceOriginalFilename (resName);
                const juce::String lower = juce::String (orig != nullptr ? orig : "").toLowerCase();
                if (! (lower.endsWith (".otf") || lower.endsWith (".ttf"))) continue;

                int size = 0;
                const char* data = BinaryData::getNamedResource (resName, size);
                if (data == nullptr || size <= 0) continue;

                auto tf = juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
                if (tf == nullptr) continue;

                if (lower.contains ("unifraktur"))
                    title = tf;
                else if (lower.contains ("bold") || lower.contains ("-bd") || lower.contains ("heavy") || lower.contains ("black"))
                { if (bold == nullptr) bold = tf; }
                else
                { if (regular == nullptr) regular = tf; }
            }
            if (bold == nullptr)    bold = regular;
            if (regular == nullptr) regular = bold;
        }
    };

    inline Bank& bank() { static Bank b; return b; }

    inline juce::Font ui (float height, bool bold = true)
    {
        auto& b = bank();
        auto tf = bold ? b.bold : b.regular;
        if (tf != nullptr)
            return juce::Font (juce::FontOptions (tf).withHeight (height));
        return juce::Font (juce::FontOptions ("Helvetica Neue", height, bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font title (float height)
    {
        auto& b = bank();
        if (b.title != nullptr)
            return juce::Font (juce::FontOptions (b.title).withHeight (height));
        return juce::Font (juce::FontOptions ("Old English Text MT", height, juce::Font::plain));
    }

    inline juce::Font tracked (float height, float kerning, bool bold = true)
    {
        return ui (height, bold).withExtraKerningFactor (kerning);
    }
}

//==============================================================================
// Glow: varias pasadas de trazo ancho y transparente alrededor de la forma.
inline void drawGlowPath (juce::Graphics& g, const juce::Path& p, juce::Colour core, juce::Colour halo,
                          float radius, float strength = 1.0f)
{
    const int layers = 7;
    for (int i = layers; i >= 1; --i)
    {
        const float k = (float) i / (float) layers;
        g.setColour (halo.withAlpha (juce::jlimit (0.0f, 1.0f, strength * 0.065f * (1.15f - 0.55f * k))));
        g.strokePath (p, juce::PathStrokeType (radius * 2.0f * k + 1.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }
    g.setColour (core);
    g.fillPath (p);
}

inline void drawGlowRect (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour core, juce::Colour halo,
                          float radius, float strength = 1.0f)
{
    const int layers = 6;
    for (int i = layers; i >= 1; --i)
    {
        const float k = (float) i / (float) layers;
        g.setColour (halo.withAlpha (juce::jlimit (0.0f, 1.0f, strength * 0.07f * (1.2f - 0.6f * k))));
        g.fillRoundedRectangle (r.expanded (radius * k), 2.0f + radius * k * 0.6f);
    }
    g.setColour (core);
    g.fillRoundedRectangle (r, 1.8f);
}

inline juce::Path textPath (const juce::String& text, const juce::Font& font, juce::Rectangle<float> area,
                            juce::Justification just)
{
    juce::GlyphArrangement ga;
    ga.addFittedText (font, text, area.getX(), area.getY(), area.getWidth(), area.getHeight(), just, 1);
    juce::Path p;
    ga.createPath (p);
    return p;
}

//==============================================================================
class BMLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BMLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (0xff131315));
        setColour (juce::PopupMenu::textColourId,                  colours::white);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3a3a40));
        setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);
    }

    juce::Font getPopupMenuFont() override { return fonts::ui (16.0f); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto  b = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const auto  c = b.getCentre();
        const float R = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const bool  dim = (bool) slider.getProperties()["dim"];

        auto pt = [&] (float radius, float angle)
        {
            return juce::Point<float> (c.x + radius * std::sin (angle), c.y - radius * std::cos (angle));
        };

        // marcas alrededor: las encendidas llegan hasta el valor actual
        const int nT = 21;
        const int litUpTo = dim ? -1 : (int) std::lround (pos * (float) (nT - 1));
        for (int i = 0; i < nT; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * (float) i / (float) (nT - 1);
            const bool  major = (i % 5 == 0);
            const auto  p0 = pt (R * 0.78f, a);
            const auto  p1 = pt (R * (major ? 0.96f : 0.89f), a);
            const juce::Line<float> line (p0, p1);

            if (i <= litUpTo)
            {
                g.setColour (colours::tickLit.withAlpha (0.10f));
                g.drawLine (line, 7.0f * R / 70.0f);
                g.setColour (colours::tickLit.withAlpha (0.22f));
                g.drawLine (line, 4.4f * R / 70.0f);
                g.setColour (colours::tickLit);
                g.drawLine (line, 2.4f * R / 70.0f);
            }
            else
            {
                g.setColour (juce::Colour (0xff6a6a70).withAlpha (major ? 0.95f : 0.75f));
                g.drawLine (line, 1.6f * R / 70.0f);
            }
        }

        // aro exterior
        const float bodyR = R * 0.64f;
        g.setColour (juce::Colour (0xff050506));
        g.fillEllipse (c.x - bodyR - 3.0f, c.y - bodyR - 1.0f, (bodyR + 3.0f) * 2.0f, (bodyR + 3.0f) * 2.0f);

        juce::ColourGradient ring (juce::Colour (0xff4a4b50), c.x, c.y - bodyR,
                                   juce::Colour (0xff141416), c.x, c.y + bodyR, false);
        g.setGradientFill (ring);
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

        // cara con efecto cepillado
        const float faceR = bodyR * 0.88f;
        g.setColour (juce::Colour (0xff1d1e21));
        g.fillEllipse (c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f);
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addEllipse (c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f);
            g.reduceClipRegion (clip);
            const int N = 180;
            for (int k = 0; k < N; ++k)
            {
                const float a  = juce::MathConstants<float>::twoPi * (float) k / (float) N;
                const float br = 0.5f + 0.5f * std::cos (2.0f * (a - 0.8f));
                g.setColour (juce::Colours::white.withAlpha (0.010f + 0.055f * br));
                g.drawLine (c.x, c.y, c.x + faceR * 1.1f * std::sin (a), c.y - faceR * 1.1f * std::cos (a), 1.4f);
            }
        }

        // indicador
        const float angle = startAngle + pos * (endAngle - startAngle);
        const juce::Line<float> needle (pt (faceR * 0.12f, angle), pt (faceR * 0.88f, angle));
        if (! dim)
        {
            g.setColour (colours::tickLit.withAlpha (0.12f));
            g.drawLine (needle, 6.0f * R / 70.0f);
        }
        g.setColour (juce::Colour (dim ? 0xffb9b9bd : 0xfff6f8ff));
        g.drawLine (needle, 2.5f * R / 70.0f);

        g.setColour (juce::Colour (0xff0c0c0e));
        g.fillEllipse (c.x - 3.6f * R / 70.0f, c.y - 3.6f * R / 70.0f, 7.2f * R / 70.0f, 7.2f * R / 70.0f);
    }
};

//==============================================================================
// Medidor de afinacion + nota (el "LCD"). Dibuja en coordenadas ref con glow.
class LcdDisplay : public juce::Component
{
public:
    static constexpr float refX = 62.0f, refY = 178.0f, refW = 636.0f, refH = 118.0f;

    LcdDisplay()
    {
        setInterceptsMouseClicks (false, false);
        setOpaque (false);
    }

    void setScale (float s) { scale = s; }

    // note: MIDI o -1; meter: -1 (plano) .. +1 (sostenido)
    void setState (int newNote, float newMeter)
    {
        const bool changed = (newNote != note) || std::fabs (newMeter - meter) > 0.004f;
        note = newNote;
        meter = newMeter;
        if (changed) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.addTransform (juce::AffineTransform::translation (-refX, -refY).scaled (scale));
        drawMeter (g);
        drawNote (g);
    }

private:
    static constexpr int kSeg = 31;

    static juce::Colour segColour (int i)
    {
        const int d = std::abs (i - kSeg / 2);
        if (d <= 3)  return juce::Colour (0xff3df06a);
        if (d <= 6)  return juce::Colour (0xff9be03c);
        if (d <= 9)  return juce::Colour (0xffe6c43a);
        if (d <= 12) return juce::Colour (0xfff0862b);
        return juce::Colour (0xfff0393a);
    }

    void drawMeter (juce::Graphics& g) const
    {
        const float x0 = 78.0f, pitch = 10.4f, w = 7.0f, y0 = 198.0f, h = 60.0f;
        const float pos = (float) (kSeg / 2) + juce::jlimit (-1.0f, 1.0f, meter) * (float) (kSeg / 2);

        for (int i = 0; i < kSeg; ++i)
        {
            const juce::Rectangle<float> r (x0 + (float) i * pitch, y0, w, h);
            const auto col = segColour (i);

            float lit = (note >= 0) ? juce::jlimit (0.0f, 1.0f, 1.0f - std::fabs ((float) i - pos) * 0.62f) : 0.0f;
            if (i == kSeg / 2) lit = juce::jmax (lit, 0.10f);

            if (lit > 0.02f)
            {
                drawGlowRect (g, r, col.brighter (0.15f).withAlpha (0.30f + 0.70f * lit), col, 5.0f, lit);
            }
            else
            {
                g.setColour (col.withAlpha (0.15f));
                g.fillRoundedRectangle (r, 1.8f);
            }
        }
    }

    static void drawDash (juce::Graphics& g, float cx, float cy)
    {
        auto hex = [] (float cx_, float cy_, float hw, float hh)
        {
            juce::Path p;
            p.startNewSubPath (cx_ - hw, cy_);
            p.lineTo (cx_ - hw * 0.68f, cy_ - hh);
            p.lineTo (cx_ + hw * 0.68f, cy_ - hh);
            p.lineTo (cx_ + hw, cy_);
            p.lineTo (cx_ + hw * 0.68f, cy_ + hh);
            p.lineTo (cx_ - hw * 0.68f, cy_ + hh);
            p.closeSubPath();
            return p.createPathWithRoundedCorners (4.0f);
        };
        drawGlowPath (g, hex (cx, cy, 23.0f, 12.0f), juce::Colour (0xff5c5f68), juce::Colour (0xff9aa0b0), 7.0f, 0.9f);
        drawGlowPath (g, hex (cx, cy, 13.0f, 4.6f), juce::Colour (0xffc9ccd4), juce::Colour (0xffffffff), 4.0f, 0.8f);
    }

    void drawNote (juce::Graphics& g) const
    {
        if (note < 0)
        {
            drawDash (g, 528.0f, 240.0f);
            drawDash (g, 586.0f, 240.0f);
            return;
        }

        static const char* letters[12] = { "C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B" };
        static const bool  sharps[12]  = { false, true, false, true, false, false, true, false, true, false, true, false };
        const int pc = note % 12;

        const auto big = fonts::ui (80.0f);
        drawGlowPath (g, textPath (letters[pc], big, { 486.0f, 196.0f, 64.0f, 88.0f }, juce::Justification::centred),
                      colours::noteCore, colours::noteHalo, 8.0f);
        if (sharps[pc])
            drawGlowPath (g, textPath ("#", fonts::ui (36.0f), { 546.0f, 196.0f, 24.0f, 40.0f }, juce::Justification::centred),
                          colours::noteCore, colours::noteHalo, 6.0f);
        drawGlowPath (g, textPath (juce::String (note / 12 - 1), big, { 566.0f, 196.0f, 56.0f, 88.0f }, juce::Justification::centred),
                      colours::noteCore, colours::noteHalo, 8.0f);
    }

    float scale = 1.0f;
    int   note  = -1;
    float meter = 0.0f;
};

//==============================================================================
// Campo desplegable (INPUT TYPE / KEY / SCALE)
class DropField : public juce::Component
{
public:
    std::function<void()> onClick;

    DropField() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void setText (const juce::String& t)
    {
        if (t != text) { text = t; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        const auto  r = getLocalBounds().toFloat().reduced (1.0f);
        const float h = r.getHeight();

        juce::ColourGradient grad (juce::Colour (hover ? 0xff26262a : 0xff1f1f22), 0.0f, r.getY(),
                                   juce::Colour (hover ? 0xff17171a : 0xff121214), 0.0f, r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, h * 0.26f);
        g.setColour (juce::Colour (hover ? 0xff4a4a52 : 0xff35353b));
        g.drawRoundedRectangle (r, h * 0.26f, 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine ((int) r.getY() + 2, r.getX() + h * 0.3f, r.getRight() - h * 0.3f);

        g.setFont (fonts::ui (h * 0.52f));
        g.setColour (colours::white);
        g.drawText (text, getLocalBounds().withTrimmedLeft ((int) (h * 0.48f)).withTrimmedRight ((int) (h * 0.9f)),
                    juce::Justification::centredLeft, true);

        const float cx = r.getRight() - h * 0.55f, cy = r.getCentreY();
        juce::Path chevron;
        chevron.startNewSubPath (cx - h * 0.17f, cy - h * 0.06f);
        chevron.lineTo (cx, cy + h * 0.11f);
        chevron.lineTo (cx + h * 0.17f, cy - h * 0.06f);
        g.setColour (colours::white);
        g.strokePath (chevron, juce::PathStrokeType (h * 0.07f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && onClick) onClick();
    }

private:
    juce::String text;
    bool hover = false;
};

//==============================================================================
// Boton tipo pastilla con LED (Throat ON/OFF)
class PillToggle : public juce::Button
{
public:
    PillToggle() : juce::Button ("throatOn")
    {
        setClickingTogglesState (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool hover, bool) override
    {
        const auto  r = getLocalBounds().toFloat().reduced (1.0f);
        const float h = r.getHeight();
        const bool  on = getToggleState();

        juce::ColourGradient grad (juce::Colour (hover ? 0xff29292d : 0xff222225), 0.0f, r.getY(),
                                   juce::Colour (0xff141416), 0.0f, r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, h * 0.5f);
        g.setColour (juce::Colour (0xff3a3a40));
        g.drawRoundedRectangle (r, h * 0.5f, 1.2f);

        const float lx = r.getX() + h * 0.62f, ly = r.getCentreY(), lr = h * 0.17f;
        if (on)
        {
            drawGlowRect (g, juce::Rectangle<float> (lx - lr, ly - lr, lr * 2.0f, lr * 2.0f),
                          juce::Colour (0xffff5a5a), colours::redGlow, h * 0.28f);
        }
        else
        {
            g.setColour (juce::Colour (0xff0a0a0b));
            g.fillEllipse (lx - lr, ly - lr, lr * 2.0f, lr * 2.0f);
        }

        g.setFont (fonts::ui (h * 0.56f));
        g.setColour (on ? colours::white : colours::grey);
        g.drawText (on ? "ON" : "OFF", getLocalBounds().withTrimmedLeft ((int) (h * 0.9f)),
                    juce::Justification::centred, false);
    }
};
} // namespace bm
