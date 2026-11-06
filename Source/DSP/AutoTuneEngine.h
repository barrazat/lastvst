#pragma once
#include <vector>
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "Scales.h"

// Motor de BLACK MAGIC:
//   1) Deteccion de pitch con YIN (sobre audio diezmado, barato en CPU).
//   2) Correccion con PSOLA (grains de 2 periodos, Hann). Al empezar cada tramo con voz el primer grain se
//      centra en el pulso de la voz (asi los grains que se solapan no se estorban y se conservan los
//      agudos); despues se avanza de periodo en periodo SIN volver a "buscar" el pulso (eso hacia oscilar
//      la afinacion).
//   3) Las decisiones (periodo, razon de afinacion, voz si/no) se guardan alineadas con el AUDIO al que
//      describen y se aplican cuando ese audio se procesa (el motor ya paga ~30 ms de latencia).
//   4) Division de bandas: por encima de 5.5 kHz el audio original pasa sin tocar.
//   5) Throat (opcional) = desplazamiento de formantes; al activarlo se procesa toda la banda.
//   6) Modo Low Latency: grains mas cortos y menos retardo, a cambio de algo de calidad.
// Sin dependencias de JUCE para poder probarlo por separado.
namespace blackmagic
{
    static inline int nextPow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }

    // Filtro de 2do orden (RBJ); dos en cascada con Q distintos = Butterworth de 4to orden
    struct Biquad
    {
        void setLowpass (double sr, double fc, double q = 0.70710678)
        {
            const double w0 = 2.0 * 3.14159265358979323846 * fc / sr, c = std::cos (w0), a = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + a;
            b0 = (float) ((1.0 - c) * 0.5 / a0); b1 = (float) ((1.0 - c) / a0); b2 = b0;
            a1 = (float) (-2.0 * c / a0);        a2 = (float) ((1.0 - a) / a0);
        }
        void setHighpass (double sr, double fc, double q = 0.70710678)
        {
            const double w0 = 2.0 * 3.14159265358979323846 * fc / sr, c = std::cos (w0), a = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + a;
            b0 = (float) ((1.0 + c) * 0.5 / a0); b1 = (float) (-(1.0 + c) / a0); b2 = b0;
            a1 = (float) (-2.0 * c / a0);        a2 = (float) ((1.0 - a) / a0);
        }
        void setHighShelf (double sr, double fc, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0), w0 = 2.0 * 3.14159265358979323846 * fc / sr;
            const double c = std::cos (w0), alpha = std::sin (w0) / 2.0 * std::sqrt (2.0);   // pendiente S = 1
            const double sA = 2.0 * std::sqrt (A) * alpha;
            const double a0 = (A + 1) - (A - 1) * c + sA;
            b0 = (float) (A * ((A + 1) + (A - 1) * c + sA) / a0);
            b1 = (float) (-2.0 * A * ((A - 1) + (A + 1) * c) / a0);
            b2 = (float) (A * ((A + 1) + (A - 1) * c - sA) / a0);
            a1 = (float) (2.0 * ((A - 1) - (A + 1) * c) / a0);
            a2 = (float) (((A + 1) - (A - 1) * c - sA) / a0);
        }
        void reset() { z1 = z2 = 0.0f; }
        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    };

    class YinDetector
    {
    public:
        void prepare (double rate, double minHz, double maxHz)
        {
            maxLag = (int) std::ceil (rate / minHz);
            minLag = std::max (2, (int) std::floor (rate / maxHz));
            W      = maxLag;
            cmnd.assign ((size_t) maxLag + 3, 1.0f);
        }

        int required()    const { return W + maxLag + 2; }
        int getMaxLag()   const { return maxLag; }
        int getMinLag()   const { return minLag; }

        // x[0] = muestra mas antigua. Busca solo entre minSel..maxSel.
        // Devuelve el periodo (fraccional, en muestras) o 0 si no hay voz.
        float detect (const float* x, int minSel, int maxSel, float threshold, float fallback)
        {
            minSel = std::max (minSel, 2);
            maxSel = std::min (maxSel, maxLag);
            if (maxSel <= minSel + 1) return 0.0f;

            double e = 0.0;
            for (int j = 0; j < W; ++j) e += (double) x[j] * x[j];
            if (std::sqrt (e / W) < 0.003) return 0.0f;           // silencio

            cmnd[0] = 1.0f;
            double running = 0.0;
            for (int tau = 1; tau <= maxSel + 1; ++tau)
            {
                double s = 0.0;
                const float* y = x + tau;
                for (int j = 0; j < W; ++j) { const float d = x[j] - y[j]; s += (double) d * d; }
                running += s;
                cmnd[(size_t) tau] = running > 0.0 ? (float) (s * tau / running) : 1.0f;
            }

            int est = -1;
            for (int tau = minSel; tau <= maxSel; ++tau)
            {
                if (cmnd[(size_t) tau] < threshold)
                {
                    while (tau + 1 <= maxSel && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                    est = tau;
                    break;
                }
            }
            if (est < 0)
            {
                float mn = 1.0e9f;
                for (int tau = minSel; tau <= maxSel; ++tau)
                    if (cmnd[(size_t) tau] < mn) { mn = cmnd[(size_t) tau]; est = tau; }
                if (mn > fallback) return 0.0f;
            }

            float better = (float) est;
            if (est > 1 && est <= maxSel)
            {
                const float s0 = cmnd[(size_t) est - 1], s1 = cmnd[(size_t) est], s2 = cmnd[(size_t) est + 1];
                const float denom = 2.0f * (2.0f * s1 - s2 - s0);
                if (std::abs (denom) > 1.0e-9f)
                    better += std::max (-1.0f, std::min (1.0f, (s2 - s0) / denom));
            }
            return better;
        }

    private:
        int minLag = 2, maxLag = 100, W = 100;
        std::vector<float> cmnd;
    };

    class AutoTuneEngine
    {
    public:
        struct Params
        {
            Params() { allowed.fill (true); }

            float retune   = 0.0f;     // 0..100  (0 = instantaneo / robotico)
            float humanize = 0.0f;     // 0..100  (deja pasar variacion natural en notas largas)
            bool  throatOn = false;    // Throat desactivado = maxima transparencia
            float throat   = 50.0f;    // 0..100  (50 = neutro; <50 garganta larga, >50 corta)
            float tracking = 50.0f;    // 0..100  (bajo = sensible/rapido, alto = tolerante/estable)
            int   inputType = 1;       // indice en kInputTypeNames
            bool  lowLatency = false;  // menos retardo (para grabar) a cambio de algo de calidad
            float mix      = 1.0f;     // 0..1
            std::array<bool, 12> allowed;   // notas permitidas (absolutas, 0 = C)
        };

        static constexpr double kMinHz = 75.0, kMaxHz = 1000.0;
        static constexpr double kThroatSemitones = 3.0;   // rango de +-3 semitonos de formante
        // Por encima de esta frecuencia el audio pasa SIN procesar (conserva el aire y el brillo original).
        static constexpr double kSplitHz = 5500.0;

        void prepare (double sampleRate)
        {
            sr     = sampleRate;
            decim  = std::max (1, (int) std::lround (sr / 22050.0));
            detRate = sr / decim;

            yin.prepare (detRate, kMinHz, kMaxHz);
            yinReq  = yin.required();
            detSize = nextPow2 (std::max (4096, 4 * yinReq));
            detMask = detSize - 1;
            detBuf.assign ((size_t) detSize, 0.0f);
            yinTmp.assign ((size_t) yinReq, 0.0f);
            hopDet     = std::max (16, (int) std::lround (0.006 * detRate));
            hopSeconds = (double) hopDet * decim / sr;

            maxH = yin.getMaxLag() * decim + decim;
            minH = std::max (16, (int) std::floor (sr / (kMaxHz * 1.25)));
            curLL = false; curType = 1;
            D    = computeLatency (curLL, curType);

            bufSize = nextPow2 (std::max (8192, 4 * computeLatency (false, 0)));
            mask    = bufSize - 1;
            for (auto* v : { &noteBuf, &envBuf, &inBuf, &outBuf, &outW, &dryL, &dryR, &hfL, &hfR, &tinBuf, &voicedBuf }) v->assign ((size_t) bufSize, 0.0f);
            pulseA = (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * 700.0 / sr));
            ratioBuf.assign ((size_t) bufSize, 1.0f);
            buildSincTable();

            const double fc = std::min (kSplitHz, sr * 0.45);
            for (Biquad* b : { &lpL1, &lpR1 }) b->setLowpass  (sr, fc, 0.5411961);
            for (Biquad* b : { &lpL2, &lpR2 }) b->setLowpass  (sr, fc, 1.3065630);
            for (Biquad* b : { &hpL1, &hpR1 }) b->setHighpass (sr, fc, 0.5411961);
            for (Biquad* b : { &hpL2, &hpR2 }) b->setHighpass (sr, fc, 1.3065630);

            // Limpieza de la rama corregida: PSOLA genera algo de ruido de agudos en los bordes de los grains;
            // por encima de ~8 kHz los agudos reales vienen intactos de la rama original.
            const double fp = std::min (8000.0, sr * 0.45);
            postLp1.setLowpass (sr, fp, 0.5411961); postLp2.setLowpass (sr, fp, 1.3065630);
            // Compensacion muy suave: en frases legato PSOLA aun pierde ~1 dB entre 1 y 5 kHz (medido con voces de prueba)
            wetShelf.setHighShelf (sr, 1200.0, 0.6);
            reset();
        }

        // Latencia (muestras). Normal: fija (cubre hasta la nota mas grave). Low Latency: depende del tipo de voz.
        int computeLatency (bool lowLat, int inputType) const
        {
            const int t = lowLat ? std::max (0, std::min (kNumInputTypes - 1, inputType)) : 0;
            const int maxHt = (int) std::ceil (detRate / kVoiceRanges[t].minHz) * decim + decim;
            return (int) std::ceil ((double) maxHt * (lowLat ? 1.55 : 2.3)) + 32;
        }

        void reset()
        {
            for (auto* v : { &noteBuf, &envBuf, &inBuf, &outBuf, &outW, &dryL, &dryR, &hfL, &hfR, &tinBuf, &voicedBuf, &detBuf }) std::fill (v->begin(), v->end(), 0.0f);
            std::fill (ratioBuf.begin(), ratioBuf.end(), 1.0f);
            for (Biquad* b : { &lpL1, &lpL2, &lpR1, &lpR2, &hpL1, &hpL2, &hpR1, &hpR2, &postLp1, &postLp2, &wetShelf }) b->reset();
            pulseLp = 0.0f; pulseEnv = 0.0f; prevRecVoiced = false; settledNote = -1; pendingNote = -1; pendingCount = 0; curNote = -1;
            writeIdx = 0; detWp = 0; decAcc = 0.0f; decCount = 0; hopCounter = 0; validUntil = -1;
            curTin = sr / 200.0; curRatio = 1.0; curAlpha = 1.0; currentShift = 0.0;
            nextOutCenter = 0.0; haveLastA = false; lastA = 0.0;
            voicedMix = 0.0f; inVoiced = false; unvoicedHops = 1000; mixSm = 1.0f; hfGain = 1.0f;
            heldNote = -1000; heldHops = 0;
            detMidi = -1.0f; tgtMidi = -1.0f;
            for (double& h : hist) h = 200.0;
        }

        int   getLatencySamples() const { return D; }
        float getDetectedMidi()   const { return detMidi; }   // -1 si no hay voz
        float getTargetMidi()     const { return tgtMidi; }

        // Se permite proceso "in place" (in == out). inR / outR pueden ser nullptr (mono).
        void process (const float* inL, const float* inR, float* outL, float* outR,
                      int n, const Params& p)
        {
            if (p.lowLatency != curLL || (p.lowLatency && p.inputType != curType))
            {
                curLL = p.lowLatency; curType = p.inputType;
                D = computeLatency (curLL, curType);
                haveLastA = false;                                   // resincronizar los grains
            }

            const double tauMs = p.retune <= 0.05f ? 0.0 : 2.0 * std::pow (200.0, p.retune / 100.0);
            const float mixTarget = std::max (0.0f, std::min (1.0f, p.mix));
            const float vmStep = (float) (1.0 / (0.004 * sr));
            const float hfStep = (float) (1.0 / (0.020 * sr));
            const float hfTarget = p.throatOn ? 0.0f : 1.0f;
            curAlpha = p.throatOn ? std::pow (2.0, ((double) p.throat - 50.0) / 50.0 * kThroatSemitones / 12.0) : 1.0;

            for (int i = 0; i < n; ++i)
            {
                const float l = inL[i];
                const float r = inR ? inR[i] : l;
                const float m = 0.5f * (l + r);

                // Division de bandas (Butterworth 4to orden): PSOLA trabaja con la banda baja/media;
                // los agudos pasan intactos y se suman al final, con el mismo retardo.
                const float lpl = lpL2.process (lpL1.process (l));
                const float lpr = lpR2.process (lpR1.process (r));
                const float hpl = hpL2.process (hpL1.process (l));
                const float hpr = hpR2.process (hpR1.process (r));
                hfGain += std::max (-hfStep, std::min (hfStep, hfTarget - hfGain));

                // Con Throat activo los agudos tambien pasan por PSOLA (hfGain -> 0), para que el timbre sea coherente.
                // Se usa (original - banda baja) y no el pasa-altos, asi con hfGain = 0 la entrada es exactamente la original.
                const float lpm = 0.5f * (lpl + lpr);
                const float src = lpm + (1.0f - hfGain) * (m - lpm);

                // Envolvente de "pulsos" (energia por encima de ~700 Hz): sirve para centrar el primer grain
                pulseLp += pulseA * (src - pulseLp);
                pulseEnv += 0.4f * (std::abs (src - pulseLp) - pulseEnv);

                const int64_t wi = writeIdx & mask;
                envBuf[(size_t) wi] = pulseEnv;
                inBuf[(size_t) wi] = src;
                dryL[(size_t) wi] = l; dryR[(size_t) wi] = r;
                hfL[(size_t) wi] = hpl; hfR[(size_t) wi] = hpr;
                ++writeIdx;

                decAcc += m;
                if (++decCount >= decim)
                {
                    detBuf[(size_t) (detWp & detMask)] = decAcc / (float) decim;
                    ++detWp; decAcc = 0.0f; decCount = 0;
                    if (++hopCounter >= hopDet) { hopCounter = 0; analyse (p, tauMs); }
                }

                const int64_t t = writeIdx - 1;
                for (int guard = 0; guard < 4; ++guard)
                {
                    const Rec r2 = recAt ((int64_t) std::llround (nextOutCenter) - D);   // decision del audio fuente
                    const int Hi = grainHalf (r2);
                    if (nextOutCenter - Hi > (double) t) break;
                    emitGrain (t, r2);
                }

                const size_t oi = (size_t) (t & mask);
                const float wsum = outW[oi];
                float wet = curLL ? outBuf[oi] / std::max (wsum, 0.3f) : outBuf[oi];
                outBuf[oi] = 0.0f; outW[oi] = 0.0f;

                // Limpieza de agudos de la rama corregida (solo cuando los agudos vienen de la rama original)
                const float wetLp = postLp2.process (postLp1.process (wet));
                wet = hfGain * wetLp + (1.0f - hfGain) * wet;
                wet = wetShelf.process (wet);

                const size_t di = (size_t) ((t - D) & mask);
                const float dl = dryL[di], dr = dryR[di];
                const float hl = hfGain * hfL[di], hr = hfGain * hfR[di];

                const float vt = recAt (t - D).voiced ? 1.0f : 0.0f;       // voz si/no del audio que sale ahora
                voicedMix += std::max (-vmStep, std::min (vmStep, vt - voicedMix));
                mixSm += 0.002f * (mixTarget - mixSm);

                const float yl = voicedMix * (wet + hl) + (1.0f - voicedMix) * dl;
                outL[i] = dl + mixSm * (yl - dl);
                if (outR)
                {
                    const float yr = voicedMix * (wet + hr) + (1.0f - voicedMix) * dr;
                    outR[i] = dr + mixSm * (yr - dr);
                }
            }
        }

    private:
        struct Rec { double tin, ratio; bool voiced; int note; };

        // Decision (periodo, razon de afinacion, voz si/no) que corresponde al instante 'srcTime' del audio de entrada.
        // Si ese instante aun no tiene estimacion se usa la mas reciente disponible.
        Rec recAt (int64_t srcTime) const
        {
            if (validUntil < 0) return { sr / 200.0, 1.0, false, -1 };
            const size_t k = (size_t) (std::min (srcTime, validUntil) & mask);
            const double tin = tinBuf[k] > 0.0f ? (double) tinBuf[k] : sr / 200.0;
            return { std::max ((double) minH, std::min ((double) maxH, tin)), (double) ratioBuf[k], voicedBuf[k] > 0.5f, (int) std::lround (noteBuf[k]) };
        }

        // Semi-ancho del grain (en muestras de salida)
        int grainHalf (const Rec& r) const
        {
            if (! curLL)
                return std::max (minH, std::min (maxH, (int) std::lround (r.tin)));
            const double tout = r.tin / r.ratio;
            return std::max (8, (int) std::ceil (std::max (0.62 * r.tin, 0.58 * tout)));
        }

        void analyse (const Params& p, double tauMs)
        {
            if (detWp < yinReq) return;
            const int64_t start = detWp - yinReq;
            for (int j = 0; j < yinReq; ++j) yinTmp[(size_t) j] = detBuf[(size_t) ((start + j) & detMask)];

            // Tracking: bajo = detector estricto y rapido; alto = tolerante y mas estable
            const double trk = std::max (0.0, std::min (1.0, (double) p.tracking / 100.0));
            const float thr      = (float) (0.06 + 0.18 * trk);
            const float fallback = std::min (0.45f, thr + 0.15f);
            const int   holdHops = 2 + (int) std::lround (trk * 6.0);
            const int   medLen   = trk < 0.4 ? 1 : (trk < 0.75 ? 3 : 5);

            const auto& vr = kVoiceRanges[std::max (0, std::min (kNumInputTypes - 1, p.inputType))];
            const int minSel = std::max (yin.getMinLag(), (int) std::floor (detRate / vr.maxHz));
            const int maxSel = std::min (yin.getMaxLag(), (int) std::ceil  (detRate / vr.minHz));

            const float tau = yin.detect (yinTmp.data(), minSel, maxSel, thr, fallback);
            if (tau > 0.0f)
            {
                lastTauDec = (double) tau;
                const double f0raw = sr / ((double) tau * decim);
                if (! inVoiced) for (double& h : hist) h = f0raw;
                else { for (int k = 0; k < 4; ++k) hist[k] = hist[k + 1]; hist[4] = f0raw; }

                double tmp[5]; for (int k = 0; k < medLen; ++k) tmp[k] = hist[5 - medLen + k];
                std::sort (tmp, tmp + medLen);
                const double f0 = tmp[medLen / 2];

                unvoicedHops = 0;
                curTin = std::max ((double) minH, std::min ((double) maxH, sr / f0));

                const double midi    = 69.0 + 12.0 * std::log2 (f0 / 440.0);
                const double target  = nearestAllowedMidi (midi, p.allowed.data());
                const double desired = target - midi;

                // Humanize: en notas largas la correccion se vuelve mas lenta (pasa el vibrato natural)
                const int tNote = (int) std::lround (target);
                curNote = tNote;
                if (! inVoiced || tNote != heldNote) { heldNote = tNote; heldHops = 0; }
                else ++heldHops;
                const double heldRamp = std::min (1.0, (double) heldHops * hopSeconds / 0.25);
                const double tauEff   = tauMs + (double) p.humanize / 100.0 * 300.0 * heldRamp;

                if (! inVoiced)
                {
                    currentShift = tauEff <= 0.0 ? desired : 0.0;
                    inVoiced = true;
                }
                else if (tauEff <= 0.0)
                    currentShift = desired;                                 // retune 0: salto instantaneo
                else
                    currentShift += (desired - currentShift) * (1.0 - std::exp (-hopSeconds * 1000.0 / tauEff));

                curRatio = std::max (0.5, std::min (2.0, std::pow (2.0, currentShift / 12.0)));
                detMidi = (float) midi; tgtMidi = (float) target;
            }
            else
            {
                unvoicedHops = std::min (unvoicedHops + 1, 1000);
                if (unvoicedHops >= holdHops) { inVoiced = false; detMidi = -1.0f; tgtMidi = -1.0f; }
            }

            // Instante del audio al que REALMENTE se refiere la estimacion. YIN compara x[j] con x[j + tau] para
            // j en [0, W): el audio efectivo es el principio de la ventana, de longitud W + tau (no la ventana
            // entera), asi que su centro esta a (W + tau)/2 del inicio. Ademas, la mediana de 'medLen'
            // estimaciones consecutivas retrasa el resultado (medLen - 1)/2 saltos.
            const double effCentreDec = (double) yinReq - ((double) yin.getMaxLag() + lastTauDec) * 0.5;      // desde "ahora", en muestras diezmadas
            const double medDelay     = 0.5 * (double) (medLen - 1) * (double) hopDet * (double) decim;
            const int64_t centre  = writeIdx - (int64_t) std::llround (effCentreDec * decim + medDelay);
            const int64_t hopHalf = (int64_t) hopDet * decim / 2;
            const int64_t newEnd  = centre + hopHalf - 1;

            // La decision se guarda en el tramo de audio al que se refiere (sin dejar huecos)
            if (newEnd > validUntil)
            {
                const int64_t from = validUntil < 0 ? newEnd - 2 * hopHalf : validUntil + 1;
                for (int64_t q = from; q <= newEnd; ++q)
                {
                    const size_t k = (size_t) (q & mask);
                    tinBuf[k] = (float) curTin; ratioBuf[k] = (float) curRatio; voicedBuf[k] = inVoiced ? 1.0f : 0.0f; noteBuf[k] = (float) curNote;
                }
                validUntil = newEnd;
            }
        }

        // Interpolacion sinc de 16 puntos (ventana Blackman): solo se usa cuando Throat esta activo
        static constexpr int kSincTaps = 16, kSincPhases = 256;

        void buildSincTable()
        {
            sincTable.assign ((size_t) (kSincPhases + 1) * kSincTaps, 0.0f);
            const double PI = 3.14159265358979323846;
            for (int ph = 0; ph <= kSincPhases; ++ph)
            {
                const double f = (double) ph / kSincPhases;
                double sum = 0.0; double w[kSincTaps];
                for (int j = 0; j < kSincTaps; ++j)
                {
                    const double d = (double) (j - (kSincTaps / 2 - 1)) - f;
                    const double x = d / (kSincTaps / 2);
                    const double sinc = std::abs (d) < 1e-9 ? 1.0 : std::sin (PI * d) / (PI * d);
                    const double win = std::abs (x) >= 1.0 ? 0.0 : 0.42 + 0.5 * std::cos (PI * x) + 0.08 * std::cos (2.0 * PI * x);
                    w[j] = sinc * win; sum += w[j];
                }
                for (int j = 0; j < kSincTaps; ++j) sincTable[(size_t) ph * kSincTaps + (size_t) j] = (float) (w[j] / sum);
            }
        }

        float readSinc (double pos) const
        {
            const int64_t i0 = (int64_t) std::floor (pos);
            const int ph = (int) std::lround ((pos - (double) i0) * kSincPhases);
            const float* h = &sincTable[(size_t) ph * kSincTaps];
            float acc = 0.0f;
            const int64_t base = i0 - (kSincTaps / 2 - 1);
            for (int j = 0; j < kSincTaps; ++j) acc += h[j] * inBuf[(size_t) ((base + j) & mask)];
            return acc;
        }

        void emitGrain (int64_t t, const Rec& rec)
        {
            if (rec.voiced && ! prevRecVoiced) { haveLastA = false; settledNote = rec.note; pendingNote = rec.note; pendingCount = 0; }   // empieza un tramo con voz: volver a alinear
            prevRecVoiced = rec.voiced;

            // Cambio de nota (legato): volver a alinear con el pulso. Debe mantenerse unos 6 grains (~50 ms)
            // para no reaccionar a una nota que "parpadea" entre dos opciones.
            if (rec.voiced && rec.note >= 0 && rec.note != settledNote)
            {
                if (rec.note == pendingNote) { if (++pendingCount >= 12) { haveLastA = false; settledNote = rec.note; pendingCount = 0; } }
                else { pendingNote = rec.note; pendingCount = 1; }
            }
            else pendingCount = 0;

            const double Tin = rec.tin;
            const int    Hi  = grainHalf (rec);
            const double Tout = Tin / rec.ratio;
            const double alpha = curAlpha;
            const bool   neutralThroat = std::abs (alpha - 1.0) < 1.0e-9;

            const int64_t ci = (int64_t) std::llround (nextOutCenter);
            const double nominal = nextOutCenter - (double) D;

            // Posicion de analisis: un numero entero de periodos despues del grain anterior (asi siguen en fase)
            double a;
            if (! haveLastA) a = nominal;
            else
            {
                double m;
                if (curLL) m = std::max (0.0, std::floor ((nominal + 0.15 * Tin - lastA) / Tin));   // nunca pasarse del tiempo nominal
                else       m = std::max (0.0, std::round ((nominal - lastA) / Tin));
                a = lastA + m * Tin;
                if (std::abs (a - nominal) > 2.0 * Tin) a = nominal;       // resincronizar
            }

            const int64_t newest = writeIdx - 1;
            const int64_t reach  = (int64_t) std::ceil ((double) Hi * alpha) + 10;

            // Primer grain de un tramo: centrarlo en el pulso mas fuerte cercano (+-medio periodo)
            if (! haveLastA)
            {
                const int S = std::max (3, (int) std::lround (0.5 * Tin));
                const int64_t c0 = (int64_t) std::llround (a);
                const int64_t lo = c0 - S, hi = std::min (c0 + S, newest - reach);
                if (hi >= lo)
                {
                    int64_t best = c0 < lo ? lo : (c0 > hi ? hi : c0); float bv = -1.0f;
                    for (int64_t c = lo; c <= hi; ++c) { const float v = envBuf[(size_t) (c & mask)]; if (v > bv) { bv = v; best = c; } }
                    a = (double) best;
                }
            }

            int64_t ai = (int64_t) std::llround (a);
            if (ai + reach > newest) { ai = newest - reach; a = (double) ai; }
            lastA = a; haveLastA = true;

            const float pi = 3.14159265358979f;
            const float gain = curLL ? 1.0f : (float) (Tout / (double) Hi);       // normal: Hann de 2 periodos
            const double edge = std::max (0.1 * Hi, std::min ((double) Hi, 2.0 * Hi - Tout));   // Low Latency: tope plano
            const double flat = (double) Hi - edge;
            for (int k = -Hi; k <= Hi; ++k)
            {
                const int64_t oi = ci + k;
                if (oi < t) continue;
                float w;
                if (! curLL) w = 0.5f * (1.0f + std::cos (pi * (float) k / (float) Hi));
                else
                {
                    const double ak = (double) std::abs (k);
                    w = ak <= flat ? 1.0f : 0.5f * (1.0f + std::cos (pi * (float) ((ak - flat) / edge)));
                }

                // Con Throat neutro se copian las muestras tal cual (sin interpolar = sin perdida de agudos);
                // si no, el grain se lee a velocidad 'alpha' con interpolacion sinc -> mueve los formantes
                const float smp = neutralThroat ? inBuf[(size_t) ((ai + k) & mask)]
                                                : readSinc ((double) ai + (double) k * alpha);
                outBuf[(size_t) (oi & mask)] += gain * w * smp;
                outW  [(size_t) (oi & mask)] += w;
            }
            nextOutCenter += Tout;
        }

        double sr = 44100.0, detRate = 22050.0;
        int decim = 2, yinReq = 0, detSize = 0, hopDet = 0, hopCounter = 0, decCount = 0;
        int maxH = 0, minH = 16, D = 0, bufSize = 0;
        int64_t mask = 0, detMask = 0, writeIdx = 0, detWp = 0, validUntil = -1;
        double hopSeconds = 0.006;
        float decAcc = 0.0f;
        bool curLL = false; int curType = 1;
        double lastTauDec = 100.0;

        YinDetector yin;
        std::vector<float> noteBuf, envBuf, detBuf, yinTmp, inBuf, outBuf, outW, dryL, dryR, hfL, hfR, tinBuf, ratioBuf, voicedBuf, sincTable;
        Biquad lpL1, lpL2, lpR1, lpR2, hpL1, hpL2, hpR1, hpR2, postLp1, postLp2, wetShelf;
        float hfGain = 1.0f, pulseA = 0.1f, pulseLp = 0.0f, pulseEnv = 0.0f;
        bool prevRecVoiced = false;
        int curNote = -1, settledNote = -1, pendingNote = -1, pendingCount = 0;

        double curTin = 220.0, curRatio = 1.0, curAlpha = 1.0, currentShift = 0.0;
        double nextOutCenter = 0.0, lastA = 0.0;
        bool haveLastA = false, inVoiced = false;
        int unvoicedHops = 1000, heldNote = -1000, heldHops = 0;
        float voicedMix = 0.0f, mixSm = 1.0f;
        float detMidi = -1.0f, tgtMidi = -1.0f;
        double hist[5] = { 200.0, 200.0, 200.0, 200.0, 200.0 };
    };
}
