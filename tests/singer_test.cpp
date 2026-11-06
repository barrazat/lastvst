// Prueba de regresion con un "cantante" sintetico: pulsos glotales + 4 formantes cambiantes + vibrato + jitter +
// glissandos + soplido (y una version aspera con dos picos por ciclo). Comprueba afinacion, periodicidad,
// clics y respuesta de agudos del motor final. Estos casos son los que delataron que centrar los grains en el
// pulso de forma continua arruinaba la afinacion con voces reales.
#include "../Source/DSP/AutoTuneEngine.h"
#include <cstdio>
#include <random>
#include <functional>
#include <complex>
using namespace blackmagic;
static const double PI = 3.14159265358979323846;

struct Note { double midi; double dur; };   // nota cantada (con desafinacion) y duracion

// Cantante sintetico: pulsos glotales + 4 formantes variables + vibrato + jitter + glissando + soplido
static double gRough = 0.0;   // 0 = voz limpia, 1 = voz aspera/ronca
static std::vector<float> singer (double sr, const std::vector<Note>& notes, std::vector<double>& targetHz, unsigned seed)
{
    std::mt19937 rng (seed); std::normal_distribution<double> nd (0, 1);
    size_t total = 0; for (auto& n : notes) total += (size_t) (n.dur * sr);
    std::vector<float> y (total); targetHz.assign (total, 0.0);
    double phase = 0, jit = 0, amp = 1, midiPrev = notes[0].midi, oqCycle = 0.6;
    // resonadores
    struct Res { double a = 0, b1 = 0, b2 = 0, y1 = 0, y2 = 0; void set (double f, double bw, double sr) { double r = std::exp (-PI * bw / sr), th = 2 * PI * f / sr; b1 = 2 * r * std::cos (th); b2 = -r * r; a = 1 - r; } double p (double x) { double o = a * x + b1 * y1 + b2 * y2; y2 = y1; y1 = o; return o; } } R[4];
    const double vowels[3][4] = { { 800, 1200, 2600, 3500 }, { 450, 800, 2500, 3400 }, { 350, 2200, 2900, 3700 } };
    size_t pos = 0; int ni = 0;
    for (auto& n : notes)
    {
        size_t len = (size_t) (n.dur * sr);
        // objetivo: nota entera mas cercana
        // nota permitida mas cercana en Do mayor (igual que el plugin)
        double tgtMidi = n.midi; { static const bool cm[12] = { 1,0,1,0,1,1,0,1,0,1,0,1 }; double bd = 1e9; for (int c = (int) n.midi - 4; c <= (int) n.midi + 5; ++c) if (cm[((c % 12) + 12) % 12] && std::abs (c - n.midi) < bd) { bd = std::abs (c - n.midi); tgtMidi = c; } }
        for (size_t i = 0; i < len; ++i, ++pos)
        {
            double tsec = (double) i / sr;
            double glide = std::min (1.0, tsec / 0.07);                      // glissando de 70 ms desde la nota anterior
            double m = midiPrev + (n.midi - midiPrev) * (0.5 - 0.5 * std::cos (PI * glide));
            double vibDepth = std::min (1.0, std::max (0.0, (tsec - 0.15) / 0.2)) * 0.18;   // 18 cents de vibrato
            m += vibDepth * std::sin (2 * PI * 5.6 * (double) pos / sr);
            if (pos % 120 == 0) jit = 0.8 * jit + 0.2 * nd (rng) * (0.004 + 0.012 * gRough);
            double f = 440.0 * std::pow (2.0, (m - 69.0) / 12.0) * (1.0 + jit);
            targetHz[pos] = 440.0 * std::pow (2.0, (tgtMidi - 69.0) / 12.0);
            phase += f / sr; if (phase >= 1.0) phase -= 1.0;
            // pulso de Rosenberg
            if (phase < f / sr) oqCycle = 0.6 + gRough * (0.45 + 0.30 * (double) (rng() % 1000) / 1000.0 - 0.6) ; // oq cambia cada ciclo
            double oq = std::max (0.3, std::min (0.8, oqCycle)), g = phase < oq ? 0.5 * (1 - std::cos (PI * phase / oq)) : std::cos (PI * 0.5 * (phase - oq) / (1 - oq));
            double src = g + gRough * 0.45 * (phase > 0.42 && phase < 0.52 ? 0.5 * (1 - std::cos (2 * PI * (phase - 0.42) / 0.10)) : 0.0);   // segundo pulso (voz aspera)
            if (pos % 200 == 0) amp = 0.97 * amp + 0.03 * (1 + 0.1 * nd (rng));
            double asp = (0.03 + 0.10 * gRough) * nd (rng) * (phase < oq ? 1.0 : 0.3);
            // vocal cambia lentamente entre 3 vocales
            double vt = (double) pos / sr * 1.3; int v0 = (int) vt % 3, v1 = (v0 + 1) % 3; double fr = vt - std::floor (vt);
            for (int k = 0; k < 4; ++k) { double fk = vowels[v0][k] * (1 - fr) + vowels[v1][k] * fr; R[k].set (fk, 60 + 40 * k, sr); }
            double e = src + asp;
            double o = 0;
            double e1 = R[0].p (e), e2 = R[1].p (e), e3 = R[2].p (e), e4 = R[3].p (e);
            o = e1 * 1.0 + e2 * 0.6 + e3 * 0.35 + e4 * 0.2;
            y[pos] = (float) (amp * o);
        }
        midiPrev = n.midi; ++ni;
    }
    // quitar DC y normalizar
    double mean = 0; for (float v : y) mean += v; mean /= (double) y.size();
    double pk = 0; for (auto& v : y) { v = (float) (v - mean); pk = std::max (pk, (double) std::abs (v)); }
    for (auto& v : y) v = (float) (v / pk * 0.5);
    return y;
}

