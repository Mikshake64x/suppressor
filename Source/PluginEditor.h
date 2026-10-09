#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SuppressorLookAndFeel.h"
#include "GRMeter.h"
#include "RatioSelector.h"
#include "PowerRocker.h"
#include "ToggleSwitch.h"
#include "DetectorFilterGraph.h"
#include "NeedleVU.h"
#include "NeedleModeSelector.h"
#include "ThdValveModule.h"
#include "AboutOverlay.h"
#include "RegistrationOverlay.h"
#include "KnobSlider.h"
#include "AiLearnButton.h"
#include "AiSettingsButton.h"
#include "OptionsOverlay.h"
#include "ParamRamper.h"
#include "ABSwitch.h"

struct KnobControl
{
    KnobSlider   slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/**
    Fenetre a TAILLE FIXE (1400x300, 2U -- plaque principale en haut,
    plaque VU/THD Valve en dessous) -- pas de redimensionnement.

    POWER : plus d'overlay rectangulaire -- chaque composant (knobs,
    ToggleSwitch, RatioSelector, DetectorFilterGraph, NeedleVU,
    ThdValveModule) interroge lookAndFeel.isPowered() dans son propre
    paint() et se rend lui-meme "eteint", comme une machine debranchee.
    Voir timerCallback().
*/
class SuppressorAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SuppressorAudioProcessorEditor (SuppressorAudioProcessor&);
    ~SuppressorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void setupKnob (KnobControl& knob, const juce::String& paramID, const juce::String& labelText,
                    const juce::String& unitSuffix, int decimalPlaces);
    void rebindKnobsForMode (int mode);
    void rebuildBackgroundCache();
    void timerCallback() override;
    void applyAiLearnResult (const AiLearnAnalyzer::Result& result);
    void setParamValue (const juce::String& paramID, float rawValue);
    void rampParamTo (const juce::String& paramID, float rawValue, int durationMs = 260);
    void showOptionsOverlay();

    SuppressorAudioProcessor& processorRef;
    SuppressorLookAndFeel lookAndFeel;

    // BYPASS (sous le logo Distressor) : momentane -- pas de
    // ButtonAttachment, pilote directement via l'etat "isDown" du bouton
    // (voir onStateChange dans le constructeur). setClickingTogglesState
    // (false) pour que le clic ne "verrouille" pas l'etat.
    ToggleSwitch bypassButton { "BYPASS", SuppressorColours::ledRed };

    // A/B : bascule persistante entre 2 jeux de reglages (voir
    // SuppressorAudioProcessor::toggleABCompare()/selectABSlot()) -- pas de
    // ButtonAttachment (etat gere par le processeur, pas un parametre
    // APVTS). Colle sous BYPASS, meme colonne de gauche.
    ABSwitch abButton;

    // AI (au-dessus du logo) + engrenage (ouvre OptionsOverlay, section AI).
    AiLearnButton aiLearnButton { processorRef };
    AiSettingsButton aiSettingsButton;
    OptionsOverlay optionsOverlay;
    ParamRamper knobRamper;

    // OPTICAL et BRITISH sont mutuellement exclusifs (logique manuelle
    // dans le constructeur -- pas de radioGroupId JUCE, qui empeche de
    // rallumer le bouton actif pour revenir au compresseur normal).
    // Libelle UI "OVERKILL" (le parametre/ID internes idBritish restent
    // inchanges pour ne pas casser l'automation/les presets existants).
    ToggleSwitch opticalButton { "OPTICAL", SuppressorColours::ledAmber };
    ToggleSwitch britishButton { "OVERKILL", SuppressorColours::ledRed };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> opticalAttachment, britishAttachment;

    // Comp/Expand (a gauche d'OPTICAL) : cycle au clic (COMP -> EXPAND ->
    // COMP...), meme mecanique que ratioSelector (ParameterAttachment
    // manuel, pas de ButtonAttachment -- 2 etats a faire cycler
    // manuellement). La diode reste toujours allumee, seule sa couleur
    // indique le mode actif (bleu = Comp, violet = Expand). Voir
    // rebindKnobsForMode().
    ToggleSwitch aboveBelowButton { "COMP", SuppressorColours::ledBlue };
    std::unique_ptr<juce::ParameterAttachment> aboveBelowAttachment;

