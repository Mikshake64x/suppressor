#pragma once
#include <JuceHeader.h>
#include "FeedbackCompressor.h"

/**
    AiLearnAnalyzer
    ---------------
    Moteur du bouton "AI" (au-dessus du logo) : capture quelques secondes
    du programme audio en cours (STEREO), puis calcule hors du thread
    audio des valeurs de depart pour Input/Ratio/Attack/Release. PAS un
    vrai reseau de neurones -- un jeu de regles/heuristiques (analyse
    spectrale + facteur de crete + correlation stereo), meme principe que
    Tripressor mais adapte a l'unite MONO-BANDE de Suppressor : pas de
    crossover a detecter (rien a scinder Low/Mid/High), pas de Fidelity
    (cette fonction restaure l'equilibre RELATIF entre plusieurs bandes,
    n'a pas de sens sur une seule bande) -- ces 2 axes de Tripressor sont
    donc absents ici, sur demande explicite ("retire le changement de
    taille ainsi que la precision de crossover, vu que c'est un monobande").

    Duree de capture : 8s (assez de materiau pour des mesures RMS/crete/
    stereo representatives, moins sensibles a un silence ou un unique coup
    de grosse caisse tombes dans la fenetre).

    Cycle de vie (State) :
      Idle -> Capturing (le thread audio remplit `capturedL/R` via
      pushSamples(), voir PluginProcessor::processBlock()) -> des que le
      buffer est plein, le thread AUDIO bascule l'etat sur Analyzing et
      demarre CE thread (juce::Thread) pour le calcul lourd (FFT) -> Done
      (resultat pret, lu par l'editeur via getResult() puis acknowledge()
      pour revenir a Idle).

    Thread-safety : meme idiome que Tripressor -- `capturedL/R` ne sont
    ecrits QUE pendant Capturing (thread audio) et QUE lus pendant
    Analyzing/Done (ce thread), jamais les deux en meme temps. `result`
    est un POD ecrit juste avant le store() de `state` a Done ; la paire
    store(seq_cst)/load(seq_cst) sur `state` etablit la relation
    "happens-before" necessaire.
*/
class AiLearnAnalyzer : private juce::Thread
{
public:
    enum class State { Idle, Capturing, Analyzing, Done };

    // Cibles de loudness integree (LUFS) par plateforme -- memes indices
    // que l'AudioParameterChoice idAiMasteringTarget (PluginProcessor.cpp).
    static constexpr int kNumMasteringTargets = 9;
    static constexpr float kLufsTargets[kNumMasteringTargets] =
    {
        0.0f,    // Off (non utilise, voir applyLoudnessTarget)
        -16.0f,  // Apple Music
        -14.0f,  // YouTube
        -14.0f,  // Spotify
        -15.0f,  // Deezer
        -14.0f,  // SoundCloud
        -23.0f,  // TV (EBU R128)
        -9.0f,   // CD
        -6.0f,   // Side Key
    };

    struct Result
    {
        float inputDb = 0.0f;
        float ratioIndex = 3.0f; // 0..6 (flottant, l'appelant arrondit)
        // Valeurs par defaut dupliquees de SuppressorAudioProcessor::
        // defaultAttackMs/defaultReleaseMs (PluginProcessor.h) -- pas
        // d'include croise possible (PluginProcessor.h inclut ce fichier).
        float attackMs = 5.60f;
        float releaseMs = 260.0f;

        // Portee (menu Options, section AI, "AI ne s'occupe que de la
        // compression si on le souhaite") -- decide ce que l'EDITEUR a le
        // droit de pousser vers les parametres. Pas de portee Crossover
        // ici (mono-bande, rien a scinder).
        bool applyDetectorsScope   = false;
        bool applyCompressionScope = true;

        // Detecteur sidechain (Bell) -- pointe sur la frequence dominante
        // mesuree sur TOUT le spectre (pas de bande a restreindre ici),
        // engage seulement si un pic assez marque a ete trouve.
        bool  detectorBellOn     = false;
        float detectorBellFreqHz = 650.0f;
        float detectorBellGainDb = 6.0f;

        // Cible de mastering (menu Options, section AI).
        bool  applyLoudnessTarget = false;
        float outputDb = 0.0f;
        bool  recommendSoftClipOn = false;
        float measuredLufs = -23.0f; // informatif seulement

        // Style (menu Options, section AI) -- simple offset statique sur
        // l'Output, voir kStyleOutputBiasDb. "Off" = pas d'offset.
        bool  applyStyle = false;
        float styleOutputDb = 0.0f;

