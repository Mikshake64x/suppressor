#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"

/**
    GRMeter
    -------
    Rangee horizontale de petites LEDs degradees (cyan -> vert -> jaune -> orange -> rouge)
    indiquant la reduction de gain, avec une echelle en dB affichee en dessous.
    Attaque instantanee, retombee douce (ballistique type VU).

    Rangee EXPAND dediee : fine bande secondaire au-dessus des diodes
    principales, TOUJOURS visible, alimentee par un compteur SEPARE
    (expandGrValueDb) qui ne recoit QUE la partie boost de l'enveloppe
    (jamais la reduction) -- une diode = +0.1dB d'expansion, degrade
    bleu -> cyan -> violet (voir SuppressorLookAndFeel::ledGradientColourExpand()),
    delibrement distinct de la rangee compression ci-dessous qui, elle, ne
    change jamais de sens quel que soit Comp/Expand.
*/
class GRMeter : public juce::Component, private juce::Timer
{
public:
    GRMeter (std::atomic<float>& grValueDbToWatch, std::atomic<float>& expandGrValueDbToWatch,
             std::atomic<float>& powerParamToWatch, std::atomic<float>& aboveBelowParamToWatch);
    ~GRMeter() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Echelle non-lineaire : ces reperes dB sont espaces EGALEMENT en
    // pixels (pas leur valeur reelle) -- resolution fine pres de 0dB,
    // plus large au-dela de 10dB, comme un vrai VU/GR meter hardware.
    // dbToFraction() convertit une valeur reelle de reduction (continue)
    // en fraction 0-1 le long de cette meme echelle, pour que la diode
    // allumee corresponde toujours exactement au repere imprime.
    static constexpr float scaleMarksDb[] = { 0.0f, 2.0f, 4.0f, 6.0f, 8.0f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f };
    static constexpr int numScaleMarks = 10;
    static float dbToFraction (float db) noexcept;

    // Rangee EXPAND dediee -- une diode = 0.1dB, MEME TAILLE/nombre de
    // diodes que la rangee compression (36) -- kExpandLedDb * kNumExpandLeds
    // = plage totale couverte avant saturation (au-dela, la rangee reste
    // pleine, elle ne "deborde" pas).
    static constexpr float kExpandLedDb = 0.1f;
    static constexpr int kNumExpandLeds = 36;

    std::atomic<float>& grValueDb;
    std::atomic<float>& expandGrValueDb;
    std::atomic<float>& powerParam;
    std::atomic<float>& aboveBelowParam;
    bool powered = true;
    float displayedGrDb = 0.0f;
    float displayedExpandGrDb = 0.0f;
    bool isExpandMode = false; // true seulement en Expand pur (idAboveBelow == 1)
    float maxRangeDb = 30.0f;

    // Optimisation CPU : on ne redessine que si l'affichage a reellement
    // change (valeur OU etat power/mode), au lieu de repaint() inconditionnel
    // a chaque tick -- evite de faire tourner le rendu (degrades, chemins)
    // pour rien quand le signal est silencieux/stable.
    float lastPaintedGrDb = -1.0f;
    float lastPaintedExpandGrDb = -1.0f;
    bool lastPaintedPowered = true;
    bool lastPaintedIsExpand = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GRMeter)
};
