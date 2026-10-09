#include "PluginProcessor.h"
#include "PluginEditor.h"

constexpr float SuppressorAudioProcessor::ratioTable[7];
constexpr float SuppressorAudioProcessor::kneeTable[7];

SuppressorAudioProcessor::SuppressorAudioProcessor()
     : AudioProcessor (BusesProperties()
                       #if ! JucePlugin_IsMidiEffect
                        #if ! JucePlugin_IsSynth
                         .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                         // Bus sidechain externe (desactive par defaut) --
                         // route depuis une autre piste dans le DAW (ex. un
                         // kick, pour du ducking/side-chain compression).
                         // Voir processBlock() : quand actif, le detecteur
                         // ecoute ce signal au lieu du feedback interne.
                         .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        #endif
                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                       #endif
                         )
{
    inputParam               = apvts.getRawParameterValue (idInput);
    ratioIndexParam          = apvts.getRawParameterValue (idRatioIndex);
    attackParam              = apvts.getRawParameterValue (idAttack);
    releaseParam             = apvts.getRawParameterValue (idRelease);
    outputParam              = apvts.getRawParameterValue (idOutput);
    inputExpandParam         = apvts.getRawParameterValue (idInputExpand);
    ratioIndexExpandParam    = apvts.getRawParameterValue (idRatioIndexExpand);
    attackExpandParam        = apvts.getRawParameterValue (idAttackExpand);
    releaseExpandParam       = apvts.getRawParameterValue (idReleaseExpand);
    aboveBelowParam          = apvts.getRawParameterValue (idAboveBelow);
    detectorHpParam          = apvts.getRawParameterValue (idDetectorHp);
    detectorHpFreqParam      = apvts.getRawParameterValue (idDetectorHpFreq);
    detectorHpSlopeParam     = apvts.getRawParameterValue (idDetectorHpSlope);
    detectorBellParam        = apvts.getRawParameterValue (idDetectorBell);
    detectorBellFreqParam    = apvts.getRawParameterValue (idDetectorBellFreq);
    detectorBellGainParam    = apvts.getRawParameterValue (idDetectorBellGain);
    detectorLinkParam        = apvts.getRawParameterValue (idDetectorLink);
    sidechainEnableParam     = apvts.getRawParameterValue (idSidechainEnable);
    audioHpParam             = apvts.getRawParameterValue (idAudioHp);
    audioDist2Param          = apvts.getRawParameterValue (idAudioDist2);
    audioDist3Param          = apvts.getRawParameterValue (idAudioDist3);
    powerParam               = apvts.getRawParameterValue (idPower);
    opticalParam             = apvts.getRawParameterValue (idOptical);
    britishParam             = apvts.getRawParameterValue (idBritish);
    softClipParam            = apvts.getRawParameterValue (idSoftClip);

    aiMasteringTargetParam        = apvts.getRawParameterValue (idAiMasteringTarget);
    aiCompressionModeParam        = apvts.getRawParameterValue (idAiCompressionMode);
    aiAttackReleaseCharacterParam = apvts.getRawParameterValue (idAiAttackReleaseCharacter);
    aiGainReductionModeParam      = apvts.getRawParameterValue (idAiGainReductionMode);
    aiScopeDetectorsParam         = apvts.getRawParameterValue (idAiScopeDetectors);
    aiScopeCompressionParam       = apvts.getRawParameterValue (idAiScopeCompression);
    aiStyleParam                  = apvts.getRawParameterValue (idAiStyle);

    startTimerHz (10);
}

SuppressorAudioProcessor::~SuppressorAudioProcessor()
{
    stopTimer();
}