        // Chaque axe a son propre "Off" (menu Options) -- decide si
        // l'EDITEUR pousse la cible CORRESPONDANTE.
        bool pushInput         = true; // gouverne par AI Compression Mode
        bool pushRatio         = true; // gouverne par AI Gain Reduction
        bool pushAttackRelease = true; // gouverne par AI Attack/Release Character
    };

    // Modes de compression proposes par le bouton AI (Off/Soft/Medium/Hard).
    struct CompressionModeBias { float ratioBoost, inputTargetDb, attackScale, releaseScale; };
    static constexpr int kNumCompressionModes = 3;
    static constexpr int kNumCompressionModeChoices = 4; // + "Off"
    static constexpr CompressionModeBias kCompressionModeBias[kNumCompressionModes] =
    {
        { 0.65f, -21.0f, 1.35f, 1.30f }, // Soft
        { 1.00f, -18.0f, 1.00f, 1.00f }, // Medium (comportement d'origine)
        { 1.45f, -15.0f, 0.70f, 0.75f }, // Hard
    };

    // Caractere Attack/Release (Off/Smooth/Medium/Punchy) -- axe
    // INDEPENDANT du mode de compression : ne change que la FORME
    // attack/release, pas le ratio ni l'input.
    struct AttackReleaseCharacterBias { float attackScale, releaseScale; };
    static constexpr int kNumAttackReleaseCharacters = 3;
    static constexpr int kNumAttackReleaseCharacterChoices = 4; // + "Off"
    static constexpr AttackReleaseCharacterBias kAttackReleaseCharacterBias[kNumAttackReleaseCharacters] =
    {
        { 0.65f, 1.55f }, // Smooth
        { 1.00f, 1.00f }, // Medium
        { 1.35f, 0.55f }, // Punchy
    };

    // Intensite GLOBALE de gain reduction (Off/Smooth/Mid/Hard) -- AXE
    // SUPPLEMENTAIRE, independant des 2 ci-dessus.
    struct GainReductionBias { float ratioMul, inputBackoffDb; };
    static constexpr int kNumGainReductionModes = 3;
    static constexpr int kNumGainReductionModeChoices = 4; // + "Off"
    static constexpr GainReductionBias kGainReductionBias[kNumGainReductionModes] =
    {
        { 0.45f,  +5.0f }, // Smooth
        { 1.00f,   0.0f }, // Mid (comportement d'origine)
        { 1.40f,  -3.0f }, // Hard
    };

    // Plafond EXPLICITE de gain reduction en dB par mode -- verifie APRES
    // coup par simulation du VRAI FeedbackCompressor sur le signal capture,
    // recule l'Input par dichotomie si depasse. Jamais l'inverse.
    static constexpr float kGainReductionCapDb[kNumGainReductionModes] = { 5.0f, 9.0f, 15.0f };

    // Tables ratio/knee de Suppressor (voir SuppressorAudioProcessor::
    // ratioTable/kneeTable, PluginProcessor.h) -- dupliquees ici (pas
    // d'include croise possible, PluginProcessor.h inclut deja ce
    // fichier). Doivent rester synchronisees si la table change la-bas.
    static constexpr float kRatioTable[7] = { 2.0f, 3.0f, 4.0f, 6.0f, 10.0f, 20.0f, 100.0f };
    static constexpr float kKneeTable[7]  = { 24.0f, 18.0f, 12.0f, 9.0f, 6.0f, 1.0f, 0.0f };

    // Styles (menu Options, section AI) -- demande explicite : liste
    // etendue avec des types de source/instrument, EN PLUS des styles
    // musicaux deja presents sur Tripressor (Bass/Loud/Rock/Pop/
    // Electronic). Offset statique en dB sur l'Output -- meme mecanisme
    // que le Style de Tripressor, simplement UNE valeur (mono-bande) au
    // lieu d'un triplet Low/Mid/High. "Off" (index 0, pas d'entree ici)
    // = aucun offset.
    static constexpr int kNumStyles = 16;
    static constexpr int kNumStyleChoices = 17; // + "Off"
    static constexpr float kStyleOutputBiasDb[kNumStyles] =
    {
        3.0f,  // Bass
        3.0f,  // Loud
        2.0f,  // Rock
        1.0f,  // Pop
        3.0f,  // Electronic
        1.5f,  // Guitar
        1.0f,  // Synth
        2.0f,  // Drum Buss
        1.0f,  // Vocal
        2.5f,  // Kick
        2.0f,  // Snare
        1.5f,  // Tom
        1.0f,  // Horn
        0.0f,  // Mastering (reste transparent -- pas de poussee)
        2.5f,  // Bass Synth
        1.5f,  // Percussions
    };

