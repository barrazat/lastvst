#include "PluginEditor.h"
#include "DSP/Scales.h"

#if BLACKMAGIC_HAS_LOGO || BLACKMAGIC_HAS_FONT || BLACKMAGIC_HAS_TITLE_FONT
 #include <BinaryData.h>
#endif

using namespace juce;

namespace
{
    // Todo el diseno esta en un espacio de 1024 x 576; se escala al tamano de la ventana
    constexpr float DW = 1024.0f, DH = 576.0f;

    struct KnobPos { float cx, cy, R; };
    constexpr KnobPos kThroat   { 790.0f, 192.0f, 62.0f };
    constexpr KnobPos kTracking { 930.0f, 192.0f, 62.0f };
    constexpr KnobPos kRetune   { 432.0f, 438.0f, 60.0f };
    constexpr KnobPos kHumanize { 592.0f, 438.0f, 60.0f };

    // Botones tipo pastilla (el componente es 10 px mas grande que la pastilla para dejar sitio al resplandor)
    constexpr float kPad = 10.0f;
    const Rectangle<float> kThroatPill { kThroat.cx - 41.0f, 268.0f, 82.0f, 26.0f };
    const Rectangle<float> kLowLatPill { 785.0f, 422.0f, 150.0f, 32.0f };

    // Pantalla LCD y sus campos
    const Rectangle<float> kLcd     { 56.0f, 96.0f, 644.0f, 204.0f };
    const Rectangle<float> kFieldIn { 72.0f, 128.0f, 196.0f, 32.0f };
    const Rectangle<float> kFieldKey{ 280.0f, 128.0f, 196.0f, 32.0f };
    const Rectangle<float> kFieldSc { 488.0f, 128.0f, 196.0f, 32.0f };
    const Rectangle<float> kLcdLower{ 56.0f, 166.0f, 644.0f, 134.0f };

    // Medidor de afinacion centrado: 31 segmentos (-15..0..+15), cada lado cubre +-50 cents
    constexpr int   kMeterSegs = 15;                       // por lado
    constexpr float kMeterX = 76.0f, kMeterPitch = 10.45f, kMeterY = 198.0f, kMeterH = 58.0f, kMeterW = 7.0f;
    Rectangle<float> meterSeg (int i) { return { kMeterX + (float) (i + kMeterSegs) * kMeterPitch, kMeterY, kMeterW, kMeterH }; }
    Colour meterColour (int i)
    {
        const int a = std::abs (i);
        return a <= 3 ? Colour (0xff35d94a) : a <= 7 ? Colour (0xffd6e03a) : a <= 11 ? Colour (0xffe8963a) : Colour (0xffe0452f);
    }
    // segmentos encendidos: del centro hacia el lado de la desviacion (sostenido = derecha, bemol = izquierda)
    bool meterLit (int i, float cents, bool have)
    {
        if (! have) return false;
        if (i == 0) return true;
        const int n = roundToInt (std::abs (cents) / (50.0f / (float) kMeterSegs));
        return (cents > 0.0f) ? (i > 0 && i <= n) : (i < 0 && -i <= n);
    }

    const Colour cTitleRed (0xffa32a2d);
    const Colour cLabel    (0xffdcdde0);
    const Colour cDim      (0xff8e9197);
    const Colour cLcdOn    (0xfff2f6ff);

    // ---- Tipografia: Helvetica Neue Display Bold (o la mas parecida instalada) ----
    // Las tipografias incrustadas se liberan al cerrar la ultima ventana (evita objetos estaticos al descargar el plugin)
    Typeface::Ptr gEmbeddedTypeface, gTitleTypeface;
    int gEditorCount = 0;

    String resolveSystemFont()
    {
        static const String name = []
        {
            const StringArray all = Font::findAllTypefaceNames();
            auto firstWith = [&all] (StringRef a, StringRef b = StringRef())
            {
                for (auto& n : all)
                    if (n.containsIgnoreCase (a) && (b.isEmpty() || n.containsIgnoreCase (b))) return n;
                return String();
            };
            for (auto cand : { firstWith ("helvetica neue", "display"), firstWith ("helveticaneue", "display"),
                               firstWith ("helvetica neue"), firstWith ("helveticaneue"),
                               firstWith ("helvetica"), firstWith ("arial") })
                if (cand.isNotEmpty()) return cand;
            return Font::getDefaultSansSerifFontName();
        }();
        return name;
    }

    Font ui (float h, bool bold = true, float kerning = 0.0f)
    {
        Font f = gEmbeddedTypeface != nullptr ? Font (FontOptions (gEmbeddedTypeface).withHeight (h))
                                              : Font (FontOptions (resolveSystemFont(), h, bold ? Font::bold : Font::plain));
        if (kerning > 0.0f) f.setExtraKerningFactor (kerning);
        return f;
    }

    // Tipografia gotica del titulo (UnifrakturCook Bold, licencia OFL)
    Font titleFont (float h)
    {
        if (gTitleTypeface != nullptr) return Font (FontOptions (gTitleTypeface).withHeight (h));
        return Font (FontOptions ("Georgia", h, Font::bold));
    }

