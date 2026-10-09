#pragma once
#include <JuceHeader.h>
#include "FeedbackCompressor.h"
#include "SoftClipper.h"
#include "AiLearnAnalyzer.h"
#include "FreeRegistration.h"

/**
    SuppressorAudioProcessor
    -------------------------
    UNE seule unite stereo (pas 2 unites mono empilees) : un unique jeu de
    controles (Input/Ratio/Attack/Release/Output/Detector/Audio/British/
    Power) pilote 2 chaines DSP internes (gauche/droite), avec 2
    mecanismes de liaison stereo independants -- voir FeedbackCompressor.h :
    - Detector Link (bit du bouton DETECTOR) : detection partagee
    - Stereo Image Link (interrupteur dedie) : reduction de gain partagee

    Valeurs de ratio, plages Attack/Release, frequences Detector/Audio HP
    et Band Emphasis alignees sur le manuel Empirical Labs EL8 Distressor.
*/
class SuppressorAudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    SuppressorAudioProcessor();
    ~SuppressorAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override                        { return 1; }
    int getCurrentProgram() override                     { return 0; }
    void setCurrentProgram (int) override                {}
    const juce::String getProgramName (int) override     { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMETERS", createParameterLayout() };

    // Suppressor est GRATUIT : pas de cle de licence, mais une inscription
    // nom + email a la 1ere utilisation (RegistrationOverlay), remontee au
    // serveur SideKey pour apparaitre dans /admin. Partage par toutes les
    // instances du processus (voir FreeRegistration.h).
    juce::SharedResourcePointer<FreeRegistration> freeRegistration;

    // --- Lu par le GUI (thread-safe) ---
    std::atomic<float> grMeterDb { 0.0f }; // combine (max L/R) -- GRMeter (plaque du haut)

    // Boost Expand (max L/R), PAR OPPOSITION a grMeterDb ci-dessus qui ne
    // recoit que la partie REDUCTION -- alimente la rangee dediee du
    // GRMeter (voir GRMeter.h).
    std::atomic<float> grMeterExpandDb { 0.0f };

    // --- VU a aiguille (plaque du bas) : 2 cadrans dedies L/R, chacun son
    // propre selecteur de mode independant (voir PluginEditor) -- niveaux
    // crete par bloc, pas de lissage cote DSP, l'aiguille fait elle-meme
    // le ressort-amorti (voir NeedleVU.h), comme dans M76. ---
    std::atomic<float> inputLevelDbL  { -100.0f }, inputLevelDbR  { -100.0f };
    std::atomic<float> outputLevelDbL { -100.0f }, outputLevelDbR { -100.0f };
    std::atomic<float> grMeterDbL     { 0.0f },    grMeterDbR     { 0.0f };
    std::atomic<float> clipMeterAmountL { 0.0f },  clipMeterAmountR { 0.0f };

    // Mode selectionne par chaque NeedleModeSelector (0=GR 1=IN 2=OUT
    // 3=THD) -- pur choix d'affichage UI (n'affecte pas le DSP), mais doit
    // survivre a la fermeture/reouverture de l'editeur ET a une session
    // sauvegardee (voir getStateInformation/setStateInformation) -- vit
    // ici plutot que dans l'editeur, qui est detruit/recree a chaque fois.
    int needleModeIndex1 = 0, needleModeIndex2 = 0; // GR par defaut au lancement

    // --- THD Valve (plaque du bas) : intensite de saturation courante
    // combinee (0..1), 0 si SOFTCLIP est desengage -- pilote le scope + la
    // barre. ---
    std::atomic<float> clipMeterAmount { 0.0f };

    // BYPASS (bouton sous le logo Distressor) : maintien enfonce -- PAS un
    // parametre APVTS (pas d'automation/sauvegarde, comme sur Tripressor),
    // simple etat momentane lu par processBlock(). Voir aussi
    // FeedbackCompressor::relaxGainReductionToZero().
    std::atomic<bool> bypassHeld { false };

    // --- AI Learn (bouton AI, au-dessus du logo) ---
    AiLearnAnalyzer aiLearnAnalyzer;

    void startAiLearn()
    {
        const int compressionModeIndex = aiCompressionModeParam != nullptr ? (int) std::round (aiCompressionModeParam->load()) : 1;
        const int attackReleaseCharIndex = aiAttackReleaseCharacterParam != nullptr ? (int) std::round (aiAttackReleaseCharacterParam->load()) : 1;
        const int gainReductionIndex = aiGainReductionModeParam != nullptr ? (int) std::round (aiGainReductionModeParam->load()) : 1;
        const bool scopeDetectors   = aiScopeDetectorsParam   != nullptr && aiScopeDetectorsParam->load()   > 0.5f;
        const bool scopeCompression = aiScopeCompressionParam == nullptr || aiScopeCompressionParam->load() > 0.5f;
        const int styleIndex = aiStyleParam != nullptr ? (int) std::round (aiStyleParam->load()) : 0;
        const int targetIndex = aiMasteringTargetParam != nullptr ? (int) std::round (aiMasteringTargetParam->load()) : 0;
        aiLearnAnalyzer.beginCapture (getSampleRate(), targetIndex, compressionModeIndex, attackReleaseCharIndex,
                                       gainReductionIndex, scopeDetectors, scopeCompression, styleIndex);
    }

    // --- A/B (bouton sous BYPASS, voir ABSwitch.h) : bascule persistante
    // entre 2 jeux de reglages independants -- PAS un parametre APVTS
    // (l'etat "slot A ou B actif" n'est pas automatisable), mais chaque
    // slot est bien sauvegarde en session (voir getStateInformation/
    // setStateInformation). Seuls les parametres de SON (voir
    // getABParamIds()) sont concernes -- Power, AI et Bypass restent des
    // reglages globaux/utilitaires, hors A/B (meme logique que Tripressor).
    void toggleABCompare()
    {
        auto& activeSlot = usingSlotB ? abSlotB : abSlotA;

        for (auto& id : getABParamIds())
            if (auto* p = apvts.getParameter (id))
                activeSlot[id] = p->getValue();

        usingSlotB = ! usingSlotB;
        auto& targetSlot = usingSlotB ? abSlotB : abSlotA;

        // Premier passage sur ce slot : jamais touche encore -- on le
        // demarre identique au slot qu'on quitte plutot que silence/
        // defauts (sinon "A" et "B" sonneraient differemment des le
        // premier clic, sans que l'utilisateur ait rien change).
        if (targetSlot.empty())
            targetSlot = activeSlot;

        for (auto& [id, value] : targetSlot)
            if (auto* p = apvts.getParameter (id))
                p->setValueNotifyingHost (value);
    }

    void selectABSlot (bool wantB) { if (wantB != usingSlotB) toggleABCompare(); }
    bool isUsingSlotB() const noexcept { return usingSlotB; }

    // Identifiants de parametres
    static constexpr const char* idInput               = "input";
    static constexpr const char* idRatioIndex          = "ratioIndex";
    static constexpr const char* idAttack              = "attack";
    static constexpr const char* idRelease             = "release";
    static constexpr const char* idOutput              = "output";

    // Reglages Input(Range)/Ratio/Attack/Release DEDIES au mode EXPAND --
    // meme role/plage que idInput/idRatioIndex/idAttack/idRelease, mais lus
    // par le DSP UNIQUEMENT quand idAboveBelow == Expand (voir
    // updateDspFromParams()). Comp continue d'utiliser les reglages
    // Compression ci-dessus. Les knobs/RatioSelector sont RE-attaches a ce
    // jeu de parametres quand on bascule sur Expand (voir PluginEditor::
    // rebindKnobsForMode()). Output reste PARTAGE entre Comp et Expand (pas
    // de idOutputExpand) -- les deux branches tournant TOUJOURS ensemble
    // (voir FeedbackCompressor::AboveBelowMode::Both), un seul makeup gain
    // a du sens.
    static constexpr const char* idInputExpand      = "inputExpand"; // reaffecte en "Range" cote UI
    static constexpr const char* idRatioIndexExpand = "ratioIndexExpand";
    static constexpr const char* idAttackExpand     = "attackExpand";
    static constexpr const char* idReleaseExpand    = "releaseExpand";

    // DETECTOR et AUDIO : un bouton par fonction (clic direct, pas de
    // defilement -- le cycle "un bouton qui avance" ne marche pas bien en
    // contexte VST/automation). Chaque fonction est son propre bool,
    // combinable librement.
    static constexpr const char* idDetectorHp   = "detectorHp";
    static constexpr const char* idDetectorHpFreq  = "detectorHpFreq";   // 20-2000Hz, pilote par le drag X
    static constexpr const char* idDetectorHpSlope = "detectorHpSlope"; // 1-8 (x6dB/oct), pilote par le drag Y
    static constexpr const char* idDetectorBell = "detectorBell";
    static constexpr const char* idDetectorBellFreq = "detectorBellFreq"; // 20-2000Hz, drag X
    static constexpr const char* idDetectorBellGain = "detectorBellGain"; // 0-18dB, drag Y
    static constexpr const char* idDetectorLink = "detectorLink";
    // Bouton explicite requis EN PLUS du routage du bus sidechain dans le
    // DAW -- certains hotes (dont Plugin Doctor) laissent ce bus "actif"
    // avec du silence sans jamais le desactiver vraiment ; sans ce bouton,
    // le detecteur ecouterait du silence en permanence et plus aucune
    // compression ne se produirait.
    static constexpr const char* idSidechainEnable = "sidechainEnable";
    static constexpr const char* idAudioHp      = "audioHp";
    static constexpr const char* idAudioDist2   = "audioDist2";
    static constexpr const char* idAudioDist3   = "audioDist3";
    static constexpr const char* idPower               = "power";
    static constexpr const char* idOptical              = "optical"; // remplace Bypass -- caractere Opto applique a TOUS les ratios
    static constexpr const char* idBritish              = "british"; // exclusif avec Optical (voir PluginEditor)
    static constexpr const char* idSoftClip             = "softClip"; // etage soft clip (tanh), off par defaut

    // Comp/Expand -- 2 choix seulement : "Comp" (index 0, defaut) et
    // "Expand" (index 1). Mappe TOUJOURS en interne sur
    // FeedbackCompressor::AboveBelowMode::Both (compression au-dessus ET
    // expansion en-dessous tournent en permanence, sur les reglages Comp) --
    // ce choix ne fait que selectionner QUEL jeu de parametres
    // (Range/Ratio/Attack/Release) pilote la branche Below, voir
    // updateDspFromParams(). Comme idInputExpand (Range) est a 0dB par
    // defaut, la contribution d'expansion est mathematiquement nulle tant
    // qu'on ne le monte pas a la main.
    static constexpr const char* idAboveBelow           = "aboveBelow";

    // --- AI Learn (bouton AI + menu d'options, voir OptionsOverlay.h) ---
    // idAiScopeCrossover et tout ce qui touche a la Fidelity N'EXISTENT
    // PAS ici (mono-bande, rien a scinder ni a rebalancer entre bandes) --
    // omis sur demande explicite.
    static constexpr const char* idAiMasteringTarget          = "aiMasteringTarget";
    static constexpr const char* idAiCompressionMode          = "aiCompressionMode";
    static constexpr const char* idAiAttackReleaseCharacter   = "aiAttackReleaseCharacter";
    static constexpr const char* idAiGainReductionMode        = "aiGainReductionMode";
    static constexpr const char* idAiScopeDetectors           = "aiScopeDetectors";
    static constexpr const char* idAiScopeCompression         = "aiScopeCompression";
    static constexpr const char* idAiStyle                    = "aiStyle";

    // Parametres concernes par A/B (voir toggleABCompare()) : uniquement
    // le SON (knobs + boutons de caractere/detecteur/audio), PAS Power,
    // PAS Bypass (etats globaux/utilitaires), PAS les reglages AI (config
    // de la fonction AI Learn, pas des reglages de son en soi) -- meme
    // logique que Tripressor.
    static const std::vector<juce::String>& getABParamIds()
    {
        static const std::vector<juce::String> ids = {
            idInput, idRatioIndex, idAttack, idRelease, idOutput,
            idInputExpand, idRatioIndexExpand, idAttackExpand, idReleaseExpand, idAboveBelow,
            idDetectorHp, idDetectorHpFreq, idDetectorHpSlope,
            idDetectorBell, idDetectorBellFreq, idDetectorBellGain, idDetectorLink,
            idSidechainEnable, idAudioHp, idAudioDist2, idAudioDist3,
            idOptical, idBritish, idSoftClip
        };
        return ids;
    }

    // Table des 7 ratios discrets (index -> ratio reel), et de leurs knees
    // -- voir le manuel EL8 : 2:1 a un knee tres long, 20:1/NUKE quasi aucun.
    // (1:1 retire -- ratio neutre inutile.)
    static constexpr float ratioTable[7] = { 2.0f, 3.0f, 4.0f, 6.0f, 10.0f, 20.0f, 100.0f };
    static constexpr float kneeTable[7]  = { 24.0f, 18.0f, 12.0f, 9.0f, 6.0f, 1.0f, 0.0f };
    // Valeurs par defaut au chargement du plugin (le knob affiche
    // desormais la valeur exacte au LCD -- plus besoin d'un bouton de
    // rappel dedie, retire sur demande).
    static constexpr float defaultAttackMs  = 5.60f;
    static constexpr float defaultReleaseMs = 260.0f;

private:
    void timerCallback() override;
    void updateDspFromParams();

    FeedbackCompressor compressorL, compressorR;
    SoftClipper clipperL, clipperR;

    std::atomic<float>* inputParam        = nullptr;
    std::atomic<float>* ratioIndexParam   = nullptr;
    std::atomic<float>* attackParam       = nullptr;
    std::atomic<float>* releaseParam      = nullptr;
    std::atomic<float>* outputParam       = nullptr;
    std::atomic<float>* inputExpandParam      = nullptr;
    std::atomic<float>* ratioIndexExpandParam = nullptr;
    std::atomic<float>* attackExpandParam     = nullptr;
    std::atomic<float>* releaseExpandParam    = nullptr;
    std::atomic<float>* aboveBelowParam       = nullptr;
    std::atomic<float>* detectorHpParam   = nullptr;
    std::atomic<float>* detectorHpFreqParam  = nullptr;
    std::atomic<float>* detectorHpSlopeParam = nullptr;
    std::atomic<float>* detectorBellParam = nullptr;
    std::atomic<float>* detectorBellFreqParam = nullptr;
    std::atomic<float>* detectorBellGainParam = nullptr;
    std::atomic<float>* detectorLinkParam = nullptr;
    std::atomic<float>* sidechainEnableParam = nullptr;
    std::atomic<float>* audioHpParam      = nullptr;
    std::atomic<float>* audioDist2Param   = nullptr;
    std::atomic<float>* audioDist3Param   = nullptr;
    std::atomic<float>* powerParam        = nullptr;
    std::atomic<float>* opticalParam      = nullptr;
    std::atomic<float>* britishParam      = nullptr;
    std::atomic<float>* softClipParam     = nullptr;

    std::atomic<float>* aiMasteringTargetParam        = nullptr;
    std::atomic<float>* aiCompressionModeParam        = nullptr;
    std::atomic<float>* aiAttackReleaseCharacterParam = nullptr;
    std::atomic<float>* aiGainReductionModeParam      = nullptr;
    std::atomic<float>* aiScopeDetectorsParam         = nullptr;
    std::atomic<float>* aiScopeCompressionParam       = nullptr;
    std::atomic<float>* aiStyleParam                  = nullptr;

    std::atomic<juce::uint32> lastProcessBlockMs { 0 };

    // --- A/B (voir toggleABCompare()) ---
    std::map<juce::String, float> abSlotA, abSlotB;
    bool usingSlotB = false;

public:
    // --- Buffer circulaire leger pour le spectre du cadre X/Y (Detector
    // Audio) : ecrit par le thread audio (juste une ecriture + increment
    // atomique par sample, cout negligeable), LU ET ANALYSE (FFT) par le
    // thread UI a basse frequence -- aucun calcul lourd sur le thread
    // audio. Voir DetectorFilterGraph.h. ---
    static constexpr int spectrumFifoSize = 2048;

    void copyLatestSpectrumSamples (float* dest, int numSamples) const noexcept
    {
        const int idx = spectrumWriteIndex.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            const int pos = ((idx - numSamples + i) % spectrumFifoSize + spectrumFifoSize) % spectrumFifoSize;
            dest[i] = spectrumFifo[(size_t) pos];
        }
    }

    // --- Historique PRECIS de la reduction de gain, pour l'Envelope
    // Display de la plaque du bas : une valeur reelle par bloc audio
    // (pas de simulation), ecriture triviale cote audio. ---
    static constexpr int grHistorySize = 1024;

    void copyLatestGrHistory (float* dest, int numSamples) const noexcept
    {
        const int idx = grHistoryWriteIndex.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            const int pos = ((idx - numSamples + i) % grHistorySize + grHistorySize) % grHistorySize;
            dest[i] = grHistory[(size_t) pos];
        }
    }

    // --- Buffer de sortie (meme principe que le spectre) : pour le vrai
    // calcul du Crest Factor OUT (peak/RMS reels, pas simules). ---
    static constexpr int outputFifoSize = 4096;

    void copyLatestOutputSamples (float* dest, int numSamples) const noexcept
    {
        const int idx = outputWriteIndex.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            const int pos = ((idx - numSamples + i) % outputFifoSize + outputFifoSize) % outputFifoSize;
            dest[i] = outputFifo[(size_t) pos];
        }
    }

private:
    inline void pushSpectrumSample (float s) noexcept
    {
        const int idx = spectrumWriteIndex.fetch_add (1, std::memory_order_relaxed) % spectrumFifoSize;
        spectrumFifo[(size_t) idx] = s;
    }

    inline void pushGrHistorySample (float grDb) noexcept
    {
        const int idx = grHistoryWriteIndex.fetch_add (1, std::memory_order_relaxed) % grHistorySize;
        grHistory[(size_t) idx] = grDb;
    }

    inline void pushOutputSample (float s) noexcept
    {
        const int idx = outputWriteIndex.fetch_add (1, std::memory_order_relaxed) % outputFifoSize;
        outputFifo[(size_t) idx] = s;
    }

    std::array<float, spectrumFifoSize> spectrumFifo {};
    std::atomic<int> spectrumWriteIndex { 0 };

    std::array<float, grHistorySize> grHistory {};
    std::atomic<int> grHistoryWriteIndex { 0 };

    std::array<float, outputFifoSize> outputFifo {};
    std::atomic<int> outputWriteIndex { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuppressorAudioProcessor)
};