    AiLearnAnalyzer() : juce::Thread ("AI Learn Analyzer") {}
    ~AiLearnAnalyzer() override { stopThread (2000); }

    // Demarre une nouvelle capture -- ignore si une capture/analyse est
    // deja en cours. Appele depuis le thread MESSAGE (clic sur le bouton AI).
    void beginCapture (double sampleRateIn, int masteringTargetIndex, int compressionModeIndex,
                        int attackReleaseCharacterIndex, int gainReductionModeIndex,
                        bool scopeDetectors, bool scopeCompression,
                        int styleIndex,
                        double durationSeconds = 8.0)
    {
        auto s = state.load();
        if (s == State::Capturing || s == State::Analyzing)
            return;

        sampleRate = sampleRateIn;
        masteringTarget = juce::jlimit (0, kNumMasteringTargets - 1, masteringTargetIndex);
        compressionMode  = juce::jlimit (0, kNumCompressionModeChoices - 1, compressionModeIndex);
        attackReleaseCharacter = juce::jlimit (0, kNumAttackReleaseCharacterChoices - 1, attackReleaseCharacterIndex);
        gainReductionMode = juce::jlimit (0, kNumGainReductionModeChoices - 1, gainReductionModeIndex);
        applyDetectors   = scopeDetectors;
        applyCompression = scopeCompression;
        style            = juce::jlimit (0, kNumStyleChoices - 1, styleIndex);
        targetSamples = juce::jmax (1, (int) (sampleRateIn * durationSeconds));
        capturedL.setSize (1, targetSamples, false, false, true);
        capturedR.setSize (1, targetSamples, false, false, true);
        writePos = 0;
        state.store (State::Capturing);
    }

    // Appele UNIQUEMENT depuis processBlock() (thread audio), et seulement
    // si getState() == Capturing. Si l'entree est mono (right == nullptr),
    // duplique L sur R. Des que le buffer cible est plein, bascule sur
    // Analyzing et demarre le calcul en tache de fond.
    void pushSamples (const float* left, const float* right, int numSamples) noexcept
    {
        if (state.load() != State::Capturing)
            return;

        auto* destL = capturedL.getWritePointer (0);
        auto* destR = capturedR.getWritePointer (0);
        const int remaining = targetSamples - writePos;
        const int n = juce::jmin (numSamples, remaining);
        for (int i = 0; i < n; ++i)
        {
            destL[writePos + i] = left[i];
            destR[writePos + i] = (right != nullptr) ? right[i] : left[i];
        }
        writePos += n;

        if (writePos >= targetSamples)
        {
            state.store (State::Analyzing);
            startThread (juce::Thread::Priority::low);
        }
    }