    // Dibuja texto como contorno con su borde derecho EXACTO en rightX (opcionalmente con resplandor).
    // Devuelve la x del borde izquierdo.
    float drawTextRightAligned (Graphics& g, const String& s, const Font& f, float rightX, float baselineY,
                                Colour c, float glowStrength)
    {
        GlyphArrangement ga;
        ga.addLineOfText (f, s, 0.0f, baselineY);
        const auto bb = ga.getBoundingBox (0, -1, true);
        const float dx = rightX - bb.getRight();
        Path p; ga.createPath (p);
        p.applyTransform (AffineTransform::translation (dx, 0.0f));
        if (glowStrength > 0.0f)
        {
            DropShadow (c.withAlpha (0.45f * glowStrength), 16, { 0, 0 }).drawForPath (g, p);
            DropShadow (c.withAlpha (0.85f * glowStrength), 6,  { 0, 0 }).drawForPath (g, p);
        }
        g.setColour (c);
        g.fillPath (p);
        return bb.getX() + dx;
    }

    void txt (Graphics& g, const String& s, Rectangle<float> r, Font f, Colour c, Justification j = Justification::centred)
    {
        g.setFont (f);
        g.setColour (c);
        g.drawText (s, r, j, false);
    }

    Point<float> dirOf (float a) { return { std::sin (a), -std::cos (a) }; }

    void drawScrew (Graphics& g, Point<float> c, float rot)
    {
        const float r = 11.0f;
        Path disc; disc.addEllipse (c.x - r, c.y - r, r * 2, r * 2);
        DropShadow (Colours::black.withAlpha (0.8f), 6, { 0, 2 }).drawForPath (g, disc);
        g.setGradientFill (ColourGradient (Colour (0xff5d5f64), c.x - r * 0.5f, c.y - r * 0.7f,
                                           Colour (0xff141517), c.x + r * 0.8f, c.y + r * 0.9f, false));
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (Colours::black.withAlpha (0.85f));
        g.drawEllipse (c.x - r, c.y - r, r * 2, r * 2, 1.2f);
        g.setColour (Colour (0xff0b0b0c));
        for (int k = 0; k < 2; ++k)
        {
            Path slot; slot.addRoundedRectangle (-r * 0.66f, -r * 0.13f, r * 1.32f, r * 0.26f, r * 0.1f);
            g.fillPath (slot, AffineTransform::rotation (rot + (float) k * MathConstants<float>::halfPi).translated (c.x, c.y));
        }
    }

    void drawVent (Graphics& g, float x, float y)
    {
        Rectangle<float> r (x, y, 38.0f, 8.0f);
        g.setColour (Colour (0xff303236));
        g.fillRoundedRectangle (r.translated (0.0f, 1.0f), 4.0f);
        g.setColour (Colour (0xff040405));
        g.fillRoundedRectangle (r, 4.0f);
    }

    // ---- Siete segmentos ----
    int letterMask (char ch)
    {
        switch (ch)
        {
            case 'A': return 119; case 'B': return 124; case 'C': return 57; case 'D': return 94;
            case 'E': return 121; case 'F': return 113; case 'G': return 61; case '-': return 64;
            default:  return 0;
        }
    }

    void drawSegChar (Graphics& g, int mask, Rectangle<float> r, Colour on, bool showGhost)
    {
        const float t = r.getHeight() * 0.13f, gap = t * 0.2f;
        const float l = r.getX() + t * 0.5f, rt = r.getRight() - t * 0.5f;
        const float tp = r.getY() + t * 0.5f, bt = r.getBottom() - t * 0.5f, md = r.getCentreY();

        auto hseg = [&] (float y)
        {
            const float xa = l + t * 0.5f + gap, xb = rt - t * 0.5f - gap;
            Path p; p.startNewSubPath (xa - t * 0.5f, y); p.lineTo (xa, y - t * 0.5f); p.lineTo (xb, y - t * 0.5f);
            p.lineTo (xb + t * 0.5f, y); p.lineTo (xb, y + t * 0.5f); p.lineTo (xa, y + t * 0.5f); p.closeSubPath();
            return p;
        };
        auto vseg = [&] (float x, float y0, float y1)
        {
            const float ya = y0 + t * 0.5f + gap, yb = y1 - t * 0.5f - gap;
            Path p; p.startNewSubPath (x, ya - t * 0.5f); p.lineTo (x + t * 0.5f, ya); p.lineTo (x + t * 0.5f, yb);
            p.lineTo (x, yb + t * 0.5f); p.lineTo (x - t * 0.5f, yb); p.lineTo (x - t * 0.5f, ya); p.closeSubPath();
            return p;
        };

        const Path segs[7] = { hseg (tp), vseg (rt, tp, md), vseg (rt, md, bt), hseg (bt),
                               vseg (l, md, bt), vseg (l, tp, md), hseg (md) };
        for (int i = 0; i < 7; ++i)
        {
            if (mask & (1 << i))
            {
                g.setColour (on.withAlpha (0.16f));
                g.strokePath (segs[i], PathStrokeType (t * 1.7f, PathStrokeType::curved, PathStrokeType::rounded));
                g.setColour (on);
                g.fillPath (segs[i]);
            }
            else if (showGhost)
            {
                g.setColour (on.withAlpha (0.06f));
                g.fillPath (segs[i]);
            }
        }
    }