    // Requis EN PLUS du routage du bus Sidechain dans le DAW (voir
    // PluginProcessor::idSidechainEnable) -- certains hotes laissent ce
    // bus actif avec du silence sans jamais le desactiver vraiment.
    ToggleSwitch sidechainToggle { "SC", SuppressorColours::ledCyan };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sidechainAttachment;

    GRMeter grMeter { processorRef.grMeterDb, processorRef.grMeterExpandDb,
                      *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idPower),
                      *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAboveBelow) };

    RatioSelector ratioSelector;
    std::unique_ptr<juce::ParameterAttachment> ratioAttachment;

    ToggleSwitch detectorHpToggle   { "HP",   SuppressorColours::ledGreen };
    ToggleSwitch detectorBellToggle { "BELL", SuppressorColours::ledYellow };
    ToggleSwitch detectorLinkToggle { "LINK", SuppressorColours::ledRed };
    ToggleSwitch audioHpToggle      { "HP",     SuppressorColours::ledBlue };
    // Libelles UI "TUBE"/"FUZZ" (ID internes idAudioDist2/3 inchanges).
    ToggleSwitch audioDist2Toggle   { "TUBE", SuppressorColours::ledAmber };
    ToggleSwitch audioDist3Toggle   { "FUZZ", SuppressorColours::ledAmber };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        detectorHpAttachment, detectorBellAttachment, detectorLinkAttachment,
        audioHpAttachment, audioDist2Attachment, audioDist3Attachment;

    DetectorFilterGraph filterGraph { *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idDetectorHp),
                                       *processorRef.apvts.getParameter (SuppressorAudioProcessor::idDetectorHpFreq),
                                       *processorRef.apvts.getParameter (SuppressorAudioProcessor::idDetectorHpSlope),
                                       *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idDetectorBell),
                                       *processorRef.apvts.getParameter (SuppressorAudioProcessor::idDetectorBellFreq),
                                       *processorRef.apvts.getParameter (SuppressorAudioProcessor::idDetectorBellGain) };

    KnobControl inputKnob, attackKnob, releaseKnob, outputKnob;

    // Expandeur (idInputExpand="Range", idAttackExpand) : rack du bas,
    // toujours visibles -- Range est TOUJOURS actif quel que soit le mode
    // Comp/Expand du haut (voir PluginProcessor::updateDspFromParams,
    // effRangeDb), donc le cacher derriere aboveBelowButton etait genant.
    // Meme dimension EXACTE que les knobs du haut (voir setupKnob/resized()).
    KnobControl rangeExpandKnob, attackExpandKnob;

    PowerRocker powerButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;

    // Plaque du bas, divisee en deux : VU a aiguille (gauche) + THD Valve
    // (droite, port du scope/barre M76 pilote par le SoftClipper existant).
    // Chaque cadran (gauche=canal L, droite=canal R) a son PROPRE
    // selecteur de mode independant (GR/IN/OUT/THD de SON canal).
    // Mode initial repris du processeur (survit a la fermeture/reouverture
    // de l'editeur et a une session sauvegardee -- voir PluginProcessor).
    NeedleVU needleVu1 { processorRef, NeedleVU::modeForIndex (processorRef.needleModeIndex1), NeedleVU::Channel::Left };
    NeedleVU needleVu2 { processorRef, NeedleVU::modeForIndex (processorRef.needleModeIndex2), NeedleVU::Channel::Right };
    NeedleModeSelector needleVu1Modes { "L", processorRef.needleModeIndex1 };
    NeedleModeSelector needleVu2Modes { "R", processorRef.needleModeIndex2 };

    ThdValveModule thdValve { processorRef.clipMeterAmount,
                              *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idSoftClip) };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> softClipAttachment;

    AboutOverlay aboutOverlay;

    // Mur d'activation (nom + email, une seule fois -- Suppressor est gratuit) :
    // dernier enfant de l'editeur, peint par-dessus tout et avale les clics
    // tant que l'utilisateur ne s'est pas inscrit.
    RegistrationOverlay registrationOverlay { processorRef.freeRegistration.get() };

    juce::Rectangle<float> panelBounds;      // plaque principale (haut, 1U)
    juce::Rectangle<float> secondPlateBounds; // plaque de mesure (bas, 1U)
    juce::Image backgroundCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuppressorAudioProcessorEditor)
};
