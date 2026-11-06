#pragma once
#include <cmath>
#include <cstdlib>

// Escalas, tonalidades y tipos de voz de BLACK MAGIC (sin dependencias de JUCE)
namespace blackmagic
{
    inline constexpr int kNumScales = 7;

    inline const char* const kKeyNames[12] =
        { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    inline const char* const kScaleNames[kNumScales] =
        { "Major", "Minor", "Chromatic", "Harmonic Minor", "Pentatonic Major", "Pentatonic Minor", "Blues" };

    // Notas permitidas relativas a la tonica (0 = tonica)
    inline const bool kScaleMasks[kNumScales][12] =
    {
        { 1,0,1,0,1,1,0,1,0,1,0,1 },   // Major
        { 1,0,1,1,0,1,0,1,1,0,1,0 },   // Minor (natural)
        { 1,1,1,1,1,1,1,1,1,1,1,1 },   // Chromatic
        { 1,0,1,1,0,1,0,1,1,0,0,1 },   // Harmonic Minor
        { 1,0,1,0,1,0,0,1,0,1,0,0 },   // Pentatonic Major
        { 1,0,0,1,0,1,0,1,0,0,1,0 },   // Pentatonic Minor
        { 1,0,0,1,0,1,1,1,0,0,1,0 }    // Blues
    };

    // Mascara ABSOLUTA de notas permitidas (indice 0 = C ... 11 = B)
    inline void buildAllowedMask (int key, int scale, bool* out12)
    {
        const int sc = scale < 0 ? 0 : (scale >= kNumScales ? kNumScales - 1 : scale);
        for (int pc = 0; pc < 12; ++pc)
            out12[pc] = kScaleMasks[sc][((pc - key) % 12 + 12) % 12];
    }

    // Nota MIDI (con decimales) permitida mas cercana
    inline double nearestAllowedMidi (double midi, const bool* allowed12)
    {
        const int base = (int) std::floor (midi);
        double best = std::round (midi), bestDist = 1.0e9;
        for (int cand = base - 6; cand <= base + 7; ++cand)
        {
            if (allowed12[((cand % 12) + 12) % 12])
            {
                const double dist = std::abs ((double) cand - midi);
                if (dist < bestDist) { bestDist = dist; best = (double) cand; }
            }
        }
        return best;
    }

    // Tipos de voz: rango de busqueda del detector de pitch
    inline constexpr int kNumInputTypes = 4;
    inline const char* const kInputTypeNames[kNumInputTypes] = { "Low Male", "Alto/Tenor", "Female", "Broadcast" };
    struct VoiceRange { double minHz, maxHz; };
    inline constexpr VoiceRange kVoiceRanges[kNumInputTypes] =
        { { 75.0, 350.0 }, { 100.0, 600.0 }, { 160.0, 1000.0 }, { 75.0, 1000.0 } };
}