    void drawHash (Graphics& g, Rectangle<float> r, Colour on)
    {
        const float t = r.getHeight() * 0.11f;
        Path p;
        const float x1 = r.getX() + r.getWidth() * 0.34f, x2 = r.getX() + r.getWidth() * 0.70f, sk = r.getWidth() * 0.10f;
        p.startNewSubPath (x1 + sk, r.getY());      p.lineTo (x1 - sk, r.getBottom());
        p.startNewSubPath (x2 + sk, r.getY());      p.lineTo (x2 - sk, r.getBottom());
        const float y1 = r.getY() + r.getHeight() * 0.34f, y2 = r.getY() + r.getHeight() * 0.68f;
        p.startNewSubPath (r.getX(), y1);           p.lineTo (r.getRight(), y1);
        p.startNewSubPath (r.getX(), y2);           p.lineTo (r.getRight(), y2);
        g.setColour (on.withAlpha (0.16f));
        g.strokePath (p, PathStrokeType (t * 1.9f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (on);
        g.strokePath (p, PathStrokeType (t, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // Nombre de una nota (p.ej. "C#") con digitos de 7 segmentos; midiNote < 0 -> "--"
    void drawNoteReadout (Graphics& g, int midiNote, float x, float y, float h)
    {
        const float cw = h * 0.56f, gap = h * 0.2f;
        if (midiNote < 0)
        {
            const Colour dim = cLcdOn.withAlpha (0.22f);
            drawSegChar (g, 64, { x, y, cw, h }, dim, false);
            drawSegChar (g, 64, { x + cw + gap, y, cw, h }, dim, false);
            return;
        }
        const String name = String (blackmagic::kKeyNames[(midiNote % 12 + 12) % 12]);
        drawSegChar (g, letterMask (name[0]), { x, y, cw, h }, cLcdOn, true);
        if (name.length() > 1)
            drawHash (g, { x + cw + gap, y + h * 0.02f, cw * 0.78f, h * 0.62f }, cLcdOn);
    }
}

//==============================================================================
BlackMagicLookAndFeel::BlackMagicLookAndFeel()
{
    setColour (ComboBox::textColourId,       Colour (0xfff4f5f7));
    setColour (PopupMenu::backgroundColourId, Colour (0xff111214));
    setColour (PopupMenu::textColourId,       Colour (0xffdcdde0));
    setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff8e2226));
    setColour (PopupMenu::highlightedTextColourId,       Colours::white);
    setColour (Label::textColourId,           Colours::white);
}

void BlackMagicLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                              float startAngle, float endAngle, Slider& slider)
{
    const auto  bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto  c = bounds.getCentre();
    const float R = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float r = R * 0.75f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    const bool dim = (bool) slider.getProperties().getWithDefault ("dim", false);

    // Marcas alrededor
    const int ticks = (int) slider.getProperties().getWithDefault ("ticks", 21);
    for (int i = 0; i < ticks; ++i)
    {
        const float tp = ticks > 1 ? (float) i / (float) (ticks - 1) : 0.5f;
        const float a  = startAngle + tp * (endAngle - startAngle);
        const bool major = (i % 5 == 0);
        const bool lit   = (tp <= pos + 0.001f);
        const auto d = dirOf (a);
        const float r0 = R * 0.86f, r1 = R * (major ? 1.0f : 0.95f);
        g.setColour (lit ? (dim ? Colour (0xff4d5056) : Colour (0xffe6e8eb)) : Colour (0xff62656b).withMultipliedBrightness (dim ? 0.7f : 1.0f));
        g.drawLine (c.x + d.x * r0, c.y + d.y * r0, c.x + d.x * r1, c.y + d.y * r1, major ? 1.7f : 1.1f);
    }

    // Cuerpo de la perilla
    Path disc; disc.addEllipse (c.x - r, c.y - r, r * 2, r * 2);
    DropShadow (Colours::black.withAlpha (0.85f), (int) (R * 0.30f), { 0, (int) (R * 0.10f) }).drawForPath (g, disc);

    g.setGradientFill (ColourGradient (Colour (0xff55575c), c.x - r * 0.5f, c.y - r * 0.8f,
                                       Colour (0xff09090a), c.x + r * 0.6f, c.y + r * 0.9f, false));
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);

    const float rf = r * 0.88f;
    g.setGradientFill (ColourGradient (Colour (0xff3d3f44), c.x - r * 0.25f, c.y - r * 0.3f,
                                       Colour (0xff0d0e10), c.x + r * 0.75f, c.y + r * 0.75f, true));
    g.fillEllipse (c.x - rf, c.y - rf, rf * 2, rf * 2);

    // Textura de metal cepillado (reflejos tipo "conico")
    for (int i = 0; i < 96; ++i)
    {
        const float a = (float) i * MathConstants<float>::twoPi / 96.0f;
        const float s = std::abs (std::cos (a - 0.75f));
        g.setColour (Colours::white.withAlpha (0.02f + 0.17f * std::pow (s, 12.0f)));
        g.drawLine (c.x + std::cos (a) * rf * 0.10f, c.y + std::sin (a) * rf * 0.10f,
                    c.x + std::cos (a) * rf * 0.97f, c.y + std::sin (a) * rf * 0.97f, 0.8f);
    }

    g.setColour (Colours::black.withAlpha (0.85f));
    g.drawEllipse (c.x - rf, c.y - rf, rf * 2, rf * 2, 1.6f);
    g.setColour (Colours::white.withAlpha (0.10f));
    g.drawEllipse (c.x - r, c.y - r, r * 2, r * 2, 1.0f);

    if (dim)   // apagado: la perilla se ve inactiva
    {
        g.setColour (Colours::black.withAlpha (0.42f));
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    }

    // Indicador
    const auto d = dirOf (angle);
    Path pointer;
    pointer.startNewSubPath (c.x + d.x * r * 0.22f, c.y + d.y * r * 0.22f);
    pointer.lineTo          (c.x + d.x * r * 0.84f, c.y + d.y * r * 0.84f);
    g.setColour (dim ? Colour (0xff80838a) : Colour (0xfff4f5f7));
    g.strokePath (pointer, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));