// ---------- metricas ----------
struct Frame { double hz, periodicity; };
static Frame analyseFrame (const std::vector<float>& y, double sr, size_t a, size_t n)
{
    int minLag = (int) (sr / 800), maxLag = (int) (sr / 80);
    std::vector<double> r ((size_t) maxLag + 2, 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double s = 0, e1 = 0, e2 = 0;
        for (size_t i = a; i + (size_t) lag < a + n; ++i) { s += (double) y[i] * y[i + (size_t) lag]; e1 += (double) y[i] * y[i]; e2 += (double) y[i + (size_t) lag] * y[i + (size_t) lag]; }
        r[(size_t) lag] = s / (std::sqrt (e1 * e2) + 1e-12);
    }
    double best = -1; int bl = minLag;
    for (int lag = minLag + 1; lag <= maxLag; ++lag) if (r[(size_t) lag] > best && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1]) { best = r[(size_t) lag]; bl = lag; }
    for (int lag = minLag + 1; lag <= maxLag; ++lag) if (r[(size_t) lag] > 0.92 * best && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1]) { bl = lag; break; }
    double s0 = r[(size_t) bl - 1], s1 = r[(size_t) bl], s2 = r[(size_t) bl + 1], d = s0 - 2 * s1 + s2, off = std::abs (d) > 1e-12 ? 0.5 * (s0 - s2) / d : 0;
    return { sr / ((double) bl + off), s1 };
}

template <class Engine>
static std::vector<float> runEngine (const std::vector<float>& in, double sr, const std::function<void (typename Engine::Params&)>& cfg, int& latency)
{
    Engine e; e.prepare (sr);
    typename Engine::Params p; cfg (p);
    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); i += 512)
    {
        int n = (int) std::min ((size_t) 512, in.size() - i);
        std::vector<float> t (in.begin() + (long) i, in.begin() + (long) i + n);
        e.process (t.data(), nullptr, t.data(), nullptr, n, p);
        std::copy (t.begin(), t.end(), out.begin() + (long) i);
    }
    latency = e.getLatencySamples();
    return out;
}

static void fft (std::vector<std::complex<double>>& a)
{
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) { double ang = -2 * PI / (double) len; std::complex<double> wl (std::cos (ang), std::sin (ang)); for (size_t i = 0; i < n; i += len) { std::complex<double> w (1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } } }
}
static double bandDb (const std::vector<float>& x, const std::vector<float>& y, double sr, double lo, double hi, size_t a, size_t b)
{
    const size_t N = 4096; double ei = 0, eo = 0;
    for (size_t s = a; s + N <= b; s += N / 2)
    {
        std::vector<std::complex<double>> fx (N), fy (N);
        for (size_t i = 0; i < N; ++i) { double w = 0.5 - 0.5 * std::cos (2 * PI * (double) i / (double) N); fx[i] = x[s + i] * w; fy[i] = y[s + i] * w; }
        fft (fx); fft (fy);
        for (size_t k = 1; k < N / 2; ++k) { double f = (double) k * sr / (double) N; if (f >= lo && f < hi) { ei += std::norm (fx[k]); eo += std::norm (fy[k]); } }
    }
    return 10 * std::log10 ((eo + 1e-30) / (ei + 1e-30));
}

