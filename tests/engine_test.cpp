#include "../Source/DSP/AutoTuneEngine.h"
#include <cstdio>
#include <random>
using namespace blackmagic;

static const double PI = 3.14159265358979323846;

// Senal tipo voz: armonicos con una "formante" (resonancia) alrededor de formantHz
static std::vector<float> makeVoice (double sr, double seconds, double f0, double vibHz = 0, double vibDepthHz = 0,
                                     double formantHz = 900.0)
{
    std::vector<float> x ((size_t) (sr * seconds));
    double ph = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        double f = f0 + (vibHz > 0 ? vibDepthHz * std::sin (2 * PI * vibHz * (double) i / sr) : 0);
        ph += 2 * PI * f / sr;
        double s = 0;
        for (int k = 1; k <= 24 && k * f < sr * 0.45; ++k)
        {
            double fk = k * f0;
            double env = 0.15 / k + std::exp (-0.5 * std::pow ((fk - formantHz) / 350.0, 2.0));
            s += env * std::sin (k * ph);
        }
        x[i] = (float) (0.15 * s);
    }
    return x;
}

static double measureHz (const std::vector<float>& y, double sr, size_t a, size_t b)
{
    int minLag = (int) (sr / 1000), maxLag = (int) (sr / 80);
    double best = -1; int bl = minLag;
    std::vector<double> r ((size_t) maxLag + 2);
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double s = 0, e1 = 0, e2 = 0;
        for (size_t i = a; i + (size_t) lag < b; ++i) { s += (double) y[i] * y[i + (size_t) lag]; e1 += (double) y[i]*y[i]; e2 += (double) y[i+(size_t)lag]*y[i+(size_t)lag]; }
        r[(size_t) lag] = s / (std::sqrt (e1 * e2) + 1e-12);
    }
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (r[(size_t) lag] > best && r[(size_t) lag] >= r[(size_t) lag-1] && r[(size_t) lag] >= r[(size_t) lag+1]) { best = r[(size_t) lag]; bl = lag; }
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (r[(size_t) lag] > 0.9 * best && r[(size_t) lag] >= r[(size_t) lag-1] && r[(size_t) lag] >= r[(size_t) lag+1]) { bl = lag; break; }
    double s0 = r[(size_t) bl-1], s1 = r[(size_t) bl], s2 = r[(size_t) bl+1];
    double d = (s0 - 2*s1 + s2);
    double off = std::abs (d) > 1e-12 ? 0.5 * (s0 - s2) / d : 0;
    return sr / ((double) bl + off);
}

// Centroide espectral usando Goertzel en los armonicos de f0
static double centroid (const std::vector<float>& y, double sr, double f0, size_t a, size_t b)
{
    double num = 0, den = 0;
    for (int k = 1; k <= 24 && k * f0 < sr * 0.45; ++k)
    {
        double w = 2 * PI * k * f0 / sr, re = 0, im = 0;
        for (size_t i = a; i < b; ++i) { re += y[i] * std::cos (w * (double) (i - a)); im += y[i] * std::sin (w * (double) (i - a)); }
        double amp = std::sqrt (re*re + im*im);
        num += k * f0 * amp; den += amp;
    }
    return num / (den + 1e-12);
}

static std::vector<float> run (const std::vector<float>& in, double sr, const AutoTuneEngine::Params& p, int block = 512)
{
    AutoTuneEngine e; e.prepare (sr);
    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); i += (size_t) block)
    {
        int n = (int) std::min ((size_t) block, in.size() - i);
        std::vector<float> tmp (in.begin() + (long) i, in.begin() + (long) i + n);
        e.process (tmp.data(), nullptr, tmp.data(), nullptr, n, p);
        std::copy (tmp.begin(), tmp.end(), out.begin() + (long) i);
    }
    return out;
}

static AutoTuneEngine::Params mk (int key, int scale, int inputType = 1)
{
    AutoTuneEngine::Params p; p.retune = 0; p.inputType = inputType;
    bool m[12]; buildAllowedMask (key, scale, m);
    for (int i = 0; i < 12; ++i) p.allowed[(size_t) i] = m[i];
    return p;
}

