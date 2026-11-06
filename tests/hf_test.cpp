#include "../Source/DSP/AutoTuneEngine.h"
#include <cstdio>
#include <random>
#include <complex>
using namespace blackmagic;
static const double PI = 3.14159265358979323846;

// Voz realista: armonicos hasta 12 kHz con caida suave, jitter, shimmer, vibrato y soplido (ruido de alta frecuencia)
static std::vector<float> realisticVoice (double sr, double sec, double f0, unsigned seed)
{
    std::mt19937 rng (seed); std::normal_distribution<double> nd (0, 1);
    std::vector<float> x ((size_t) (sr * sec));
    double ph = 0, jit = 0, amp = 1, nz1 = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        if (i % 200 == 0) { jit = 0.7 * jit + 0.3 * nd (rng) * 0.006; amp = 0.95 * amp + 0.05 * (1 + 0.15 * nd (rng)); }
        double f = f0 * (1.0 + jit) + 4.0 * std::sin (2 * PI * 5.2 * (double) i / sr);
        ph += 2 * PI * f / sr;
        double s = 0;
        for (int k = 1; k * f0 < 12000.0 && k < 80; ++k)
        {
            double fk = k * f0;
            double env = std::pow ((double) k, -1.1) + 0.8 * std::exp (-0.5 * std::pow ((fk - 2800.0) / 900.0, 2.0));
            s += env * std::sin (k * ph + 0.3 * k);
        }
        double n = nd (rng); double hp = n - nz1; nz1 = n;
        x[i] = (float) (0.12 * amp * s + 0.010 * hp);
    }
    return x;
}

static void fft (std::vector<std::complex<double>>& a)
{
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        double ang = -2 * PI / (double) len; std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w (1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}

static std::vector<double> psd (const std::vector<float>& x, size_t a, size_t b, size_t N = 4096)
{
    std::vector<double> p (N / 2, 0.0); int cnt = 0;
    for (size_t s = a; s + N <= b; s += N / 2)
    {
        std::vector<std::complex<double>> f (N);
        for (size_t i = 0; i < N; ++i) f[i] = x[s + i] * (0.5 - 0.5 * std::cos (2 * PI * (double) i / (double) N));
        fft (f); for (size_t i = 0; i < N / 2; ++i) p[i] += std::norm (f[i]); ++cnt;
    }
    for (auto& v : p) v /= cnt;
    return p;
}

static double bandDb (const std::vector<double>& pi, const std::vector<double>& po, double sr, double lo, double hi)
{
    size_t N = pi.size() * 2; double ei = 0, eo = 0;
    for (size_t k = 1; k < pi.size(); ++k) { double f = (double) k * sr / (double) N; if (f >= lo && f < hi) { ei += pi[k]; eo += po[k]; } }
    return 10 * std::log10 ((eo + 1e-30) / (ei + 1e-30));
}

static std::vector<float> run (const std::vector<float>& in, double sr, const AutoTuneEngine::Params& p)
{
    AutoTuneEngine e; e.prepare (sr);
    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); i += 512)
    {
        int n = (int) std::min ((size_t) 512, in.size() - i);
        std::vector<float> t (in.begin() + (long) i, in.begin() + (long) i + n);
        e.process (t.data(), nullptr, t.data(), nullptr, n, p);
        std::copy (t.begin(), t.end(), out.begin() + (long) i);
    }
    return out;
}


// Regresion: la correccion NO debe apagar los agudos (cada banda en dB, salida vs entrada)
int main()
{
    const double bands[4][2] = { { 1000, 3000 }, { 3000, 6000 }, { 6000, 10000 }, { 10000, 16000 } };
    struct Cfg { const char* name; bool lowLat, throatOn; float throat; double minDb[4]; };
    const Cfg cfgs[] = {
        { "Normal           ", false, false, 50, { -0.6, -0.9, -0.4, -0.2 } },
        { "Low Latency      ", true,  false, 50, { -0.6, -0.9, -0.4, -0.2 } },
        { "Throat ON  50 %  ", false, true,  50, { -0.6, -0.9, -0.8, -1.6 } },
        { "Throat ON  85 %  ", false, true,  85, { -4.0, -4.0, -4.0, -4.0 } },
        { "Throat ON  15 %  ", false, true,  15, { -4.0, -4.0, -4.0, -4.0 } },
    };
    int fails = 0;
    for (auto& c : cfgs)
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    for (double f0 : { 130.0, 215.0, 330.0 })
    {
        AutoTuneEngine::Params p; p.retune = 0; p.inputType = 3;
        p.lowLatency = c.lowLat; p.throatOn = c.throatOn; p.throat = c.throat;
        auto x = realisticVoice (sr, 4.0, f0, 5);
        auto y = run (x, sr, p);
        const size_t a = (size_t) (sr * 1.2), b = (size_t) (sr * 3.8);
        auto pi = psd (x, a, b, 8192), po = psd (y, a, b, 8192);
        std::printf ("%s sr %5.0f f0 %3.0f :", c.name, sr, f0);
        for (int k = 0; k < 4; ++k)
        {
            const double d = bandDb (pi, po, sr, bands[k][0], bands[k][1]);
            const bool ok = d >= c.minDb[k];
            std::printf (" %6.2f%s", d, ok ? "  " : "!!");
            if (! ok) ++fails;
        }
        std::printf ("  dB (1-3k | 3-6k | 6-10k | 10-16k)\n");
    }
    std::printf ("\n%s (%d fallas)\n", fails ? "HAY FALLAS" : "AGUDOS OK", fails);
    return fails ? 1 : 0;
}