    // Resplandor (bloom) del indicador y de las marcas encendidas
    if (! dim)
    {
        Image em (Image::ARGB, jmax (1, w), jmax (1, h), true);
        {
            Graphics eg (em);
            eg.addTransform (AffineTransform::translation ((float) -x, (float) -y));
            eg.setColour (Colour (0xffd2e6ff));
            for (int i = 0; i < ticks; ++i)
            {
                const float tp = ticks > 1 ? (float) i / (float) (ticks - 1) : 0.5f;
                if (tp > pos + 0.001f) break;
                const auto dd = dirOf (startAngle + tp * (endAngle - startAngle));
                eg.drawLine (c.x + dd.x * R * 0.86f, c.y + dd.y * R * 0.86f, c.x + dd.x * R, c.y + dd.y * R, 2.0f);
            }
            eg.strokePath (pointer, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        Image blurred;
        ImageEffects::applyGaussianBlurEffect (R * 0.085f, em, blurred);
        g.setOpacity (0.95f);
        g.drawImageAt (blurred, x, y);
        g.setOpacity (1.0f);
    }
}

void BlackMagicLookAndFeel::drawComboBox (Graphics& g, int width, int height, bool isButtonDown,
                                          int, int, int, int, ComboBox& box)
{
    const auto r = Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f);
    const float cr = 6.0f * uiScale;
    const bool active = isButtonDown || box.isPopupActive();

    if (active)
    {
        Path rp; rp.addRoundedRectangle (r, cr);
        DropShadow (Colour (0xffd0383c).withAlpha (0.75f), (int) (8.0f * uiScale), { 0, 0 }).drawForPath (g, rp);
    }
    g.setGradientFill (ColourGradient (Colour (0xff1b1c1f), 0, r.getY(), Colour (0xff0b0b0d), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, cr);
    g.setColour (active ? Colour (0xffd0383c) : Colour (0xff3d3f45));
    g.drawRoundedRectangle (r, cr, 1.5f);
    g.setColour (Colours::white.withAlpha (0.05f));
    g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.45f).reduced (2.0f, 1.0f), cr);

    // Flecha hacia abajo
    const float ax = (float) width - 20.0f * uiScale, ay = (float) height * 0.5f, s = 5.0f * uiScale;
    Path chev;
    chev.startNewSubPath (ax - s, ay - s * 0.45f);
    chev.lineTo (ax, ay + s * 0.55f);
    chev.lineTo (ax + s, ay - s * 0.45f);
    g.setColour (active ? Colour (0xfff4f5f7) : Colour (0xffb7bcc4));
    g.strokePath (chev, PathStrokeType (2.0f * uiScale, PathStrokeType::curved, PathStrokeType::rounded));
}

void BlackMagicLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced (kPad * uiScale);
    const bool on = b.getToggleState();
    const float cr = r.getHeight() * 0.5f;
    const Colour red (0xffd0383c);

    if (on)
    {
        Path rp; rp.addRoundedRectangle (r, cr);
        DropShadow (red.withAlpha (0.55f), (int) (7.0f * uiScale), { 0, 0 }).drawForPath (g, rp);
    }
    g.setGradientFill (ColourGradient (Colour (0xff1d1e21), 0, r.getY(), Colour (0xff0b0b0d), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, cr);
    g.setColour (on ? red : Colour (0xff3d3f45));
    g.drawRoundedRectangle (r.reduced (0.75f), cr, 1.5f);
    g.setColour (Colours::white.withAlpha (0.05f));
    g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.45f).reduced (3.0f, 1.0f), cr * 0.8f);

    // LED
    const float lr = r.getHeight() * 0.17f;
    const Point<float> lc (r.getX() + r.getHeight() * 0.58f, r.getCentreY());
    Path led; led.addEllipse (lc.x - lr, lc.y - lr, lr * 2, lr * 2);
    if (on)
    {
        DropShadow (Colour (0xffff4a40).withAlpha (0.95f), (int) (6.0f * uiScale), { 0, 0 }).drawForPath (g, led);
        g.setColour (Colour (0xffff6b5e));  g.fillPath (led);
        g.setColour (Colours::white.withAlpha (0.85f));
        g.fillEllipse (lc.x - lr * 0.45f, lc.y - lr * 0.45f, lr * 0.9f, lr * 0.9f);
    }
    else
    {
        g.setColour (Colour (0xff2c2e32));  g.fillPath (led);
        g.setColour (Colour (0xff0a0a0b));  g.strokePath (led, PathStrokeType (1.0f));
    }

    const String label = on ? b.getProperties().getWithDefault ("textOn", b.getButtonText()).toString()
                            : b.getProperties().getWithDefault ("textOff", b.getButtonText()).toString();
    g.setFont (ui (12.5f * uiScale, true, 0.08f));
    g.setColour (on ? Colour (0xfff4f5f7) : Colour (0xff80838a));
    g.drawText (label, Rectangle<float> (lc.x + lr * 1.8f, r.getY(), r.getRight() - lc.x - lr * 1.8f - 4.0f * uiScale, r.getHeight()),
                Justification::centred, false);
}