    State getState() const noexcept { return state.load(); }
    const Result& getResult() const noexcept { return result; }
    void acknowledge() noexcept { state.store (State::Idle); }

private:
    void run() override
    {
        Result r;
        r.applyDetectorsScope   = applyDetectors;
        r.applyCompressionScope = applyCompression;

        r.applyStyle = style > 0;
        if (r.applyStyle)
            r.styleOutputDb = kStyleOutputBiasDb[(size_t) (style - 1)];

        const bool compressionModeOn = compressionMode > 0;
        const bool characterOn       = attackReleaseCharacter > 0;
        const bool gainReductionOn   = gainReductionMode > 0;
        r.pushInput         = compressionModeOn;
        r.pushAttackRelease = characterOn;
        r.pushRatio         = gainReductionOn;
        const int compressionBiasIdx = juce::jlimit (0, kNumCompressionModes - 1, compressionModeOn ? compressionMode - 1 : 1);
        const int characterBiasIdx   = juce::jlimit (0, kNumAttackReleaseCharacters - 1, characterOn ? attackReleaseCharacter - 1 : 1);
        const int gainReductionBiasIdx = juce::jlimit (0, kNumGainReductionModes - 1, gainReductionOn ? gainReductionMode - 1 : 1);

        const int numSamples = capturedL.getNumSamples();
        const float* left  = capturedL.getReadPointer (0);
        const float* right = capturedR.getReadPointer (0);

        constexpr int fftOrder = 12;
        constexpr int fftSize  = 1 << fftOrder;
        const int numBins = fftSize / 2 + 1;

        if (numSamples < fftSize)
        {
            result = r; // pas assez de signal capture -- garde les valeurs par defaut
            state.store (State::Done);
            return;
        }

        // --- 1. Spectre moyen (Welch, fenetre Hann, recouvrement 50%) sur
        // le downmix mono -- utilise seulement pour le detecteur Bell
        // (pas de crossover a situer ici, unite mono-bande). ---
        std::vector<float> monoDownmix ((size_t) numSamples);
        for (int i = 0; i < numSamples; ++i)
            monoDownmix[(size_t) i] = 0.5f * (left[i] + right[i]);

        juce::dsp::FFT fft (fftOrder);
        juce::dsp::WindowingFunction<float> window (fftSize, juce::dsp::WindowingFunction<float>::hann);

        std::vector<float> avgMagSq (numBins, 0.0f);
        std::vector<float> fftData ((size_t) fftSize * 2, 0.0f);
        const int hop = fftSize / 2;
        int numFrames = 0;

        for (int start = 0; start + fftSize <= numSamples; start += hop)
        {
            std::fill (fftData.begin(), fftData.end(), 0.0f);
            std::copy (monoDownmix.begin() + start, monoDownmix.begin() + start + fftSize, fftData.begin());
            window.multiplyWithWindowingTable (fftData.data(), fftSize);
            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

            for (int bin = 0; bin < numBins; ++bin)
                avgMagSq[(size_t) bin] += fftData[(size_t) bin] * fftData[(size_t) bin];

            ++numFrames;

            if (threadShouldExit())
                return;
        }

        const double binHz = sampleRate / (double) fftSize;

        // --- 1bis. Detecteur sidechain (Bell) : cherche le PIC spectral le
        // plus marque sur TOUT le spectre utile (20Hz-18kHz) -- pas de
        // bande a restreindre (mono-bande). Engage seulement si le pic
        // depasse nettement la moyenne (peakiness > 3x). ---
        if (applyDetectors && numFrames > 0)
        {
            const int bLo = juce::jlimit (1, numBins - 1, (int) (20.0 / binHz));
            const int bHi = juce::jlimit (bLo, numBins - 1, (int) (18000.0 / binHz));

            double sum = 0.0;
            int peakBin = bLo;
            double peakVal = 0.0;
            for (int bin = bLo; bin <= bHi; ++bin)
            {
                const double v = avgMagSq[(size_t) bin];
                sum += v;
                if (v > peakVal) { peakVal = v; peakBin = bin; }
            }

            const int count = juce::jmax (1, bHi - bLo + 1);
            const double avg = sum / (double) count;
            const double peakiness = peakVal / juce::jmax (1.0e-12, avg);

            r.detectorBellFreqHz = juce::jlimit (20.0f, 20000.0f, (float) (peakBin * binHz));

            if (peakiness > 3.0)
            {
                r.detectorBellOn = true;
                const double t2 = juce::jlimit (0.0, 1.0, (peakiness - 3.0) / 9.0);
                r.detectorBellGainDb = juce::jlimit (0.0f, 18.0f, (float) (4.0 + t2 * 10.0));
            }
            else
            {
                r.detectorBellOn = false;
            }

            if (threadShouldExit())
                return;
        }

        // --- 2. Mesures RMS/crete/stereo sur le programme COMPLET (mono
        // downmix) -- pas de split par bande, unite mono-bande. ---
        double sumSq = 0.0;
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float mono = monoDownmix[(size_t) i];
            sumSq += (double) mono * (double) mono;
            peak = juce::jmax (peak, std::abs (mono));
        }

        const float rms    = (float) std::sqrt (sumSq / juce::jmax (1, numSamples));
        const float rmsDb  = juce::Decibels::gainToDecibels (rms,  -100.0f);
        const float peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
        const float crestDb = juce::jlimit (0.0f, 30.0f, peakDb - rmsDb);

        const auto& bias = kCompressionModeBias[compressionBiasIdx];
        const auto& gr   = kGainReductionBias[gainReductionBiasIdx];

        r.inputDb = juce::jlimit (-30.0f, 30.0f, bias.inputTargetDb + gr.inputBackoffDb - rmsDb);

        const float t = juce::jlimit (0.0f, 1.0f, (crestDb - 6.0f) / 18.0f);
        r.ratioIndex = juce::jlimit (0.0f, 6.0f, t * 4.0f * bias.ratioBoost * gr.ratioMul);

        const auto& character = kAttackReleaseCharacterBias[characterBiasIdx];
        r.attackMs  = juce::jlimit (0.05f, 40.0f,   (20.0f - t * 15.0f) * bias.attackScale * character.attackScale);
        r.releaseMs = juce::jlimit (50.0f, 3000.0f, (260.0f - t * 30.0f) * bias.releaseScale * character.releaseScale);

