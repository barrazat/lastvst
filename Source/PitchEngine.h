// Black Magic - Dungeon Estudio
// Motor de correccion de tono (estilo Auto-Tune clasico).
//
// Deteccion : YIN sobre senal decimada (cada kHop muestras)
// Cambio    : TD-PSOLA con marcas de tono seguidas por correlacion
//             (preserva formantes; "Throat" remuestrea cada grano)
// Lectura   : interpolacion sinc de 32 puntos (Kaiser) => sin perdida de agudos
//
// Este archivo NO depende de JUCE, solo de la libreria estandar.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bm
{
struct EngineParams
{
    float retune    = 0.0f;   // 0..100  (0 = instantaneo / robotico)
    int   key       = 0;      // 0..11   (C..B)
    int   scale     = 0;      // 0 chromatic, 1 major, 2 minor, 3 harm. minor, 4 pent. major, 5 pent. minor
    int   inputType = 1;      // 0 low male, 1 alto/tenor, 2 female
    float throat    = 50.0f;  // 0..100  (50 = neutro)
    bool  throatOn  = false;  // si esta apagado, los formantes no se tocan
    float tracking  = 50.0f;  // 0..100
    float humanize  = 0.0f;   // 0..100
};

class PitchEngine
{
public:
    using int64 = std::int64_t;

    PitchEngine()
    {
        for (auto& b : inBuf)  b.assign ((size_t) kInSize, 0.0f);
        for (auto& b : outAcc) b.assign ((size_t) kOutSize, 0.0f);
        winAcc.assign ((size_t) kOutSize, 0.0f);
        monoBuf.assign ((size_t) kInSize, 0.0f);
        decRing.assign ((size_t) kDecSize, 0.0f);
        yinWin.assign ((size_t) (2 * kMaxTau + 8), 0.0f);
        diffv.assign ((size_t) (kMaxTau + 8), 0.0f);
        cmnd.assign ((size_t) (kMaxTau + 8), 0.0f);
        buildSincTable();
        prepare (48000.0, 2, 1);
    }

    //==========================================================================
    void prepare (double sampleRate, int channels, int inputType)
    {
        fs          = sampleRate;
        nCh         = std::clamp (channels, 1, kMaxCh);
        inputType   = std::clamp (inputType, 0, 2);
        currentType = inputType;

        static const double fMinTab[3] = { 65.0, 90.0, 160.0 };
        static const double fMaxTab[3] = { 350.0, 700.0, 1100.0 };
        fMinHz = fMinTab[inputType];
        fMaxHz = fMaxTab[inputType];

        decim       = std::max (1, (int) std::lround (fs / (inputType == 2 ? 48000.0 : 24000.0)));
        fsDec       = fs / (double) decim;
        tauMinDec   = std::max (2, (int) std::floor (fsDec / fMaxHz));
        tauMaxDec   = std::min (kMaxTau, (int) std::ceil (fsDec / fMinHz) + 1);
        tMinSamples = fs / fMaxHz;
        tMaxSamples = fs / fMinHz;
        unvoicedT   = std::min (std::max (64.0, std::round (fs * 0.0055)), tMaxSamples);

        latency = (int) std::ceil (tMaxSamples * (1.0 / kPhiMin + 1.0 + 0.15)) + 48;
        reset();
    }

    void reset()
    {
        for (auto& b : inBuf)  std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : outAcc) std::fill (b.begin(), b.end(), 0.0f);
        std::fill (winAcc.begin(),  winAcc.end(),  0.0f);
        std::fill (monoBuf.begin(), monoBuf.end(), 0.0f);
        std::fill (decRing.begin(), decRing.end(), 0.0f);

        wIdx = 0; decWrite = 0; decAccum = 0.0f; decCount = 0; hopCounter = 0;
        nextC = 0.0; aPrev = 0.0;
        voiced = false; holdCount = 0; jumpCount = 0; jumpPeriod = 0.0;
        tinSmooth = unvoicedT; targetNote = -1; sOut = 0.0; heldTime = 0.0; needSnap = true;
        curT = curTout = curH = unvoicedT; curPhi = 1.0;
        for (auto& v : voteRing) v = -1;
        votePos = 0; stableNote = -1; centsAvg = 0.0;
        targetNoteOut.store (-1);
        centsOut.store (0.0f);
        hzOut.store (0.0f);
    }

    int  getLatency()   const noexcept { return latency; }
    int  getInputType() const noexcept { return currentType; }
    int  getTargetNote() const noexcept { return targetNoteOut.load (std::memory_order_relaxed); }
    // desviacion de la voz de entrada respecto a la nota objetivo, en cents (-50..+50)
    float getCents()     const noexcept { return centsOut.load (std::memory_order_relaxed); }
    // frecuencia detectada de la voz de entrada (Hz), 0 si no hay voz
    float getInputHz()   const noexcept { return hzOut.load (std::memory_order_relaxed); }

    //==========================================================================
    // Procesa en el sitio. channels[c][n]
    void process (float* const* channels, int numCh, int numSamples, const EngineParams& p)
    {
        params = p;
        scaleMask = kScaleMasks[std::clamp (params.scale, 0, 5)];
        const int chs = std::clamp (numCh, 1, nCh);

        for (int s = 0; s < numSamples; ++s)
        {
            const int64 i  = wIdx;
            const int   wi = (int) (i & kInMask);

            float mono = 0.0f;
            for (int c = 0; c < chs; ++c)
            {
                const float x = channels[c][s];
                inBuf[(size_t) c][(size_t) wi] = x;
                mono += x;
            }
            mono /= (float) chs;
            monoBuf[(size_t) wi] = mono;
            pushDecimated (mono);
            ++wIdx;

            if (++hopCounter >= kHop)
            {
                hopCounter = 0;
                analyse();
            }

            // indice de salida que toca emitir ahora
            const int64 m = i - (int64) latency;
            while (nextC - curH <= (double) m)
                synthGrain (i, m, chs);

            const int   oi   = (int) (m & kOutMask);
            const float norm = 1.0f / std::max (winAcc[(size_t) oi], 0.08f);
            for (int c = 0; c < chs; ++c)
            {
                channels[c][s] = outAcc[(size_t) c][(size_t) oi] * norm;
                outAcc[(size_t) c][(size_t) oi] = 0.0f;
            }
            winAcc[(size_t) oi] = 0.0f;
        }
    }

private:
    //==========================================================================
    static constexpr int    kMaxCh   = 2;
    static constexpr int    kInSize  = 1 << 16;
    static constexpr int    kInMask  = kInSize - 1;
    static constexpr int    kOutSize = 1 << 14;
    static constexpr int    kOutMask = kOutSize - 1;
    static constexpr int    kDecSize = 1 << 12;
    static constexpr int    kDecMask = kDecSize - 1;
    static constexpr int    kHop     = 128;
    static constexpr int    kTaps    = 32;
    static constexpr int    kPhases  = 512;
    static constexpr int    kMaxTau  = 1024;
    static constexpr int    kVotes   = 110;   // ~300 ms de votos
    static constexpr double kPhiMin  = 0.8408964152537145;   // 2^(-3/12)

    static constexpr int kScaleMasks[6] = { 0xFFF, 0xAB5, 0x5AD, 0x9AD, 0x295, 0x4A9 };

    //==========================================================================
    void pushDecimated (float mono)
    {
        decAccum += mono;
        if (++decCount >= decim)
        {
            decRing[(size_t) (decWrite & kDecMask)] = decAccum / (float) decim;
            ++decWrite;
            decAccum = 0.0f;
            decCount = 0;
        }
    }

    // YIN. Devuelve el periodo en muestras a la frecuencia de muestreo original.
    bool detectPeriod (double thr, double& periodFull)
    {
        const int W = tauMaxDec;
        const int N = W + tauMaxDec + 1;
        if (decWrite < (int64) N) return false;

        for (int j = 0; j < N; ++j)
            yinWin[(size_t) j] = decRing[(size_t) ((decWrite - N + j) & kDecMask)];

        double e = 0.0;
        for (int j = N - W; j < N; ++j) e += (double) yinWin[(size_t) j] * (double) yinWin[(size_t) j];
        if (e / (double) W < 4.0e-6) return false;   // silencio (< -54 dBFS)

        const float* x = yinWin.data();
        for (int tau = 1; tau <= tauMaxDec; ++tau)
        {
            const float* b = x + tau;
            float sum = 0.0f;
            for (int j = 0; j < W; ++j) { const float d = x[j] - b[j]; sum += d * d; }
            diffv[(size_t) tau] = sum;
        }

        cmnd[0] = 1.0f;
        float run = 0.0f;
        for (int tau = 1; tau <= tauMaxDec; ++tau)
        {
            run += diffv[(size_t) tau];
            cmnd[(size_t) tau] = run > 1.0e-12f ? diffv[(size_t) tau] * (float) tau / run : 1.0f;
        }

        int found = -1;
        int tau   = tauMinDec;
        while (tau < tauMaxDec)
        {
            if ((double) cmnd[(size_t) tau] < thr)
            {
                while (tau + 1 < tauMaxDec && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                found = tau;
                break;
            }
            ++tau;
        }
        if (found < 0) return false;

        double refined = (double) found;
        if (found > 1 && found < tauMaxDec)
        {
            const double s0 = cmnd[(size_t) found - 1], s1 = cmnd[(size_t) found], s2 = cmnd[(size_t) found + 1];
            const double den = 2.0 * (2.0 * s1 - s2 - s0);
            if (std::fabs (den) > 1.0e-9) refined += std::clamp ((s2 - s0) / den, -1.0, 1.0);
        }
        periodFull = refined * (double) decim;
        return true;
    }

    // Nota mostrada = la que mas veces salio en los ultimos ~300 ms (kVotes saltos de analisis).
    // Una nota nueva solo reemplaza a la actual cuando tiene claramente mas votos (>1.4x):
    // asi, en glissandos o transiciones, solo se muestra la nota donde "aterrizas".
    void pushVote (int n) { voteRing[votePos] = n; votePos = (votePos + 1) % kVotes; }

    int modeNote()
    {
        // sin voz en los ultimos ~55 ms => nada que mostrar (la interfaz hace el fade)
        bool recentVoice = false;
        for (int i = 1; i <= 20; ++i)
            if (voteRing[(votePos - i + kVotes) % kVotes] >= 0) { recentVoice = true; break; }
        if (! recentVoice) { stableNote = -1; return -1; }

        int counts[128] = {};
        int best = -1, bestCount = 0;
        for (int i = 0; i < kVotes; ++i)
        {
            const int n = voteRing[i];
            if (n < 0 || n > 127) continue;
            if (++counts[n] > bestCount) { bestCount = counts[n]; best = n; }
        }

        if (stableNote < 0 || counts[stableNote] == 0)
            stableNote = (bestCount >= 6) ? best : -1;
        else if (best != stableNote && (float) bestCount > 1.4f * (float) counts[stableNote])
            stableNote = best;
        return stableNote;
    }

    bool isAllowed (int note) const
    {
        const int rel = ((note - params.key) % 12 + 12) % 12;
        return ((scaleMask >> rel) & 1) != 0;
    }

    int chooseTarget (double midiIn, int prevTarget) const
    {
        const int centre = (int) std::lround (midiIn);
        int best = -1;
        double bestDist = 1.0e9;
        for (int n = centre - 7; n <= centre + 7; ++n)
        {
            if (! isAllowed (n)) continue;
            const double d = std::fabs ((double) n - midiIn);
            if (d < bestDist) { bestDist = d; best = n; }
        }
        if (best < 0) best = centre;

        // histeresis: no cambiar de nota por oscilaciones en el borde
        if (prevTarget >= 0 && best != prevTarget && isAllowed (prevTarget))
            if (std::fabs ((double) prevTarget - midiIn) < bestDist + 0.2)
                return prevTarget;
        return best;
    }

    //==========================================================================
    void analyse()
    {
        const double hopSec = (double) kHop / fs;
        const double trk    = std::clamp ((double) params.tracking / 100.0, 0.0, 1.0);
        const double thr    = 0.24 - 0.14 * trk;                       // umbral de YIN
        const int    holdMax = (int) std::lround (10.0 - 8.0 * trk);   // saltos tolerados sin tono
        const double follow = 0.25 + 0.75 * trk;                       // velocidad de seguimiento

        double period = 0.0;
        const bool found = detectPeriod (thr, period);

        if (found)
        {
            period    = std::clamp (period, tMinSamples, tMaxSamples);
            holdCount = 0;
            if (! voiced)
            {
                voiced = true; tinSmooth = period; sOut = 0.0; heldTime = 0.0;
                targetNote = -1; jumpCount = 0; needSnap = true;
            }
            else
            {
                const double lr = std::log2 (period / tinSmooth);
                if (std::fabs (lr) > 0.4)   // salto grande (posible error de octava): exigir persistencia
                {
                    if (jumpCount > 0 && std::fabs (std::log2 (period / jumpPeriod)) < 0.06) ++jumpCount;
                    else jumpCount = 1;
                    jumpPeriod = period;
                    if (jumpCount >= 4) { tinSmooth = period; jumpCount = 0; targetNote = -1; needSnap = true; }
                }
                else
                {
                    jumpCount = 0;
                    tinSmooth += follow * (period - tinSmooth);
                }
            }
        }
        else if (voiced)
        {
            if (++holdCount > holdMax) { voiced = false; holdCount = 0; }
        }

        if (voiced)
        {
            const double f0     = fs / tinSmooth;
            const double midiIn = 69.0 + 12.0 * std::log2 (f0 / 440.0);

            const int nt = chooseTarget (midiIn, targetNote);
            const bool noteChanged = (nt != targetNote);
            if (noteChanged) { targetNote = nt; heldTime = 0.0; }
            else             heldTime += hopSec;

            const double instCents = std::clamp ((midiIn - (double) targetNote) * 100.0, -50.0, 50.0);
            if (noteChanged) centsAvg = instCents;
            else             centsAvg += (1.0 - std::exp (-hopSec / 0.18)) * (instCents - centsAvg);

            const double sDes = std::clamp ((double) targetNote - midiIn, -7.0, 7.0);

            // Retune speed: 0 = salto instantaneo, 100 = ~0.5 s
            const double v   = std::clamp ((double) params.retune / 100.0, 0.0, 1.0);
            double       tau = 0.5 * v * v;
            // Humanize: las notas largas se corrigen mas despacio
            const double hum = std::clamp ((double) params.humanize / 100.0, 0.0, 1.0);
            tau += hum * std::min (1.0, heldTime / 0.5) * 0.40;

            const double alpha = tau < 1.0e-4 ? 1.0 : 1.0 - std::exp (-hopSec / tau);
            sOut += alpha * (sDes - sOut);

            curT    = tinSmooth;
            curTout = std::clamp (curT / std::pow (2.0, sOut / 12.0), 8.0, tMaxSamples * 2.0);
            curPhi  = params.throatOn ? std::pow (2.0, (std::clamp ((double) params.throat, 0.0, 100.0) - 50.0) / 50.0 * 0.25) : 1.0;
            curH    = curT / curPhi;
            pushVote (targetNote);
            targetNoteOut.store (modeNote(), std::memory_order_relaxed);
            hzOut.store ((float) f0, std::memory_order_relaxed);
            centsOut.store ((float) centsAvg, std::memory_order_relaxed);
        }
        else
        {
            targetNote = -1; sOut = 0.0; heldTime = 0.0;
            curT = curTout = curH = unvoicedT; curPhi = 1.0;
            pushVote (-1);
            targetNoteOut.store (modeNote(), std::memory_order_relaxed);
            hzOut.store (0.0f, std::memory_order_relaxed);
            centsOut.store (0.0f, std::memory_order_relaxed);
            centsAvg = 0.0;
        }
    }

    //==========================================================================
    // Busca el pico mas fuerte alrededor de 'centre' (marca de tono inicial)
    double snapToPeak (double centre, double radius) const
    {
        const int64 c0 = (int64) std::llround (centre);
        const int   R  = std::max (2, (int) radius);
        float best = -1.0f; int bestN = 0;
        for (int n = -R; n <= R; ++n)
        {
            const float v = std::fabs (monoBuf[(size_t) ((c0 + n - 1) & kInMask)])
                          + 2.0f * std::fabs (monoBuf[(size_t) ((c0 + n) & kInMask)])
                          + std::fabs (monoBuf[(size_t) ((c0 + n + 1) & kInMask)]);
            if (v > best) { best = v; bestN = n; }
        }
        return (double) (c0 + bestN);
    }

    // Ajusta el centro del siguiente grano por correlacion con el grano anterior
    double refineCenter (double aPrevious, double aPred) const
    {
        const int S = std::max (2, (int) (curT * 0.12));
        const int L = std::max (8, (int) (curT * 0.5));
        const int64 ref   = (int64) std::llround (aPrevious);
        const int64 cand0 = (int64) std::llround (aPred);

        double bestScore = -1.0e30; int bestD = 0;
        for (int d = -S; d <= S; ++d)
        {
            double num = 0.0, den = 1.0e-12;
            for (int n = -L; n <= L; ++n)
            {
                const double r = monoBuf[(size_t) ((ref + n) & kInMask)];
                const double v = monoBuf[(size_t) ((cand0 + d + n) & kInMask)];
                num += r * v;
                den += v * v;
            }
            const double score = num / std::sqrt (den);
            if (score > bestScore) { bestScore = score; bestD = d; }
        }
        return aPred + (double) bestD;
    }

    static inline float interp (const float* buf, int64 ip, const float* coef)
    {
        const int s = (int) ((ip - (kTaps / 2 - 1)) & kInMask);
        float acc = 0.0f;
        if (s + kTaps <= kInSize)
        {
            const float* x = buf + s;
            for (int t = 0; t < kTaps; ++t) acc += coef[t] * x[t];
        }
        else
        {
            for (int t = 0; t < kTaps; ++t) acc += coef[t] * buf[(s + t) & kInMask];
        }
        return acc;
    }

    void synthGrain (int64 latest, int64 emitIdx, int chs)
    {
        const double c   = nextC;
        const double T   = curT;
        const double rel = c - aPrev;
        const int    m   = rel > 0.0 ? (int) std::floor (rel / T) : 0;
        double a = aPrev + (double) m * T;

        if (voiced)
        {
            if (needSnap)      { a = snapToPeak (a - 0.5 * T, 0.5 * T); needSnap = false; }
            else if (m > 0)    { a = refineCenter (aPrev, a); }
        }
        else
        {
            a = c - std::round (c - a);   // offset entero => copia exacta, sin filtrado
            needSnap = true;
        }
        const double aMax = (double) latest - T - 24.0;
        if (a > aMax) a = aMax;
        aPrev = a;

        const int64  base = (int64) std::floor (c);
        const double fc   = c - (double) base;
        const double H    = curH;
        const double phi  = curPhi;
        const int    Hi   = (int) std::ceil (H) + 1;
        const double pi   = 3.14159265358979323846;

        for (int k = -Hi; k <= Hi; ++k)
        {
            const int64 idx = base + k;
            if (idx < emitIdx) continue;

            const double tau = (double) k - fc;
            const double tn  = tau / H;
            if (tn <= -1.0 || tn >= 1.0) continue;

            const float  w   = (float) (0.5 + 0.5 * std::cos (pi * tn));
            const double p   = a + phi * tau;
            const int64  ip  = (int64) std::floor (p);
            const int    row = (int) std::lround ((p - (double) ip) * (double) kPhases);
            const float* coef = sincTable.data() + (size_t) row * kTaps;

            const int oi = (int) (idx & kOutMask);
            for (int ch = 0; ch < chs; ++ch)
                outAcc[(size_t) ch][(size_t) oi] += w * interp (inBuf[(size_t) ch].data(), ip, coef);
            winAcc[(size_t) oi] += w;
        }
        nextC += curTout;
    }

    //==========================================================================
    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 60; ++k)
        {
            const double q = x / (2.0 * k);
            term *= q * q;
            sum  += term;
        }
        return sum;
    }

    void buildSincTable()
    {
        sincTable.assign ((size_t) (kPhases + 1) * kTaps, 0.0f);
        const double pi = 3.14159265358979323846;
        const double beta = 8.0, i0b = besselI0 (beta), halfW = (double) kTaps / 2.0;
        for (int r = 0; r <= kPhases; ++r)
        {
            const double f = (double) r / (double) kPhases;
            double tmp[kTaps]; double sum = 0.0;
            for (int t = 0; t < kTaps; ++t)
            {
                const double j = (double) (t - (kTaps / 2 - 1));
                const double x = f - j;
                const double s = std::fabs (x) < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
                const double u = x / halfW;
                const double w = std::fabs (u) < 1.0 ? besselI0 (beta * std::sqrt (1.0 - u * u)) / i0b : 0.0;
                tmp[t] = s * w; sum += tmp[t];
            }
            for (int t = 0; t < kTaps; ++t) sincTable[(size_t) r * kTaps + (size_t) t] = (float) (tmp[t] / sum);
        }
    }

    //==========================================================================
    EngineParams params;
    int scaleMask = 0xFFF;

    double fs = 48000.0;
    int    nCh = 2, currentType = 1, decim = 2, latency = 0;
    double fsDec = 24000.0, fMinHz = 90.0, fMaxHz = 700.0, tMinSamples = 68.0, tMaxSamples = 533.0, unvoicedT = 264.0;
    int    tauMinDec = 2, tauMaxDec = 200;

    std::vector<float> inBuf[kMaxCh], outAcc[kMaxCh];
    std::vector<float> winAcc, monoBuf, decRing, yinWin, diffv, cmnd, sincTable;

    int64 wIdx = 0, decWrite = 0;
    float decAccum = 0.0f; int decCount = 0, hopCounter = 0;

    double nextC = 0.0, aPrev = 0.0;
    double curT = 264.0, curTout = 264.0, curH = 264.0, curPhi = 1.0;
    bool   needSnap = true;

    bool   voiced = false;
    int    holdCount = 0, jumpCount = 0;
    double jumpPeriod = 0.0, tinSmooth = 264.0, sOut = 0.0, heldTime = 0.0;
    int    targetNote = -1;
    std::atomic<int>   targetNoteOut { -1 };
    std::atomic<float> centsOut { 0.0f };
    std::atomic<float> hzOut    { 0.0f };
    int voteRing[kVotes] = {};
    int votePos = 0, stableNote = -1;
    double centsAvg = 0.0;
};
} // namespace bm