Font BlackMagicLookAndFeel::getComboBoxFont (ComboBox&)
{
    return ui (16.0f * uiScale, true, 0.02f);
}

void BlackMagicLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (roundToInt (12.0f * uiScale), 1, box.getWidth() - roundToInt (40.0f * uiScale), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

Font BlackMagicLookAndFeel::getPopupMenuFont()
{
    return ui (16.0f * jmax (0.85f, uiScale), true, 0.02f);
}

void BlackMagicLookAndFeel::drawPopupMenuBackground (Graphics& g, int width, int height)
{
    g.fillAll (Colour (0xff111214));
    g.setColour (Colour (0xff4a4d53));
    g.drawRect (0, 0, width, height, 1);
}

//==============================================================================
BlackMagicAudioProcessorEditor::BlackMagicAudioProcessorEditor (BlackMagicAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    ++gEditorCount;
    setLookAndFeel (&laf);

#if BLACKMAGIC_HAS_LOGO
    dungeonLogo = ImageCache::getFromMemory (BinaryData::dungeon_logo_png, BinaryData::dungeon_logo_pngSize);
#endif
#if BLACKMAGIC_HAS_TITLE_FONT
    if (gTitleTypeface == nullptr)
        gTitleTypeface = Typeface::createSystemTypefaceFor (BinaryData::title_font_ttf, (size_t) BinaryData::title_font_ttfSize);
#endif
#if BLACKMAGIC_HAS_FONT
    if (gEmbeddedTypeface == nullptr)
        gEmbeddedTypeface = Typeface::createSystemTypefaceFor (BLACKMAGIC_FONT_DATA, (size_t) BLACKMAGIC_FONT_SIZE);
#endif

    setupKnob (throatSlider,       21);
    setupKnob (trackingSlider,     21);
    setupKnob (retuneSpeedSlider,  21);
    setupKnob (humanizeSlider,     21);

    constexpr float pi = MathConstants<float>::pi;
    for (auto* s : { &throatSlider, &trackingSlider, &retuneSpeedSlider, &humanizeSlider })
        s->setRotaryParameters (pi * 1.25f, pi * 2.75f, true);

    StringArray inputs, keys, scales;
    for (auto* t : blackmagic::kInputTypeNames) inputs.add (t);
    for (auto* k : blackmagic::kKeyNames)       keys.add (k);
    for (auto* s : blackmagic::kScaleNames)     scales.add (s);
    setupCombo (inputTypeBox, inputs);
    setupCombo (keyBox,       keys);
    setupCombo (scaleBox,     scales);
    setupButton (throatButton,     "ON", "OFF");
    setupButton (lowLatencyButton, "LOW LATENCY", "LOW LATENCY");

    auto& vts = audioProcessor.apvts;
    throatAttach          = std::make_unique<SliderAttach> (vts, "THROAT",       throatSlider);
    trackingAttach        = std::make_unique<SliderAttach> (vts, "TRACKING",     trackingSlider);
    retuneSpeedAttachment = std::make_unique<SliderAttach> (vts, "RETUNE_SPEED", retuneSpeedSlider);
    humanizeAttachment    = std::make_unique<SliderAttach> (vts, "HUMANIZE",     humanizeSlider);
    inputTypeAttach       = std::make_unique<ComboAttach>  (vts, "INPUT_TYPE",   inputTypeBox);
    keyAttach             = std::make_unique<ComboAttach>  (vts, "KEY",          keyBox);
    scaleAttach           = std::make_unique<ComboAttach>  (vts, "SCALE",        scaleBox);
    throatOnAttach        = std::make_unique<ButtonAttach> (vts, "THROAT_ON",    throatButton);
    lowLatencyAttach      = std::make_unique<ButtonAttach> (vts, "LOW_LATENCY",  lowLatencyButton);

    lastThroatOn = vts.getRawParameterValue ("THROAT_ON")->load() > 0.5f;
    throatSlider.getProperties().set ("dim", ! lastThroatOn);

    setResizable (true, false);
    getConstrainer()->setFixedAspectRatio ((double) (DW / DH));
    setResizeLimits (640, 360, 1600, 900);
    setSize (1024, 576);

    voteRing.fill (-1);
    startTimerHz (30);
}

BlackMagicAudioProcessorEditor::~BlackMagicAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
    if (--gEditorCount == 0) { gEmbeddedTypeface = nullptr; gTitleTypeface = nullptr; }
}

void BlackMagicAudioProcessorEditor::setupKnob (KnobSlider& s, int ticks)
{
    s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    s.getProperties().set ("ticks", ticks);
    // (sin ventanitas emergentes al pasar el raton ni al arrastrar)
    addAndMakeVisible (s);
}

void BlackMagicAudioProcessorEditor::setupButton (ToggleButton& b, const String& textOn, const String& textOff)
{
    b.setClickingTogglesState (true);
    b.setWantsKeyboardFocus (false);
    b.getProperties().set ("textOn", textOn);
    b.getProperties().set ("textOff", textOff);
    addAndMakeVisible (b);
}

void BlackMagicAudioProcessorEditor::setupCombo (ComboBox& box, const StringArray& items)
{
    box.addItemList (items, 1);
    box.setJustificationType (Justification::centredLeft);
    box.setWantsKeyboardFocus (false);
    box.setScrollWheelEnabled (false);
    addAndMakeVisible (box);
}