int main()
{
    int fails = 0;
    auto check = [&] (const char* name, double got, double expect, double tol)
    {
        bool ok = std::abs (got - expect) <= tol;
        std::printf ("%-56s got %8.2f  expect %8.2f  %s\n", name, got, expect, ok ? "OK" : "FALLA");
        if (! ok) ++fails;
    };
    auto cond = [&] (const char* name, bool ok, double v1, double v2)
    {
        std::printf ("%-56s %8.2f vs %8.2f  %s\n", name, v1, v2, ok ? "OK" : "FALLA");
        if (! ok) ++fails;
    };

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::printf ("--- sample rate %.0f ---\n", sr);
        size_t a = (size_t) (sr * 1.5), b = (size_t) (sr * 2.5);
        auto p = mk (0, 0);   // C Major

        check ("215 Hz, C Major, retune 0 -> A3", measureHz (run (makeVoice (sr, 3.0, 215.0), sr, p), sr, a, b), 220.0, 1.0);
        check ("vibrato 215+-8 Hz, retune 0 -> A3 fijo", measureHz (run (makeVoice (sr, 3.0, 215.0, 5.5, 8.0), sr, p), sr, a, b), 220.0, 2.0);
        auto x2 = makeVoice (sr, 3.0, 228.0);
        check ("228 Hz, C Major (A# no existe) -> A3", measureHz (run (x2, sr, p), sr, a, b), 220.0, 1.0);
        check ("228 Hz, Chromatic -> A#3", measureHz (run (x2, sr, mk (0, 2)), sr, a, b), 233.08, 1.5);
        check ("250 Hz, A Minor -> B3", measureHz (run (makeVoice (sr, 3.0, 250.0), sr, mk (9, 1)), sr, a, b), 246.94, 1.5);

        // Pentatonica menor de A: A C D E G  (la nota B=246.9 no existe)
        check ("250 Hz, A Pent. menor -> C4 (261.6)", measureHz (run (makeVoice (sr, 3.0, 250.0), sr, mk (9, 5)), sr, a, b), 261.63, 2.0);

        // Tipos de voz
        check ("Low Male: 105 Hz -> G#2 (103.83)", measureHz (run (makeVoice (sr, 3.0, 105.0), sr, mk (0, 2, 0)), sr, a, b), 103.83, 1.2);
        check ("Female: 335 Hz, C Major -> E4 (329.63)", measureHz (run (makeVoice (sr, 3.0, 335.0), sr, mk (0, 0, 2)), sr, a, b), 329.63, 2.0);
        check ("Alto/Tenor: 215 Hz -> A3", measureHz (run (makeVoice (sr, 3.0, 215.0), sr, mk (0, 0, 1)), sr, a, b), 220.0, 1.0);
        check ("Broadcast: 215 Hz -> A3", measureHz (run (makeVoice (sr, 3.0, 215.0), sr, mk (0, 0, 3)), sr, a, b), 220.0, 1.0);

        // Throat: no cambia el pitch, pero mueve las formantes
        {
            auto x = makeVoice (sr, 3.0, 220.0);
            auto pt = mk (0, 0); pt.throatOn = true;
            pt.throat = 0;   auto y0   = run (x, sr, pt);
            pt.throat = 50;  auto y50  = run (x, sr, pt);
            pt.throat = 100; auto y100 = run (x, sr, pt);
            double h0 = measureHz (y0, sr, a, b), h100 = measureHz (y100, sr, a, b);
            check ("Throat 0: el pitch se mantiene en 220", h0, 220.0, 1.0);
            check ("Throat 100: el pitch se mantiene en 220", h100, 220.0, 1.0);
            double c0 = centroid (y0, sr, 220.0, a, a + 8192), c50 = centroid (y50, sr, 220.0, a, a + 8192), c100 = centroid (y100, sr, 220.0, a, a + 8192);
            cond ("Throat: brillo 0 < 50", c0 < c50 - 5, c0, c50);
            cond ("Throat: brillo 50 < 100", c50 < c100 - 5, c50, c100);
        }

        // Humanize: en una nota larga con vibrato, deja pasar mas variacion natural
        {
            auto x = makeVoice (sr, 4.0, 220.0, 5.5, 6.0);
            auto stdOfPitch = [&] (const AutoTuneEngine::Params& pp)
            {
                auto y = run (x, sr, pp);
                std::vector<double> v;
                size_t win = (size_t) (sr * 0.06);
                for (size_t s = (size_t) (sr * 1.5); s + win < (size_t) (sr * 3.8); s += win / 2) v.push_back (measureHz (y, sr, s, s + win));
                double m = 0; for (double q : v) m += q; m /= (double) v.size();
                double var = 0; for (double q : v) var += (q - m) * (q - m);
                return std::sqrt (var / (double) v.size());
            };
            auto ph = mk (0, 0);
            ph.humanize = 0;   double s0 = stdOfPitch (ph);
            ph.humanize = 100; double s1 = stdOfPitch (ph);
            cond ("Humanize 100 deja pasar mas variacion que 0 (std Hz)", s1 > s0 * 2.0 + 0.2, s0, s1);
        }

        // Low Latency: la afinacion debe seguir siendo exacta
        {
            auto pl = mk (0, 0); pl.lowLatency = true;
            check ("LowLat: 215 Hz, C Major -> A3", measureHz (run (makeVoice (sr, 3.0, 215.0), sr, pl), sr, a, b), 220.0, 1.2);
            check ("LowLat: vibrato 215+-8 Hz -> A3 fijo", measureHz (run (makeVoice (sr, 3.0, 215.0, 5.5, 8.0), sr, pl), sr, a, b), 220.0, 2.5);
            auto pf = mk (0, 0, 2); pf.lowLatency = true;
            check ("LowLat Female: 335 Hz -> E4", measureHz (run (makeVoice (sr, 3.0, 335.0), sr, pf), sr, a, b), 329.63, 2.5);
            auto pm2 = mk (0, 2, 0); pm2.lowLatency = true;
            check ("LowLat Low Male: 105 Hz -> G#2", measureHz (run (makeVoice (sr, 3.0, 105.0), sr, pm2), sr, a, b), 103.83, 1.5);
        }

        // Throat desactivado = transparente aunque el knob este movido
        {
            auto x = makeVoice (sr, 3.0, 196.0);
            auto pt = mk (0, 0); pt.throat = 100; pt.throatOn = false;
            auto y = run (x, sr, pt);
            AutoTuneEngine tmp; tmp.prepare (sr); int D = tmp.getLatencySamples();
            double best = -2;
            for (int lag = -(int) (sr / 80); lag <= (int) (sr / 80); ++lag)
            {
                double s2 = 0, e1 = 0, e2 = 0;
                for (size_t i = a; i < b; ++i) { double u = y[i], v = x[(size_t) ((long) i - D + lag)]; s2 += u*v; e1 += u*u; e2 += v*v; }
                best = std::max (best, s2 / std::sqrt (e1 * e2 + 1e-20));
            }
            cond ("Throat OFF (knob a 100): nota afinada intacta (corr)", best > 0.995, best, 1.0);
        }

        // Retune lento
        AutoTuneEngine::Params ps = p; ps.retune = 100;
        check ("retune 100: tras 4.5 s casi en 220", measureHz (run (makeVoice (sr, 5.0, 215.0), sr, ps), sr, (size_t)(sr*4.0), (size_t)(sr*4.8)), 220.0, 1.5);
        double early = measureHz (run (makeVoice (sr, 0.6, 215.0), sr, ps), sr, (size_t)(sr*0.25), (size_t)(sr*0.45));
        cond ("retune 100: a los 0.3 s todavia entre 214.5 y 218.8", early < 218.8 && early > 214.5, early, 216.0);
    }

    // Tracking: valores extremos no rompen nada y tracking alto aguanta voz ruidosa
    {
        double sr = 44100; std::mt19937 rng (7); std::normal_distribution<float> nd (0.0f, 0.02f);
        auto x = makeVoice (sr, 3.0, 215.0);
        for (auto& s : x) s += nd (rng);
        for (float trk : { 0.0f, 50.0f, 100.0f })
        {
            auto pt = mk (0, 0); pt.tracking = trk;
            double hz = measureHz (run (x, sr, pt), sr, (size_t) (sr*1.5), (size_t) (sr*2.5));
            char name[96]; std::snprintf (name, sizeof name, "voz con ruido, tracking %.0f -> A3", trk);
            check (name, hz, 220.0, 2.0);
        }
    }

    // robustez
    {
        double sr = 44100; std::mt19937 rng (1); std::uniform_real_distribution<float> u (-0.5f, 0.5f);
        std::vector<float> n ((size_t) (sr * 2)); for (auto& s : n) s = u (rng);
        for (int type = 0; type < 4; ++type)
        {
            auto pp = mk (0, 0, type); pp.throat = type * 33.0f; pp.humanize = 50; pp.tracking = type * 33.0f;
            auto y = run (n, sr, pp, 137);
            bool bad = false; for (float s : y) if (! std::isfinite (s) || std::abs (s) > 4.0f) bad = true;
            std::printf ("ruido blanco, tipo %d, sin NaN/explosion: %s\n", type, bad ? "FALLA" : "OK"); if (bad) ++fails;
        }
        std::vector<float> z ((size_t) sr, 0.0f); auto yz = run (z, sr, mk (0, 0));
        bool bad = false; for (float s : yz) if (s != 0.0f) bad = true;
        std::printf ("silencio -> silencio: %s\n", bad ? "FALLA" : "OK"); if (bad) ++fails;
        AutoTuneEngine e; e.prepare (44100);
        for (int ll = 0; ll < 2; ++ll)
            for (int type = 0; type < 4; ++type)
                std::printf ("latencia %-9s %-11s: %4d muestras = %5.1f ms\n", ll ? "LowLat" : "Normal", kInputTypeNames[type],
                             e.computeLatency (ll != 0, type), 1000.0 * e.computeLatency (ll != 0, type) / 44100.0);
    }
    std::printf ("\n%s (%d fallas)\n", fails ? "HAY FALLAS" : "TODO OK", fails);
    return fails ? 1 : 0;
}
