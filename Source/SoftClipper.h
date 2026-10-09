#pragma once
#include <JuceHeader.h>

/**
    SoftClipper
    -----------
    Etage de fin de chaine.

    Bouton LIMIT desactive (par defaut) : saturation douce (tanh) pure,
    AUCUN plafond artificiel -- le signal peut depasser 0dBFS librement,
    comme le softclip de l'Arousor/Distressor qui n'a pas de brickwall
    numerique strict.

    Bouton LIMIT active : brickwall fixe a -0.5dBFS, avec une protection
    "true clip" inspiree du Glue Compressor d'Ableton : on estime le pic
    INTER-echantillon par interpolation lineaire et on limite aussi ce pic
    estime, pas seulement l'echantillon brut, pour eviter les depassements
    caches qui n'apparaissent qu'apres reconstruction D/A.
*/
class SoftClipper
{
public:
    void setDriveDb (float db) noexcept
    {
        driveGain = juce::Decibels::decibelsToGain (db);
    }

    void setHardLimitEnabled (bool shouldLimit) noexcept { hardLimit = shouldLimit; }

    void reset() noexcept
    {
        clipAmount = 0.0f;
        prevY = 0.0f;
    }

    inline float processSample (float input) noexcept
    {
        const float x = input * driveGain;
        float z;

        if (hardLimit)
        {
            // --- Mode LIMIT : brickwall fixe a -0.5dBFS + protection true-peak ---
            const float clippedX = juce::jlimit (-ceilingLin, ceilingLin, x);

            // Estimation du pic inter-echantillon (interpolation lineaire a mi-chemin)
            const float interSamplePeak = juce::jmax (std::abs (clippedX), std::abs ((clippedX + prevY) * 0.5f));

            float limited = clippedX;
            if (interSamplePeak > ceilingLin)
                limited = clippedX * (ceilingLin / interSamplePeak);

            prevY = limited;
            z = limited / driveGain;
        }
        else
        {
            // --- Mode SOFT CLIP : tanh pur, AUCUN plafond artificiel ---
            // Le signal peut depasser 0dBFS librement si on le pousse fort,
            // comme sur l'Arousor/Distressor de reference (verifie via
            // PluginDoctor : son softclip n'a pas de brickwall numerique).
            const float y = std::tanh (x);
            prevY = y; // continuite si on bascule sur LIMIT au sample suivant
            z = y / driveGain;
        }

        // Intensite visuelle du clip (pour les diodes de l'UI)
        const float diff = std::abs (input) - std::abs (z);
        const float target = juce::jlimit (0.0f, 1.0f, diff * 2.5f);
        clipAmount = target > clipAmount ? target : clipAmount * 0.995f;

        return z;
    }

    float getClipAmount() const noexcept { return clipAmount; }

private:
    float driveGain  = 1.0f;
    float clipAmount = 0.0f;
    bool  hardLimit    = false;
    float prevY         = 0.0f;
    float ceilingLin     = juce::Decibels::decibelsToGain (-0.5f); // fixe, plus de knob separe
};