void BlackMagicAudioProcessorEditor::resized()
{
    scale = (float) getWidth() / DW;
    laf.setUiScale (scale);

    auto place = [this] (KnobSlider& s, const KnobPos& k)
    {
        s.setBounds (Rectangle<float> ((k.cx - k.R) * scale, (k.cy - k.R) * scale,
                                       k.R * 2.0f * scale, k.R * 2.0f * scale).toNearestInt());
    };
    place (throatSlider, kThroat);        place (trackingSlider, kTracking);
    place (retuneSpeedSlider, kRetune);   place (humanizeSlider, kHumanize);

    inputTypeBox.setBounds ((kFieldIn  * scale).toNearestInt());
    keyBox      .setBounds ((kFieldKey * scale).toNearestInt());
    scaleBox    .setBounds ((kFieldSc  * scale).toNearestInt());
    for (auto* b : { &inputTypeBox, &keyBox, &scaleBox })
        b->resized();
    throatButton    .setBounds ((kThroatPill.expanded (kPad) * scale).toNearestInt());
    lowLatencyButton.setBounds ((kLowLatPill.expanded (kPad) * scale).toNearestInt());

    renderBackground();
}

void BlackMagicAudioProcessorEditor::renderBackground()
{
    if (getWidth() < 2 || getHeight() < 2) return;
    float dpi = 1.0f;
    if (auto* d = Desktop::getInstance().getDisplays().getPrimaryDisplay()) dpi = jmax (1.0f, (float) d->scale);
    const int pw = roundToInt ((float) getWidth() * dpi), ph = roundToInt ((float) getHeight() * dpi);
    background = Image (Image::ARGB, pw, ph, true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale ((float) pw / DW, (float) ph / DH));
    drawStatic (g);
}