void SuppressorAudioProcessor::timerCallback()
{
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    const juce::uint32 last = lastProcessBlockMs.load();

    if (last == 0 || now - last > 250)
        grMeterDb.store (0.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout SuppressorAudioProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { idInput, 1 }, "Input",
        NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idRatioIndex, 1 }, "Ratio",
        StringArray { "2:1", "3:1", "4:1", "6:1", "10:1 Opto", "20:1", "NUKE" }, 3));

    // 0.05ms a 40ms
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { idAttack, 1 }, "Attack",
        NormalisableRange<float> (0.05f, 40.0f, 0.001f, 0.3f), defaultAttackMs,
        AudioParameterFloatAttributes().withLabel ("ms")));

    // 50ms a 3000ms nominal (le mode Opto etend cette plage en interne, voir FeedbackCompressor)
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { idRelease, 1 }, "Release",
        NormalisableRange<float> (50.0f, 3000.0f, 0.1f, 0.3f), defaultReleaseMs,
        AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { idOutput, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    // --- Jeu de parametres dedie EXPAND (voir idInputExpand et la doc
    // associee dans PluginProcessor.h) ---
    // Reaffecte en "Range" cote UI (pas Input) : plafond du boost
    // applicable, 0dB par defaut = expansion COMPLETEMENT DESACTIVEE tant
    // que l'utilisateur ne monte pas Range a la main.
    // Course limitee a 5 dB max (etait 24) : un expander de mastering n'a
    // besoin que de quelques dB de boost, et la course courte rend le knob
    // beaucoup plus precis. (FeedbackCompressor::setExpandRangeDb garde son
    // propre plafond 24 dB, inchange, tres au-dessus de cette limite.)
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { idInputExpand, 1 }, "Expand Range",
        NormalisableRange<float> (0.0f, 5.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    // Ratio par defaut plus doux que la compression (2:1, index 0, au lieu
    // de 6:1 -- le meme ratio "costaud" par defaut pour un expander sonnerait trop dur).
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idRatioIndexExpand, 1 }, "Ratio Expand",
        StringArray { "2:1", "3:1", "4:1", "6:1", "10:1 Opto", "20:1", "NUKE" }, 0));

    // Plage remontee a 500ms (etait 40ms comme la compression) -- un
    // expander/upward compressor de mastering a souvent besoin d'une
    // attaque plus lente que la compression classique.
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { idAttackExpand, 1 }, "Attack Expand",
        NormalisableRange<float> (0.05f, 500.0f, 0.001f, 0.2f), defaultAttackMs,
        AudioParameterFloatAttributes().withLabel ("ms")));
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { idReleaseExpand, 1 }, "Release Expand",
        NormalisableRange<float> (50.0f, 3000.0f, 0.1f, 0.3f), defaultReleaseMs,
        AudioParameterFloatAttributes().withLabel ("ms")));

    // 2 choix seulement (Comp/Expand) -- voir doc idAboveBelow.
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAboveBelow, 1 }, "Comp/Expand",
        StringArray { "Comp", "Expand" }, 0));

    // DETECTOR et AUDIO : un bool par fonction, un bouton par fonction
    // (clic direct dans l'UI -- plus de defilement).
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idDetectorHp, 1 }, "Detector HP", false));
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { idDetectorHpFreq, 1 }, "Detector HP Frequency",
        NormalisableRange<float> (20.0f, 2000.0f, 0.1f, 0.3f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));
    params.push_back (std::make_unique<AudioParameterInt> (
        ParameterID { idDetectorHpSlope, 1 }, "Detector HP Slope", 1, 8, 2));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idDetectorBell, 1 }, "Detector Bell", false));
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { idDetectorBellFreq, 1 }, "Detector Bell Frequency",
        NormalisableRange<float> (20.0f, 2000.0f, 0.1f, 0.3f), 650.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { idDetectorBellGain, 1 }, "Detector Bell Gain",
        NormalisableRange<float> (0.0f, 18.0f, 0.1f), 6.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idDetectorLink, 1 }, "Detector Link", false));

    // Requis EN PLUS du routage du bus Sidechain dans le DAW -- voir
    // commentaire idSidechainEnable.
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idSidechainEnable, 1 }, "Sidechain", false));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idAudioHp, 1 }, "Audio HP", false));
    // Noms affiches "Tube"/"Fuzz" (ID internes idAudioDist2/3 inchanges,
    // pour ne pas casser l'automation/les presets existants).
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idAudioDist2, 1 }, "Tube", false));
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idAudioDist3, 1 }, "Fuzz", false));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idPower, 1 }, "Power", true));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idOptical, 1 }, "Optical", false));

    // Exclusif avec Optical (logique manuelle dans PluginEditor) :
    // caractere "toutes boutons enfonces" du hardware. Nom affiche
    // "Overkill" (ID interne idBritish inchange).
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idBritish, 1 }, "Overkill", false));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idSoftClip, 1 }, "Soft Clip", false));

    // --- AI Learn (voir OptionsOverlay.h/AiLearnAnalyzer.h) --- pas de
    // idAiScopeCrossover ni idAiFidelity ici (mono-bande).
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiMasteringTarget, 1 }, "AI Mastering Target",
        StringArray { "Off", "Apple Music", "YouTube", "Spotify", "Deezer", "SoundCloud", "TV", "CD", "Side Key" }, 0));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idAiScopeDetectors, 1 }, "AI Scope: Detectors", false));
    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { idAiScopeCompression, 1 }, "AI Scope: Compression", true));

    // Chaque axe a son propre choix "Off" (index 0) -- Off ne desactive QUE
    // l'ecriture de sa propre cible, le reste continue d'etre pousse.
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiCompressionMode, 1 }, "AI Compression Mode",
        StringArray { "Off", "Soft", "Medium", "Hard" }, 2));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiAttackReleaseCharacter, 1 }, "AI Attack/Release Character",
        StringArray { "Off", "Smooth", "Medium", "Punchy" }, 2));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiGainReductionMode, 1 }, "AI Gain Reduction",
        StringArray { "Off", "Smooth", "Mid", "Hard" }, 2));

    // Style (menu Options, section AI) -- offset statique sur l'Output,
    // voir AiLearnAnalyzer::kStyleOutputBiasDb. Liste ETENDUE (demande
    // explicite) : styles musicaux d'origine + types d'instrument/source.
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { idAiStyle, 1 }, "AI Style",
        StringArray { "Off", "Bass", "Loud", "Rock", "Pop", "Electronic",
                      "Guitar", "Synth", "Drum Buss", "Vocal", "Kick", "Snare",
                      "Tom", "Horn", "Mastering", "Bass Synth", "Percussions" }, 0));

    return { params.begin(), params.end() };
}

void SuppressorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    compressorL.prepare (sampleRate);
    compressorR.prepare (sampleRate);
    clipperL.reset();
    clipperR.reset();
}

void SuppressorAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SuppressorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    // Bus sidechain (index 1) : optionnel -- peut etre desactive, mono ou
    // stereo, n'a pas besoin de matcher le bus principal (un kick de
    // ducking est souvent mono).
    const auto scSet = layouts.getChannelSet (true, 1);
    if (! scSet.isDisabled() && scSet != juce::AudioChannelSet::mono() && scSet != juce::AudioChannelSet::stereo())
        return false;

    return true;
}
#endif

void SuppressorAudioProcessor::updateDspFromParams()
{
    const int ratioIdx = juce::jlimit (0, 6, (int) std::round (ratioIndexParam->load()));
    const float ratio = ratioTable[(size_t) ratioIdx];
    const float knee  = kneeTable[(size_t) ratioIdx];
    const bool isOpto = opticalParam->load() > 0.5f; // global -- applicable a n'importe quel ratio
    const bool isBritish = britishParam->load() > 0.5f; // exclusif avec Optical, voir PluginEditor

    const bool detHp = detectorHpParam->load() > 0.5f;
    const float detHpFreq = detectorHpFreqParam->load();
    const int detHpSlope = (int) std::round (detectorHpSlopeParam->load());
    const bool detBandEmphasis = detectorBellParam->load() > 0.5f;
    const float detBellFreq = detectorBellFreqParam->load();
    const float detBellGain = detectorBellGainParam->load();
    // Le bit Link est gere au niveau processBlock() (voir detectorLink plus bas)

    const bool audHp = audioHpParam->load() > 0.5f;
    const bool dist2On = audioDist2Param->load() > 0.5f;
    const bool dist3On = audioDist3Param->load() > 0.5f;

    // Les deux boutons sont independants (chacun son propre bouton, pas de
    // radio exclusif) -- si les deux sont enclenches en meme temps, on
    // additionne simplement les deux caracteres plutot que d'interdire la
    // combinaison.
    float colourAmount = 0.0f;
    float colourBias = 0.0f;
    if (dist2On) { colourAmount += 25.0f; colourBias += -0.6f; } // Dist 2 : 2nd harmonique, "class A"
    if (dist3On) { colourAmount += 55.0f; colourBias +=  0.6f; } // Dist 3 : 2nd + 3rd, "tape"
    if (dist2On && dist3On) colourBias *= 0.5f; // moyenne des deux biais si les deux sont actifs
    colourAmount = juce::jmin (colourAmount, 80.0f);

    if (ratioIdx == 6) // NUKE (dernier index depuis le retrait de 1:1) : garantit un minimum de coloration/ecrasement
        colourAmount = juce::jmax (colourAmount, 30.0f);

    // Comp/Expand -- mappe TOUJOURS en interne sur AboveBelowMode::Both
    // (compression ET expansion tournent en permanence) : ce choix ne fait
    // que selectionner QUEL jeu de parametres (Range/Ratio/Attack/Release)
    // pilote la branche Below (expansion), voir doc de idAboveBelow.
    const bool useExpandSettings = juce::roundToInt (aboveBelowParam->load()) == 1;
    const auto aboveBelowMode = FeedbackCompressor::AboveBelowMode::Both;

    // Below : Comp mire les reglages Compression ci-dessus (ratio/attack/
    // release), Expand bascule vers le jeu de parametres Expand dedie.
    const int belowRatioIdx = juce::jlimit (0, 6, (int) std::round (
        (useExpandSettings ? ratioIndexExpandParam : ratioIndexParam)->load()));
    const float belowRatio = ratioTable[(size_t) belowRatioIdx];
    const float belowKnee  = kneeTable[(size_t) belowRatioIdx];
    const float belowAttackMs  = (useExpandSettings ? attackExpandParam  : attackParam)->load();
    const float belowReleaseMs = (useExpandSettings ? releaseExpandParam : releaseParam)->load();

    // Range TOUJOURS pris depuis le parametre Expand calibre, quel que soit
    // le mode Comp/Expand -- coherent d'un mode a l'autre (voir doc de
    // FeedbackCompressor::setExpandRangeDb()).
    const float effRangeDb = inputExpandParam->load();

    for (auto* comp : { &compressorL, &compressorR })
    {
        comp->setInputDriveDb (inputParam->load());
        comp->setRatio (ratio);
        comp->setKneeDb (knee);
        comp->setAttackMs (attackParam->load());
        comp->setReleaseMs (releaseParam->load());
        comp->setOutputDb (outputParam->load());
        comp->setOptoCharacter (isOpto);
        comp->setBritishMode (isBritish);
        comp->setBelowRatio (belowRatio);
        comp->setBelowKneeDb (belowKnee);
        comp->setBelowAttackMs (belowAttackMs);
        comp->setBelowReleaseMs (belowReleaseMs);
        comp->setAboveBelowMode (aboveBelowMode);
        comp->setExpandRangeDb (effRangeDb);
        comp->setDetectorHpEngaged (detHp);
        comp->setDetectorHpFrequency (detHpFreq);
        comp->setDetectorHpSlopeSteps (detHpSlope);
        comp->setDetectorBandEmphasis (detBandEmphasis);
        comp->setDetectorBellFrequency (detBellFreq);
        comp->setDetectorBellGainDb (detBellGain);
        comp->setAudioHpEngaged (audHp);
        comp->setColourAmount (colourAmount);
        comp->setColourHarmonicBias (colourBias);
    }
}

void SuppressorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    lastProcessBlockMs.store (juce::Time::getMillisecondCounter());

    juce::ScopedNoDenormals noDenormals;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    const bool powerOn = powerParam->load() > 0.5f;

    updateDspFromParams();

    auto* dataL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
    auto* dataR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    // Bouton AI : capture le programme SEC (avant tout traitement) pendant
    // la fenetre d'ecoute -- voir AiLearnAnalyzer.h. Ne fait rien tant que
    // l'etat n'est pas Capturing (verifie en interne), donc cout nul le
    // reste du temps.
    if (dataL != nullptr)
        aiLearnAnalyzer.pushSamples (dataL, dataR, numSamples);

    // Bus sidechain externe (voir constructeur) : NE PAS se fier
    // uniquement au nombre de canaux du bus -- certains hotes (dont
    // Plugin Doctor) le laissent "actif" avec du silence sans jamais le
    // desactiver vraiment, ce qui ferait ecouter du silence au detecteur
    // en permanence (plus aucune compression). Le bouton SIDECHAIN de
    // l'UI est donc REQUIS en plus du routage cote DAW.
    auto sidechainBuffer = getBusBuffer (buffer, true, 1);
    const bool sidechainActive = sidechainBuffer.getNumChannels() > 0 && sidechainEnableParam->load() > 0.5f;
    const float* scDataL = sidechainActive ? sidechainBuffer.getReadPointer (0) : nullptr;
    const float* scDataR = sidechainActive && sidechainBuffer.getNumChannels() > 1
                                ? sidechainBuffer.getReadPointer (1) : scDataL;

    if (! powerOn)
    {
        compressorL.reset(); compressorR.reset();
        clipperL.reset();    clipperR.reset();
        grMeterDb.store (0.0f);
        grMeterExpandDb.store (0.0f);
        grMeterDbL.store (0.0f); grMeterDbR.store (0.0f);
        inputLevelDbL.store (-100.0f);  inputLevelDbR.store (-100.0f);
        outputLevelDbL.store (-100.0f); outputLevelDbR.store (-100.0f);
        clipMeterAmount.store (0.0f);
        clipMeterAmountL.store (0.0f); clipMeterAmountR.store (0.0f);
        return;
    }

    const bool detectorLink    = dataR != nullptr && detectorLinkParam->load() > 0.5f;
    const bool softClipOn      = softClipParam->load() > 0.5f;
    const bool bypassOn        = bypassHeld.load();

    // THD Valve ON => engage aussi le limiteur du SoftClipper (brickwall
    // -0.5dBFS + protection true-peak), jusque-la jamais active dans
    // Suppressor -- desactive avec THD, comme une seule fonction "valve".
    clipperL.setHardLimitEnabled (softClipOn);
    clipperR.setHardLimitEnabled (softClipOn);

    float peakInL = 0.0f, peakInR = 0.0f, peakOutL = 0.0f, peakOutR = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = dataL != nullptr ? dataL[i] : 0.0f;
        const float dryR = dataR != nullptr ? dataR[i] : 0.0f;

        peakInL = juce::jmax (peakInL, std::abs (dryL));
        peakInR = juce::jmax (peakInR, std::abs (dryR));

        // Spectre du cadre X/Y (Detector/Audio) : le signal d'ENTREE (avant
        // traitement) est ce sur quoi agissent HP/Bell -- c'est lui qu'on
        // envoie au buffer d'analyse UI (ecriture triviale, cf. header).
        pushSpectrumSample (dataR != nullptr ? 0.5f * (dryL + dryR) : dryL);

        float compL, compR;

        // Bypass momentane (bouton maintenu, voir bypassHeld) : le signal
        // ne traverse plus le compresseur (sortie = signal SEC), mais
        // l'aiguille/LED de GR doit quand meme retomber a 0 en suivant le
        // temps de Release regle par l'utilisateur -- pas de saut net. Le
        // clipper/THD est aussi court-circuite, comme un vrai bypass
        // hardware.
        if (bypassOn)
        {
            compressorL.relaxGainReductionToZero();
            if (dataR != nullptr)
                compressorR.relaxGainReductionToZero();

            compL = dryL;
            compR = dataR != nullptr ? dryR : 0.0f;
        }
        else if (sidechainActive)
        {
            const float scL = scDataL[i];
            const float scR = (scDataR != nullptr) ? scDataR[i] : scL;
            compL = compressorL.processSample (dryL, &scL);
            compR = dataR != nullptr ? compressorR.processSample (dryR, &scR) : 0.0f;
        }
        else
        {
            compL = compressorL.processSample (dryL);
            compR = dataR != nullptr ? compressorR.processSample (dryR) : 0.0f;
        }

        // Etage Soft Clip / THD Valve : optionnel (bouton dedie), et
        // court-circuite pendant le bypass.
        const bool applyClip = softClipOn && ! bypassOn;
        if (dataL != nullptr) dataL[i] = applyClip ? clipperL.processSample (compL) : compL;
        if (dataR != nullptr) dataR[i] = applyClip ? clipperR.processSample (compR) : compR;

        peakOutL = juce::jmax (peakOutL, dataL != nullptr ? std::abs (dataL[i]) : 0.0f);
        peakOutR = juce::jmax (peakOutR, dataR != nullptr ? std::abs (dataR[i]) : 0.0f);

        // Buffer de sortie pour le Crest Factor OUT reel (meme principe
        // que le spectre -- cout negligeable).
        pushOutputSample (dataR != nullptr ? 0.5f * (dataL[i] + dataR[i]) : (dataL != nullptr ? dataL[i] : 0.0f));

        if (detectorLink && dataR != nullptr)
        {
            const float lvlL = compressorL.getFeedbackTapLevel();
            const float lvlR = compressorR.getFeedbackTapLevel();
            const float combined = 0.5f * (lvlL + lvlR); // approx. "somme des deux entrees" du hardware
            compressorL.setFeedbackTapLevel (combined);
            compressorR.setFeedbackTapLevel (combined);
        }
    }

    const float envL = compressorL.getGainReductionDb();
    const float envR = dataR != nullptr ? compressorR.getGainReductionDb() : 0.0f;

    // grDbL/grDbR (NeedleVU global) : magnitude ABSOLUE, direction ignoree.
    const float grDbL = std::abs (envL);
    const float grDbR = std::abs (envR);
    grMeterDbL.store (grDbL);
    grMeterDbR.store (grDbR);

    // GRMeter dedie (plaque du haut) : rangee compression (positif
    // seulement) et rangee Expand dediee (partie boost, negatif de
    // l'enveloppe, jamais melangee a la reduction) -- voir GRMeter.h.
    const float finalGrDb = juce::jmax (juce::jmax (envL, 0.0f), juce::jmax (envR, 0.0f));
    const float finalExpandGrDb = juce::jmax (juce::jmax (-envL, 0.0f), juce::jmax (-envR, 0.0f));
    grMeterDb.store (finalGrDb);
    grMeterExpandDb.store (finalExpandGrDb);
    pushGrHistorySample (finalGrDb); // une valeur reelle par bloc -- precis, pas simule

    inputLevelDbL.store  (juce::Decibels::gainToDecibels (peakInL, -100.0f));
    inputLevelDbR.store  (juce::Decibels::gainToDecibels (peakInR, -100.0f));
    outputLevelDbL.store (juce::Decibels::gainToDecibels (peakOutL, -100.0f));
    outputLevelDbR.store (juce::Decibels::gainToDecibels (peakOutR, -100.0f));

    const float clipAmountL = softClipOn ? clipperL.getClipAmount() : 0.0f;
    const float clipAmountR = softClipOn ? clipperR.getClipAmount() : 0.0f;
    clipMeterAmountL.store (clipAmountL);
    clipMeterAmountR.store (clipAmountR);
    clipMeterAmount.store (juce::jmax (clipAmountL, clipAmountR));
}