struct Report { double errAbsCents, within10, periodicity, clicks, hf36, hf610, hf1016; int latency; };
static std::vector<double> gStarts;   // inicios de nota (s)
static bool gSteadyOnly = false;

static Report evaluate (const std::vector<float>& in, const std::vector<float>& out, const std::vector<double>& targetHz, double sr, int latency)
{
    Report r {}; r.latency = latency;
    const size_t N = (size_t) (0.040 * sr);
    double sumErr = 0, sumPer = 0; int cnt = 0, ok10 = 0;
    for (size_t s = (size_t) (0.3 * sr); s + N + (size_t) latency < in.size(); s += N / 2)
    {
        size_t centre = s + N / 2;
        if (targetHz[centre] <= 0) continue;
        // solo tramos estables (no durante el glissando de 70 ms al inicio de cada nota)
        Frame f = analyseFrame (out, sr, s + (size_t) latency, N);
        Frame fi = analyseFrame (in,  sr, s, N);
        if (fi.periodicity < 0.8) continue;
        if (gSteadyOnly)
        {
            double tc = (double) centre / sr; bool skip = false;
            for (size_t q = 0; q < gStarts.size(); ++q) { double st = gStarts[q], en = q + 1 < gStarts.size() ? gStarts[q + 1] : 1e9; if (tc >= st && (tc - st < 0.20 || en - tc < 0.06)) skip = true; }
            if (skip) continue;
        }
        double cents = 1200.0 * std::log2 (f.hz / targetHz[centre]);
        if (std::abs (cents) > 300) cents = 300;          // salto de octava u otro error grave
        sumErr += std::abs (cents); sumPer += f.periodicity; ++cnt; if (std::abs (cents) < 10) ++ok10;
    }
    r.errAbsCents = sumErr / std::max (1, cnt); r.within10 = 100.0 * ok10 / std::max (1, cnt); r.periodicity = sumPer / std::max (1, cnt);
    // clics: picos de la derivada comparados con la entrada
    auto spike = [&] (const std::vector<float>& v) { double m = 0; std::vector<double> d (v.size()); for (size_t i = 1; i < v.size(); ++i) d[i] = std::abs ((double) v[i] - v[i - 1]); for (double q : d) m += q; m /= (double) v.size(); int c = 0; for (double q : d) if (q > 12 * m) ++c; return c; };
    r.clicks = spike (out) - spike (in);
    size_t a = (size_t) (0.5 * sr), b = in.size() - (size_t) (0.2 * sr);
    std::vector<float> inAl (in.size(), 0.0f); for (size_t i = (size_t) latency; i < in.size(); ++i) inAl[i] = in[i - (size_t) latency];
    r.hf36 = bandDb (inAl, out, sr, 3000, 6000, a, b); r.hf610 = bandDb (inAl, out, sr, 6000, 10000, a, b); r.hf1016 = bandDb (inAl, out, sr, 10000, 16000, a, b);
    return r;
}


static void hfTable (const char* name, const std::vector<float>& x, const std::vector<float>& y, int lat, double sr)
{
    const double bands[7][2] = { {800,1500},{1500,2500},{2500,3500},{3500,5000},{5000,7000},{7000,10000},{10000,16000} };
    std::vector<float> inAl (x.size(), 0.0f); for (size_t i = (size_t) lat; i < x.size(); ++i) inAl[i] = x[i - (size_t) lat];
    size_t a = (size_t) (0.5 * sr), b = x.size() - (size_t) (0.2 * sr);
    std::printf ("%-40s", name); for (auto& bd : bands) std::printf (" %6.2f", bandDb (inAl, y, sr, bd[0], bd[1], a, b)); std::printf ("\n");
}


