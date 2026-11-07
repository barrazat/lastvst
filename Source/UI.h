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
    const juce::Colour violet   (0xff722db3);   // color de marca: titulo, glow, anillo
    const juce::Colour lavender (0xffeadfff);   // "encendido" claro (afinado)
    const juce::Colour grey     (0xff96969b);
    const juce::Colour greyDark (0xff68686c);
    const juce::Colour white    (0xffeef0f4);
    const juce::Colour tickLit  (0xffe4d6ff);
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
        juce::Typeface::Ptr title, bold, medium, regular;

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
                else if (lower.contains ("medium") || lower.contains ("-md") || lower.contains ("_md"))
                { if (medium == nullptr) medium = tf; }
                else if (lower.contains ("bold") || lower.contains ("-bd") || lower.contains ("heavy") || lower.contains ("black"))
                { if (bold == nullptr) bold = tf; }
                else
                { if (regular == nullptr) regular = tf; }
            }
            if (bold == nullptr)    bold = regular;
            if (regular == nullptr) regular = bold;
            if (medium == nullptr)  medium = regular;
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

    // Helvetica Medium (para los items seleccionables)
    inline juce::Font medium (float height)
    {
        auto& b = bank();
        if (b.medium != nullptr)
            return juce::Font (juce::FontOptions (b.medium).withHeight (height));
        return juce::Font (juce::FontOptions ("Helvetica Neue", height, juce::Font::plain));
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
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3a2a55));
        setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);
    }

    juce::Font getPopupMenuFont() override { return fonts::medium (16.0f); }

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
        const juce::Line<float> needle (pt (faceR * 0.22f, angle), pt (faceR * 0.88f, angle));
        if (! dim)
        {
            g.setColour (colours::tickLit.withAlpha (0.12f));
            g.drawLine (needle, 6.0f * R / 70.0f);
        }
        g.setColour (juce::Colour (dim ? 0xffb9b9bd : 0xfff4eeff));
        g.drawLine (needle, 2.5f * R / 70.0f);

    }
};

//==============================================================================
// "Pantalla" del LCD: anillo de tuning con la nota al centro.
// Todo con glow. Dibuja en coordenadas ref.
class LcdDisplay : public juce::Component
{
public:
    static constexpr float refX = 62.0f, refY = 178.0f, refW = 636.0f, refH = 118.0f;

    struct State
    {
        int   note = -1, prevNote = -1;   // nota MIDI actual / anterior (para el cruce suave)
        float blend = 1.0f;               // 0..1  cruce prevNote -> note
        float activity = 0.0f;            // 0..1  brillo general (se apaga con fade al cortar la voz)
        float cents = 0.0f;               // desviacion de la voz respecto a la nota (ya suavizada, con zona muerta)
    };

    LcdDisplay()
    {
        setInterceptsMouseClicks (false, false);
        setOpaque (false);
    }

    void setScale (float s) { scale = s; }