void BlackMagicAudioProcessorEditor::drawStatic (Graphics& g)
{
    // Panel metalico oscuro con textura
    g.setGradientFill (ColourGradient (Colour (0xff1e1e21), 0, 0, Colour (0xff101012), 0, DH, false));
    g.fillRect (0.0f, 0.0f, DW, DH);
    Random rng (1234);
    for (int i = 0; i < 420; ++i)
    {
        g.setColour (Colours::white.withAlpha (rng.nextFloat() * 0.020f));
        g.fillRect (rng.nextFloat() * DW * 0.6f, rng.nextFloat() * DH, 80.0f + rng.nextFloat() * 520.0f, 0.7f);
    }
    for (int i = 0; i < 2600; ++i)
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.02f + rng.nextFloat() * 0.04f));
        g.fillRect (rng.nextFloat() * DW, rng.nextFloat() * DH, 1.0f, 1.0f);
    }
    g.setGradientFill (ColourGradient (Colours::transparentBlack, DW * 0.5f, DH * 0.5f,
                                       Colours::black.withAlpha (0.50f), 0.0f, 0.0f, true));
    g.fillRect (0.0f, 0.0f, DW, DH);

    g.setColour (Colour (0xff050506));
    g.drawRoundedRectangle (1.5f, 1.5f, DW - 3.0f, DH - 3.0f, 14.0f, 3.0f);
    g.setColour (Colour (0xff313337));
    g.drawRoundedRectangle (6.0f, 6.0f, DW - 12.0f, DH - 12.0f, 11.0f, 1.2f);

    // Logo Dungeon Estudio (arriba a la izquierda)
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    if (dungeonLogo.isValid())
        g.drawImage (dungeonLogo, 52.0f, 12.0f, 160.0f, 75.0f, 0, 0, dungeonLogo.getWidth(), dungeonLogo.getHeight());
    else
    {
        txt (g, "Dungeon", { 50.0f, 14.0f, 170.0f, 46.0f }, ui (34.0f), Colour (0xfff2f2f2), Justification::centredLeft);
        txt (g, "ESTUDIO", { 130.0f, 8.0f, 90.0f, 16.0f }, ui (11.0f, true, 0.1f), Colour (0xfff2f2f2), Justification::centredRight);
    }

    // Titulo gotico arriba a la derecha. Titulo y subtitulo comparten el MISMO borde derecho;
    // la linea se detiene antes del texto para no cruzarlo. El titulo lleva resplandor rojo.
    const float rightEdge = 957.0f;
    const float titleLeft = drawTextRightAligned (g, "BLACK MAGIC", titleFont (46.0f), rightEdge, 48.0f, cTitleRed, 1.0f);
    drawTextRightAligned (g, "PITCH CORRECTION PROCESSOR", ui (10.5f, true, 0.1f), rightEdge, 66.0f, Colour (0xff80838a), 0.0f);
    g.setColour (Colour (0xff5a5b60));
    g.drawLine (232.0f, 35.0f, titleLeft - 30.0f, 35.0f, 1.2f);

    // Titulos de las perillas
    const Font sec = ui (15.5f, true, 0.04f);
    txt (g, "THROAT",       { kThroat.cx - 80, 100.0f, 160, 20 },   sec, cLabel);
    txt (g, "TRACKING",     { kTracking.cx - 80, 100.0f, 160, 20 }, sec, cLabel);
    txt (g, "RETUNE SPEED", { kRetune.cx - 80, 342.0f, 160, 20 },   sec, cLabel);
    txt (g, "HUMANIZE",     { kHumanize.cx - 80, 342.0f, 160, 20 }, sec, cLabel);

    // Valores minimo / maximo bajo las perillas
    const Font tick = ui (12.0f);
    auto minMax = [&] (const KnobPos& k, const char* lo, const char* hi)
    {
        txt (g, lo, { k.cx - 66, k.cy + 47, 46, 16 }, tick, cDim);
        txt (g, hi, { k.cx + 20, k.cy + 47, 46, 16 }, tick, cDim);
    };
    minMax (kThroat, "0%", "100%");  minMax (kTracking, "0%", "100%");
    minMax (kRetune, "0", "100");    minMax (kHumanize, "0%", "100%");

    // Rejillas laterales
    for (int i = 0; i < 6; ++i) { drawVent (g, 26.0f, 372.0f + (float) i * 17.0f); drawVent (g, 960.0f, 372.0f + (float) i * 17.0f); }

    // Tornillos
    drawScrew (g, { 33.0f, 35.0f }, 0.3f);    drawScrew (g, { 990.0f, 35.0f }, 1.1f);
    drawScrew (g, { 33.0f, 541.0f }, 0.8f);   drawScrew (g, { 990.0f, 541.0f }, 0.1f);
    drawScrew (g, { 290.0f, 438.0f }, 0.5f);  drawScrew (g, { 734.0f, 438.0f }, 1.3f);

    // Marco de la pantalla LCD
    Path lcdShadow; lcdShadow.addRoundedRectangle (kLcd, 12.0f);
    DropShadow (Colours::black.withAlpha (0.8f), 16, { 0, 5 }).drawForPath (g, lcdShadow);
    g.setColour (Colour (0xff3b3c41));
    g.fillRoundedRectangle (kLcd.expanded (3.0f), 14.0f);
    g.setGradientFill (ColourGradient (Colour (0xff0c0c0e), 0, kLcd.getY(), Colour (0xff050506), 0, kLcd.getBottom(), false));
    g.fillRoundedRectangle (kLcd, 12.0f);
    g.setColour (Colours::white.withAlpha (0.03f));
    g.fillRoundedRectangle (kLcd.withHeight (kLcd.getHeight() * 0.40f).reduced (3.0f, 2.0f), 10.0f);

    // Etiquetas de los campos desplegables (dentro de la LCD)
    const Font small = ui (11.5f, true, 0.1f);
    const Colour lbl (0xffa9adb4);
    txt (g, "INPUT TYPE", { kFieldIn.getX() + 2, kFieldIn.getY() - 20.0f, 160, 16 },  small, lbl, Justification::centredLeft);
    txt (g, "KEY",        { kFieldKey.getX() + 2, kFieldKey.getY() - 20.0f, 160, 16 }, small, lbl, Justification::centredLeft);
    txt (g, "SCALE",      { kFieldSc.getX() + 2, kFieldSc.getY() - 20.0f, 160, 16 },   small, lbl, Justification::centredLeft);

    g.setColour (Colour (0xff2a2b2f));
    g.drawLine (kLcd.getX() + 16.0f, 171.0f, kLcd.getRight() - 16.0f, 171.0f, 1.2f);

    txt (g, "TUNING", { 76.0f, 180.0f, 90.0f, 16.0f },  small, lbl, Justification::centredLeft);
    {   // escala bajo el medidor
        const Font sc = ui (11.0f, true, 0.08f); const Colour c (0xff767a82);
        const float xc = meterSeg (0).getCentreX();
        txt (g, "FLAT",  { kMeterX - 2.0f, 262.0f, 60.0f, 14.0f }, sc, c, Justification::centredLeft);
        txt (g, "0",     { xc - 20.0f, 262.0f, 40.0f, 14.0f }, sc, c);
        txt (g, "SHARP", { meterSeg (kMeterSegs).getRight() - 58.0f, 262.0f, 60.0f, 14.0f }, sc, c, Justification::centredRight);
        g.setColour (c.withAlpha (0.6f));
        for (int k : { -kMeterSegs, -kMeterSegs / 2, 0, kMeterSegs / 2, kMeterSegs })
            g.drawLine (meterSeg (k).getCentreX(), 258.0f, meterSeg (k).getCentreX(), 262.0f, 1.0f);
    }
    txt (g, "NOTE",  { 440.0f, 180.0f, 90.0f, 16.0f }, small, lbl, Justification::centredLeft);
    g.setColour (Colour (0xff2a2b2f));
    g.drawLine (410.0f, 184.0f, 410.0f, 286.0f, 1.2f);

    // Pie de pagina centrado entre lineas
    const float fy = DH - 48.0f, tw = 200.0f, tx = (DW - tw) * 0.5f;
    txt (g, "DUNGEON  ESTUDIO", { tx, fy - 10.0f, tw, 20.0f }, ui (14.0f, true, 0.25f), Colour (0xff5a5f64));
    g.setColour (Colour (0xff4a4d52));
    g.drawLine (50.0f, fy, tx - 20.0f, fy, 1.0f);
    g.drawLine (tx + tw + 20.0f, fy, DW - 50.0f, fy, 1.0f);
}