static bool gOk = true;
static void expectLE (const char* what, double v, double lim) { bool ok = v <= lim; std::printf ("   %-46s %8.2f  (max %.2f)  %s\n", what, v, lim, ok ? "OK" : "FALLA"); if (! ok) gOk = false; }
static void expectGE (const char* what, double v, double lim) { bool ok = v >= lim; std::printf ("   %-46s %8.2f  (min %.2f)  %s\n", what, v, lim, ok ? "OK" : "FALLA"); if (! ok) gOk = false; }

int main()
{
    const double sr = 44100;
    std::vector<Note> high = { { 57.30, 0.9 }, { 59.28, 0.8 }, { 60.65, 1.0 }, { 62.40, 0.8 }, { 64.30, 1.0 }, { 62.70, 0.7 }, { 60.33, 0.9 }, { 59.72, 0.9 } };
    std::vector<Note> low  = { { 43.30, 0.9 }, { 45.28, 0.8 }, { 47.35, 1.0 }, { 48.65, 0.8 }, { 45.70, 1.0 }, { 43.35, 0.9 }, { 47.65, 0.9 } };
    struct Sc { const char* name; const std::vector<Note>* notes; double rough; int type; bool ll; double maxErr, minWithin, minPer; };
    const Sc scs[] = {
        { "voz limpia ~220 Hz",            &high, 0.0, 1, false, 2.5, 98, 0.995 },
        { "voz ASPERA ~220 Hz",            &high, 1.0, 1, false, 6.0, 90, 0.90 },
        { "voz limpia GRAVE 98-131 Hz",    &low,  0.0, 0, false, 3.0, 97, 0.995 },
        { "voz ASPERA GRAVE",              &low,  1.0, 0, false, 5.0, 90, 0.93 },
        { "voz limpia ~220 Hz  (Low Lat.)", &high, 0.0, 1, true,  4.0, 95, 0.99 },
        { "voz limpia GRAVE    (Low Lat.)", &low,  0.0, 0, true,  4.0, 95, 0.99 },
    };
    const double bands[3][2] = { { 1500, 3500 }, { 3500, 6000 }, { 6000, 16000 } };
    for (auto& sc : scs)
    {
        gRough = sc.rough; gSteadyOnly = true;
        std::vector<double> tgt; auto x = singer (sr, *sc.notes, tgt, 5);
        gStarts.clear(); double acc = 0; for (auto& n : *sc.notes) { gStarts.push_back (acc); acc += n.dur; }
        int lat;
        auto y = runEngine<AutoTuneEngine> (x, sr, [&] (AutoTuneEngine::Params& p) { p.retune = 0; p.inputType = sc.type; p.lowLatency = sc.ll; for (int i = 0; i < 12; ++i) p.allowed[(size_t) i] = (i == 0 || i == 2 || i == 4 || i == 5 || i == 7 || i == 9 || i == 11); }, lat);
        auto r = evaluate (x, y, tgt, sr, lat);
        std::printf ("%s\n", sc.name);
        expectLE ("error de afinacion en notas sostenidas (cents)", r.errAbsCents, sc.maxErr);
        expectGE ("% de frames a menos de 10 cents", r.within10, sc.minWithin);
        expectGE ("periodicidad (calidad de la onda)", r.periodicity, sc.minPer);
        expectLE ("clics de mas respecto a la entrada", r.clicks, 4.0);
        if (sc.rough == 0.0)   // respuesta de agudos (solo voz limpia: en la aspera la propia entrada casi no tiene agudos)
        {
            std::vector<float> inAl (x.size(), 0.0f); for (size_t i = (size_t) lat; i < x.size(); ++i) inAl[i] = x[i - (size_t) lat];
            const size_t a = (size_t) (0.5 * sr), b = x.size() - (size_t) (0.2 * sr);
            for (int k = 0; k < 3; ++k) { char nm[64]; std::snprintf (nm, sizeof nm, "agudos %4.0f-%5.0f Hz (dB, min -1.5)", bands[k][0], bands[k][1]); expectGE (nm, bandDb (inAl, y, sr, bands[k][0], bands[k][1], a, b), -1.5); }
        }
    }
    std::printf ("\n%s\n", gOk ? "CANTANTE OK" : "HAY FALLAS");
    return gOk ? 0 : 1;
}