        // --- Plafond de gain reduction : simule le VRAI FeedbackCompressor
        // (meme classe que le DSP temps reel) sur le signal REEL capture,
        // mesure la GR crete obtenue, et recule l'Input par dichotomie si
        // elle depasse le plafond du mode. Jamais l'inverse. ---
        if (gainReductionOn)
        {
            const int ratioIdxInt = juce::jlimit (0, 6, juce::roundToInt (r.ratioIndex));
            const float ratioValue = kRatioTable[(size_t) ratioIdxInt];
            const float kneeValue  = kKneeTable[(size_t) ratioIdxInt];
            const float attackMs   = r.attackMs;
            const float releaseMs  = r.releaseMs;

            auto measurePeakGrDb = [&] (float inputDb) -> float
            {
                FeedbackCompressor sim;
                sim.prepare (sampleRate);
                sim.setInputDriveDb (inputDb);
                sim.setRatio (ratioValue);
                sim.setKneeDb (kneeValue);
                sim.setAttackMs (attackMs);
                sim.setReleaseMs (releaseMs);
                sim.setOutputDb (0.0f);

                float peakGr = 0.0f;
                for (int i = 0; i < numSamples; ++i)
                {
                    sim.processSample (monoDownmix[(size_t) i]);
                    peakGr = juce::jmax (peakGr, std::abs (sim.getGainReductionDb()));
                }
                return peakGr;
            };

            const float capDb = kGainReductionCapDb[gainReductionBiasIdx];
            float testInputDb = r.inputDb;
            if (measurePeakGrDb (testInputDb) > capDb)
            {
                float lo = -30.0f, hi = testInputDb;
                for (int iter = 0; iter < 10; ++iter)
                {
                    const float mid = 0.5f * (lo + hi);
                    if (measurePeakGrDb (mid) > capDb)
                        hi = mid;
                    else
                        lo = mid;

                    if (threadShouldExit())
                        return;
                }
                r.inputDb = lo;
            }
        }

        // --- 3. Cible de mastering (LUFS approx. BS.1770, K-weighting 2
        // etages + moyenne quadratique, sans le gating officiel complet). ---
        if (masteringTarget > 0)
        {
            juce::dsp::IIR::Filter<float> shelfL, hpL, shelfR, hpR;
            shelfL.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf ((float) sampleRate, 1500.0f, 0.7071f, juce::Decibels::decibelsToGain (4.0f));
            hpL.coefficients    = juce::dsp::IIR::Coefficients<float>::makeHighPass  ((float) sampleRate, 38.0f);
            shelfR.coefficients = shelfL.coefficients;
            hpR.coefficients    = hpL.coefficients;

            std::vector<float> kL ((size_t) numSamples), kR ((size_t) numSamples);
            std::copy (left,  left  + numSamples, kL.begin());
            std::copy (right, right + numSamples, kR.begin());

            for (int i = 0; i < numSamples; ++i)
            {
                kL[(size_t) i] = hpL.processSample (shelfL.processSample (kL[(size_t) i]));
                kR[(size_t) i] = hpR.processSample (shelfR.processSample (kR[(size_t) i]));
            }

            double msL = 0.0, msR = 0.0;
            for (int i = 0; i < numSamples; ++i)
            {
                msL += (double) kL[(size_t) i] * (double) kL[(size_t) i];
                msR += (double) kR[(size_t) i] * (double) kR[(size_t) i];
            }
            msL /= juce::jmax (1, numSamples);
            msR /= juce::jmax (1, numSamples);

            const double sumMs = juce::jmax (1.0e-12, msL + msR);
            const float measuredLufs = (float) (-0.691 + 10.0 * std::log10 (sumMs));
            r.measuredLufs = measuredLufs;

            const float targetLufs = kLufsTargets[(size_t) masteringTarget];
            const float gainNeededDb = juce::jlimit (-12.0f, 12.0f, targetLufs - measuredLufs);

            r.applyLoudnessTarget = true;
            r.recommendSoftClipOn = true;
            r.outputDb = juce::jlimit (-30.0f, 30.0f, gainNeededDb);
        }

        result = r;
        state.store (State::Done);
    }

    std::atomic<State> state { State::Idle };
    double sampleRate = 44100.0;
    int masteringTarget = 0;
    int compressionMode = 1;
    int attackReleaseCharacter = 1;
    int gainReductionMode = 1;
    bool applyDetectors = false, applyCompression = true;
    int style = 0;
    int targetSamples = 0;
    int writePos = 0;
    juce::AudioBuffer<float> capturedL, capturedR;
    Result result;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiLearnAnalyzer)
};