void BlackMagicAudioProcessorEditor::drawDynamic (Graphics& g)
{
    // Segmentos apagados del medidor de afinacion (los encendidos se dibujan en la capa con resplandor)
    for (int i = -kMeterSegs; i <= kMeterSegs; ++i)
        if (! meterLit (i, devCents, haveDev))
        {
            g.setColour (meterColour (i).withMultipliedBrightness (0.20f).withAlpha (i == 0 ? 0.9f : 0.55f));
            g.fillRoundedRectangle (meterSeg (i), 2.0f);
        }

    // Lectura de latencia real (cambia con Low Latency y con el tipo de voz)
    const double sr = audioProcessor.uiSampleRate.load();
    if (sr > 0.0)
    {
        const double ms = 1000.0 * (double) audioProcessor.getLatencySamples() / sr;
        txt (g, "LATENCY  " + String (ms, 1) + " ms", { kLowLatPill.getX() - 10.0f, kLowLatPill.getBottom() + 8.0f, kLowLatPill.getWidth() + 20.0f, 16.0f },
             ui (11.5f, true, 0.06f), Colour (0xff8e9197));
    }
}

// Todo lo "encendido" del LCD (barras activas y nota) se dibuja en una capa aparte que se desenfoca
// y se suma debajo: eso da el resplandor (bloom). Se trabaja en pixeles reales, no en espacio de diseno.
void BlackMagicAudioProcessorEditor::drawLcdGlow (Graphics& g)
{
    const auto region = (kLcdLower * scale).toNearestIntEdges();
    if (region.isEmpty()) return;

    Image em (Image::ARGB, region.getWidth(), region.getHeight(), true);
    {
        Graphics eg (em);
        eg.addTransform (AffineTransform::scale (scale).translated ((float) -region.getX(), (float) -region.getY()));

        for (int i = -kMeterSegs; i <= kMeterSegs; ++i)
            if (meterLit (i, devCents, haveDev))
            {
                // el segmento central se ve verde cuando esta afinado (< ~3 cents) y blanco si no
                Colour c = (i == 0) ? (std::abs (devCents) < 3.0f ? Colour (0xff35d94a) : Colour (0xffe8eaee)) : meterColour (i);
                eg.setColour (c);
                eg.fillRoundedRectangle (meterSeg (i), 2.0f);
            }
        drawNoteReadout (eg, shownNote, 506.0f, 200.0f, 76.0f);
    }

    Image blurred;
    ImageEffects::applyGaussianBlurEffect (9.0f * scale, em, blurred);
    g.setOpacity (1.0f);
    g.drawImageAt (blurred, region.getX(), region.getY());
    g.drawImageAt (em, region.getX(), region.getY());
}

void BlackMagicAudioProcessorEditor::paint (Graphics& g)
{
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    if (background.isValid())
        g.drawImage (background, getLocalBounds().toFloat(), RectanglePlacement::stretchToFit);
    else
        g.fillAll (Colour (0xff121214));

    {
        Graphics::ScopedSaveState ss (g);
        g.addTransform (AffineTransform::scale (scale));
        drawDynamic (g);
    }
    drawLcdGlow (g);
}

void BlackMagicAudioProcessorEditor::timerCallback()
{
    const float tgt = audioProcessor.uiTargetMidi.load(), det = audioProcessor.uiDetectedMidi.load();

    // Medidor de afinacion: cuanto se aleja la voz de la nota a la que se corrige (cents), suavizado
    if (tgt >= 0.0f && det >= 0.0f)
    {
        const float dev = jlimit (-50.0f, 50.0f, (det - tgt) * 100.0f);
        devCents = haveDev ? devCents + 0.35f * (dev - devCents) : dev;
        haveDev = true;
    }
    else if (haveDev)
    {
        devCents *= 0.8f;                          // sin voz: vuelve al centro
        if (shownNote < 0) haveDev = false;
    }

    // Nota de salida PROMEDIADA: voto de las ultimas 10 lecturas (~0.33 s). La nota mostrada solo cambia si la nueva
    // nota tiene >= 4 votos (~0.13 s) y se apaga cuando no hay voz en toda la ventana.
    voteRing[(size_t) voteIdx] = tgt >= 0.0f ? (int) std::lround (tgt) : -1;
    voteIdx = (voteIdx + 1) % (int) voteRing.size();
    {
        int votes[128] = {}; int valid = 0, best = -1;
        for (int v : voteRing) if (v >= 0 && v < 128) { ++votes[v]; ++valid; }
        for (int n = 0; n < 128; ++n) if (votes[n] > 0 && (best < 0 || votes[n] > votes[best])) best = n;
        if (valid == 0) shownNote = -1;
        else if (best >= 0 && best != shownNote && votes[best] >= (shownNote < 0 ? 3 : 4)) shownNote = best;
    }

    // Throat desactivado -> la perilla se ve apagada (tambien si el cambio viene de automatizacion)
    const bool throatOn = audioProcessor.apvts.getRawParameterValue ("THROAT_ON")->load() > 0.5f;
    if (throatOn != lastThroatOn)
    {
        lastThroatOn = throatOn;
        throatSlider.getProperties().set ("dim", ! throatOn);
        throatSlider.repaint();
    }

    const bool changed = std::abs (devCents - lastDevDrawn) > 0.4f || shownNote != lastShownNote;
    lastDevDrawn = devCents; lastShownNote = shownNote;
    if (changed)
        repaint ((kLcdLower * scale).toNearestIntEdges());

    const int lat = audioProcessor.getLatencySamples();
    if (lat != lastLatency)
    {
        lastLatency = lat;
        repaint (((kLowLatPill.expanded (14.0f, 0.0f).withBottom (kLowLatPill.getBottom() + 30.0f)) * scale).toNearestIntEdges());
    }
}