    void setState (const State& s)
    {
        const bool changed = s.note != st.note || s.prevNote != st.prevNote
                          || std::fabs (s.blend - st.blend) > 0.01f
                          || std::fabs (s.activity - st.activity) > 0.01f
                          || std::fabs (s.cents - st.cents) > 0.05f;
        if (changed) { st = s; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        g.addTransform (juce::AffineTransform::translation (-refX, -refY).scaled (scale));
        drawGauge (g);
    }

private:
    static constexpr int   kSeg = 25;                      // 12 a cada lado + 1 central
    static constexpr float gx = 379.0f, gy = 243.0f;       // centro del anillo (ref)
    static constexpr float gR = 52.0f, gr = 33.0f;         // radio exterior / interior
    static constexpr float kRangeCents = 30.0f;            // el anillo cubre +-30 cents

    // lavanda brillante cuando estas afinado -> violeta de marca cuando estas lejos
    static juce::Colour tuneColour (float absCents)
    {
        const float t = juce::jlimit (0.0f, 1.0f, absCents / 16.0f);
        return colours::lavender.interpolatedWith (colours::violet, t);
    }

    static void drawDash (juce::Graphics& g, float cx, float cy)
    {
        juce::Path p;
        const float hw = 17.0f, hh = 8.0f;
        p.startNewSubPath (cx - hw, cy);
        p.lineTo (cx - hw * 0.68f, cy - hh);
        p.lineTo (cx + hw * 0.68f, cy - hh);
        p.lineTo (cx + hw, cy);
        p.lineTo (cx + hw * 0.68f, cy + hh);
        p.lineTo (cx - hw * 0.68f, cy + hh);
        p.closeSubPath();
        drawGlowPath (g, p.createPathWithRoundedCorners (3.5f), juce::Colour (0xff4b4e57), juce::Colour (0xff9aa0b0), 6.0f, 1.2f);
    }

    void drawNoteGlyph (juce::Graphics& g, int note, float alpha, juce::Colour c) const
    {
        if (note < 0 || alpha < 0.02f) return;

        static const char* letters[12] = { "C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B" };
        static const bool  sharps[12]  = { false, true, false, true, false, false, true, false, true, false, true, false };
        const int pc = note % 12;
        const auto core = c.interpolatedWith (colours::lavender, 0.45f).withAlpha (alpha);   // siempre legible

        drawGlowPath (g, textPath (letters[pc], fonts::ui (46.0f), { gx - 30.0f, gy - 30.0f, 48.0f, 60.0f },
                                   juce::Justification::centred), core, c, 6.0f, 2.0f * alpha);
        if (sharps[pc])
            drawGlowPath (g, textPath ("#", fonts::ui (22.0f), { gx + 8.0f, gy - 28.0f, 18.0f, 24.0f },
                                       juce::Justification::centred), core, c, 4.0f, 1.8f * alpha);
        drawGlowPath (g, textPath (juce::String (note / 12 - 1), fonts::ui (16.0f), { gx + 8.0f, gy + 5.0f, 18.0f, 20.0f },
                                   juce::Justification::centred), core.withAlpha (alpha * 0.85f), c, 3.0f, 1.4f * alpha);
    }

    void drawGauge (juce::Graphics& g) const
    {
        const float act  = st.activity;
        const float v    = juce::jlimit (-12.0f, 12.0f, st.cents / (kRangeCents / 12.0f));
        const float mag  = std::fabs (v);
        const float start = -0.75f * juce::MathConstants<float>::pi;
        const float seg   = 1.5f * juce::MathConstants<float>::pi / (float) kSeg;
        const float gap   = 0.034f;
        const auto  tc    = tuneColour (std::fabs (st.cents));

        // disco central oscuro con halo del color actual
        const float dr = gr - 3.0f;
        g.setColour (juce::Colour (0xff040405));
        g.fillEllipse (gx - dr, gy - dr, dr * 2.0f, dr * 2.0f);
        if (act > 0.02f)
        {
            g.setGradientFill (juce::ColourGradient (tc.withAlpha (0.32f * act), gx, gy,
                                                     tc.withAlpha (0.0f), gx + dr, gy, true));
            g.fillEllipse (gx - dr, gy - dr, dr * 2.0f, dr * 2.0f);
        }

        // segmentos: se llenan desde el centro hacia el lado donde desafinas
        for (int i = 0; i < kSeg; ++i)
        {
            const int k = i - kSeg / 2;
            float lit = 0.0f;
            if (k == 0)                         lit = juce::jlimit (0.3f, 1.0f, 1.5f - mag);
            else if ((k > 0) == (v > 0.0f))     lit = juce::jlimit (0.0f, 1.0f, mag - ((float) std::abs (k) - 0.5f));
            lit *= act;

            juce::Path p;
            p.addPieSegment (gx - gR, gy - gR, gR * 2.0f, gR * 2.0f,
                             start + (float) i * seg + gap, start + (float) (i + 1) * seg - gap, gr / gR);

            if (lit > 0.03f)
            {
                const auto c = tuneColour ((float) std::abs (k) * (kRangeCents / 12.0f));
                drawGlowPath (g, p, c.withMultipliedBrightness (0.35f + 0.65f * lit), c, 5.0f, 2.0f * lit);
            }
            else
            {
                g.setColour (juce::Colour (0xff1e1f23));
                g.fillPath (p);
                g.setColour (juce::Colour (0xff2e2f34));
                g.strokePath (p, juce::PathStrokeType (0.8f));
            }
        }

        // nota al centro (cruce suave entre la anterior y la nueva)
        if (st.note < 0 && st.prevNote < 0)
        {
            drawDash (g, gx, gy);
            return;
        }
        if (st.prevNote >= 0 && st.blend < 1.0f)
            drawNoteGlyph (g, st.prevNote, (1.0f - st.blend) * act, tc);
        drawNoteGlyph (g, st.note, st.blend * act, tc);
        if (act < 0.02f) drawDash (g, gx, gy);
    }

    float scale = 1.0f;
    State st;
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
        g.setColour (juce::Colour (hover ? 0xff6a4a99 : 0xff35353b));
        g.drawRoundedRectangle (r, h * 0.26f, 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine ((int) r.getY() + 2, r.getX() + h * 0.3f, r.getRight() - h * 0.3f);

        g.setFont (fonts::medium (h * 0.52f));
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
                          juce::Colour (0xffb98cf5), colours::violet, h * 0.28f);
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