juce::AudioProcessorEditor* SuppressorAudioProcessor::createEditor()
{
    return new SuppressorAudioProcessorEditor (*this);
}

void SuppressorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Mode des VU a aiguille : pas des parametres APVTS (n'affecte pas le
    // DSP, ne doit pas etre automatisable), mais doit quand meme survivre
    // a une session sauvegardee -- stocke comme propriete brute du
    // ValueTree racine.
    apvts.state.setProperty ("needleModeIndex1", needleModeIndex1, nullptr);
    apvts.state.setProperty ("needleModeIndex2", needleModeIndex2, nullptr);

    auto state = apvts.copyState();

    // A/B (voir toggleABCompare()) : capture le slot ACTIF avant d'ecrire,
    // pour ne pas perdre les changements faits depuis le dernier switch.
    auto& activeSlot = usingSlotB ? abSlotB : abSlotA;
    for (auto& id : getABParamIds())
        if (auto* p = apvts.getParameter (id))
            activeSlot[id] = p->getValue();

    juce::ValueTree abTree ("AB_COMPARE");
    abTree.setProperty ("usingSlotB", usingSlotB, nullptr);
    for (auto& [id, value] : abSlotA) abTree.setProperty ("A_" + id, value, nullptr);
    for (auto& [id, value] : abSlotB) abTree.setProperty ("B_" + id, value, nullptr);
    state.appendChild (abTree, nullptr);

    juce::MemoryOutputStream stream (destData, true);
    state.writeToStream (stream);
}

void SuppressorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! tree.isValid())
        return;

    abSlotA.clear();
    abSlotB.clear();
    usingSlotB = false;

    if (auto abTree = tree.getChildWithName ("AB_COMPARE"); abTree.isValid())
    {
        usingSlotB = (bool) abTree.getProperty ("usingSlotB", false);

        for (auto& id : getABParamIds())
        {
            if (abTree.hasProperty ("A_" + id))
                abSlotA[id] = (float) abTree.getProperty ("A_" + id);
            if (abTree.hasProperty ("B_" + id))
                abSlotB[id] = (float) abTree.getProperty ("B_" + id);
        }

        tree.removeChild (abTree, nullptr);
    }

    apvts.replaceState (tree);
    needleModeIndex1 = apvts.state.getProperty ("needleModeIndex1", 1);
    needleModeIndex2 = apvts.state.getProperty ("needleModeIndex2", 1);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SuppressorAudioProcessor();
}
