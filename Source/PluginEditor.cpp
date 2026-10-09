#include "PluginEditor.h"

SuppressorAudioProcessorEditor::SuppressorAudioProcessorEditor (SuppressorAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);

    // Necessaire pour que le champ de saisie d'un knob (voir KnobSlider)
    // se ferme quand on clique ailleurs sur une zone vide du panneau (qui
    // ne demande pas le focus par elle-meme, sinon rien ne recupere le
    // focus clavier et le champ reste ouvert indefiniment).
    setWantsKeyboardFocus (true);

    // OPTICAL et BRITISH exclusifs -- PAS de radioGroupId JUCE : un vrai
    // radio group force le bouton clique a TOUJOURS passer a "on" (meme
    // s'il l'etait deja), donc cliquer sur le bouton actif ne l'eteignait
    // jamais. Ici chaque bouton reste un toggle libre (clic = bascule son
    // propre etat, y compris pour se rallumer -> "retour au compresseur
    // normal"), et onClick eteint juste l'AUTRE bouton s'il devient actif.
    addAndMakeVisible (opticalButton);
    opticalAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SuppressorAudioProcessor::idOptical, opticalButton);

    addAndMakeVisible (britishButton);
    britishAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SuppressorAudioProcessor::idBritish, britishButton);

    opticalButton.onClick = [this] { if (opticalButton.getToggleState()) britishButton.setToggleState (false, juce::sendNotificationSync); };
    britishButton.onClick = [this] { if (britishButton.getToggleState()) opticalButton.setToggleState (false, juce::sendNotificationSync); };

    // Comp/Expand -- choix a 2 valeurs, cycle au clic (COMP -> EXPAND ->
    // COMP...), meme mecanique que ratioSelector plus bas (ParameterAttachment
    // manuel, pas de ButtonAttachment). La diode reste toujours allumee,
    // seule sa couleur indique le mode actif.
    addAndMakeVisible (aboveBelowButton);
    aboveBelowButton.setClickingTogglesState (false);
    static const char* aboveBelowLabels[2] = { "COMP", "EXPAND" };
    static const juce::Colour aboveBelowColours[2] = { SuppressorColours::ledBlue, SuppressorColours::ledPurple };
    auto* aboveBelowParam = processorRef.apvts.getParameter (SuppressorAudioProcessor::idAboveBelow);
    aboveBelowAttachment = std::make_unique<juce::ParameterAttachment> (
        *aboveBelowParam,
        [this] (float v)
        {
            const int idx = juce::jlimit (0, 1, juce::roundToInt (v));
            aboveBelowButton.setText (aboveBelowLabels[idx]);
            aboveBelowButton.setAccentColour (aboveBelowColours[idx]);
            aboveBelowButton.setToggleState (true, juce::dontSendNotification);
            // Comp et Expand tournent TOUJOURS ensemble en interne (voir
            // PluginProcessor::updateDspFromParams) -- ce choix ne fait que
            // reattacher les knobs/RatioSelector au bon jeu de parametres.
            rebindKnobsForMode (idx);
        },
        nullptr);
    aboveBelowButton.onClick = [this, aboveBelowParam]
    {
        const int current = juce::roundToInt (aboveBelowParam->convertFrom0to1 (aboveBelowParam->getValue()));
        aboveBelowAttachment->setValueAsCompleteGesture ((float) ((current + 1) % 2));
    };

    addAndMakeVisible (sidechainToggle);
    sidechainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SuppressorAudioProcessor::idSidechainEnable, sidechainToggle);

    // BYPASS : momentane -- pas de ButtonAttachment, on pilote directement
    // le bouton via son etat "isDown" (maintenu enfonce = bypass actif).
    addAndMakeVisible (bypassButton);
    bypassButton.setClickingTogglesState (false);
    bypassButton.onStateChange = [this]
    {
        const bool down = bypassButton.isDown();
        bypassButton.setToggleState (down, juce::dontSendNotification);
        processorRef.bypassHeld.store (down);
    };

    // A/B : bascule persistante entre 2 jeux de reglages -- pas de
    // ButtonAttachment (etat gere par le processeur, pas un parametre APVTS).
    addAndMakeVisible (abButton);
    abButton.setIsB (processorRef.isUsingSlotB());
    abButton.onSelect = [this] (bool wantB)
    {
        processorRef.selectABSlot (wantB);
        abButton.setIsB (processorRef.isUsingSlotB());
    };

    addAndMakeVisible (aiLearnButton);
    aiLearnButton.setTooltip ("Ecoute quelques secondes du programme audio et propose Input/Ratio/Attack/Release.");
    aiLearnButton.onClick = [this] { processorRef.startAiLearn(); };

    addAndMakeVisible (aiSettingsButton);
    aiSettingsButton.onClick = [this] { showOptionsOverlay(); };

    addAndMakeVisible (grMeter);

    addAndMakeVisible (ratioSelector);
    auto* ratioParam = processorRef.apvts.getParameter (SuppressorAudioProcessor::idRatioIndex);
    ratioAttachment = std::make_unique<juce::ParameterAttachment> (
        *ratioParam,
        [this] (float v) { ratioSelector.setSelectedIndex ((int) std::round (v)); },
        nullptr);
    ratioSelector.onSelect = [this] (int idx) { ratioAttachment->setValueAsCompleteGesture ((float) idx); };
    ratioAttachment->sendInitialUpdate();

    for (auto* t : { &detectorHpToggle, &detectorBellToggle, &detectorLinkToggle,
                     &audioHpToggle, &audioDist2Toggle, &audioDist3Toggle })
        addAndMakeVisible (*t);

    detectorHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SuppressorAudioProcessor::idDetectorHp, detectorHpToggle);
    detectorBellAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SuppressorAudioProcessor::idDetectorBell, detectorBellToggle);
    detectorLinkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SuppressorAudioProcessor::idDetectorLink, detectorLinkToggle);
    audioHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SuppressorAudioProcessor::idAudioHp, audioHpToggle);
    audioDist2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SuppressorAudioProcessor::idAudioDist2, audioDist2Toggle);
    audioDist3Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SuppressorAudioProcessor::idAudioDist3, audioDist3Toggle);

    addAndMakeVisible (filterGraph);

    setupKnob (inputKnob,   SuppressorAudioProcessor::idInput,   "IN",  " dB", 1);
    setupKnob (attackKnob,  SuppressorAudioProcessor::idAttack,  "ATT", " ms", 2);
    setupKnob (releaseKnob, SuppressorAudioProcessor::idRelease, "REL", " ms", 1);
    setupKnob (outputKnob,  SuppressorAudioProcessor::idOutput,  "OUT", " dB", 1);

    // Expandeur, rack du bas -- toujours visibles, pas besoin de basculer
    // aboveBelowButton pour y acceder (voir doc du membre dans PluginEditor.h).
    setupKnob (rangeExpandKnob,  SuppressorAudioProcessor::idInputExpand,  "RANGE", " dB", 1);
    setupKnob (attackExpandKnob, SuppressorAudioProcessor::idAttackExpand, "ATT",   " ms", 2);

    // Meme degrade violet que les knobs du haut quand ils basculent en
    // Expand (voir rebindKnobsForMode()) -- ici PERMANENT (jamais clear),
    // ces 2 knobs etant l'Expandeur en propre, pas un mode temporaire.
    for (auto* knob : { &rangeExpandKnob, &attackExpandKnob })
        SuppressorLookAndFeel::setKnobAccentGradient (knob->slider, juce::Colour (0xff5a1fb8), SuppressorColours::ledPurple);

    // Envoye APRES setupKnob/ratioSelector ci-dessus (pas juste apres la
    // creation de aboveBelowAttachment plus haut) : le callback appelle
    // rebindKnobsForMode(), qui doit trouver des knobs deja attaches pour
    // pouvoir les reattacher correctement si une session sauvegardee
    // restaure le plugin deja en Expand.
    aboveBelowAttachment->sendInitialUpdate();

    addAndMakeVisible (powerButton);
    powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SuppressorAudioProcessor::idPower, powerButton);

    addAndMakeVisible (needleVu1);
    addAndMakeVisible (needleVu2);

    // Chaque cadran a son PROPRE selecteur, independant de l'autre --
    // l'index choisi est aussi ecrit sur le processeur (needleModeIndex1/2)
    // pour survivre a la fermeture/reouverture de l'editeur et a une
    // session sauvegardee (voir PluginProcessor::getStateInformation).
    needleVu1Modes.onSelect = [this] (int idx) { needleVu1.setMode (NeedleVU::modeForIndex (idx)); processorRef.needleModeIndex1 = idx; };
    needleVu2Modes.onSelect = [this] (int idx) { needleVu2.setMode (NeedleVU::modeForIndex (idx)); processorRef.needleModeIndex2 = idx; };
    addAndMakeVisible (needleVu1Modes);
    addAndMakeVisible (needleVu2Modes);

    addAndMakeVisible (thdValve);
    softClipAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, SuppressorAudioProcessor::idSoftClip, thdValve.engageSwitch);

    addChildComponent (aboutOverlay);
    aboutOverlay.setVisible (false);
    aboutOverlay.onDismiss = [this] { aboutOverlay.setVisible (false); };

    addChildComponent (optionsOverlay);
    optionsOverlay.setVisible (false);
    optionsOverlay.onDismiss = [this] { optionsOverlay.setVisible (false); };

    optionsOverlay.onAiTargetSelected = [this] (int index)
    {
        setParamValue (SuppressorAudioProcessor::idAiMasteringTarget, (float) index);
    };
    optionsOverlay.onAiCompressionModeSelected = [this] (int index)
    {
        setParamValue (SuppressorAudioProcessor::idAiCompressionMode, (float) index);
    };
    optionsOverlay.onAiCharacterSelected = [this] (int index)
    {
        setParamValue (SuppressorAudioProcessor::idAiAttackReleaseCharacter, (float) index);
    };
    optionsOverlay.onAiGainReductionSelected = [this] (int index)
    {
        setParamValue (SuppressorAudioProcessor::idAiGainReductionMode, (float) index);
    };
    optionsOverlay.onAiScopeChanged = [this] (int scopeIndex, bool enabled)
    {
        static constexpr const char* scopeIds[2] =
        {
            SuppressorAudioProcessor::idAiScopeDetectors,
            SuppressorAudioProcessor::idAiScopeCompression,
        };
        if (scopeIndex >= 0 && scopeIndex < 2)
            setParamValue (scopeIds[scopeIndex], enabled ? 1.0f : 0.0f);
    };
    optionsOverlay.onAiStyleSelected = [this] (int index)
    {
        setParamValue (SuppressorAudioProcessor::idAiStyle, (float) index);
    };

    // Mur d'activation (nom + email, une seule fois) -- ajoute en dernier pour
    // peindre par-dessus tout le reste. Visible uniquement tant que
    // l'utilisateur ne s'est pas inscrit (voir RegistrationOverlay).
    addChildComponent (registrationOverlay);
    registrationOverlay.setVisible (! processorRef.freeRegistration->isRegistered());
    registrationOverlay.toFront (false);

    startTimerHz (10); // suit l'etat Power, met a jour lookAndFeel.setPowered()

    setResizable (false, false);
    setSize (1400, 300);
}

SuppressorAudioProcessorEditor::~SuppressorAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SuppressorAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    // Le logo "SUPPRESSOR" (voir paint(), bounds 6,58,166,24) ouvre About
    // -- remplace le monogramme SK retire pour le moment.
    if (juce::Rectangle<int> (6, 58, 166, 24).contains (e.position.toInt()))
    {
        aboutOverlay.setVisible (true);
        aboutOverlay.toFront (true);
    }
}

void SuppressorAudioProcessorEditor::timerCallback()
{
    const bool powered = *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idPower) > 0.5f;
    if (powered != lookAndFeel.isPowered())
    {
        lookAndFeel.setPowered (powered);
        repaint();
    }

    // Bouton AI : des que l'analyse en tache de fond est terminee, on
    // applique le resultat puis on acquitte pour permettre un nouvel
    // apprentissage. AiLearnButton se rafraichit lui-meme via son propre
    // Timer (clignotement) -- on n'a besoin de "poller" ici que pour savoir
    // QUAND appliquer le resultat.
    if (processorRef.aiLearnAnalyzer.getState() == AiLearnAnalyzer::State::Done)
    {
        applyAiLearnResult (processorRef.aiLearnAnalyzer.getResult());
        processorRef.aiLearnAnalyzer.acknowledge();
    }
}

void SuppressorAudioProcessorEditor::applyAiLearnResult (const AiLearnAnalyzer::Result& result)
{
    // Portee (menu Options, section AI, "AI ne s'occupe que de la
    // compression si on le souhaite") -- chaque categorie n'est poussee
    // vers les parametres QUE si son interrupteur etait actif au moment du
    // clic (voir AiLearnAnalyzer::Result). Pas de portee Crossover ici
    // (mono-bande).
    if (result.applyCompressionScope)
    {
        // Chaque axe (Compression Mode/Gain Reduction/Attack-Release
        // Character) a son propre "Off" -- ne pousse SA cible que si son
        // axe n'est pas sur Off.
        if (result.pushInput)
            rampParamTo (SuppressorAudioProcessor::idInput, result.inputDb);
        if (result.pushAttackRelease)
        {
            rampParamTo (SuppressorAudioProcessor::idAttack,  result.attackMs);
            rampParamTo (SuppressorAudioProcessor::idRelease, result.releaseMs);
        }
        if (result.pushRatio)
            setParamValue (SuppressorAudioProcessor::idRatioIndex, (float) juce::roundToInt (result.ratioIndex));
    }

    if (result.applyDetectorsScope)
    {
        setParamValue (SuppressorAudioProcessor::idDetectorBell, result.detectorBellOn ? 1.0f : 0.0f);
        setParamValue (SuppressorAudioProcessor::idDetectorBellFreq, result.detectorBellFreqHz);
        setParamValue (SuppressorAudioProcessor::idDetectorBellGain, result.detectorBellGainDb);
    }

    // Output : jusqu'a 2 sources INDEPENDANTES peuvent y contribuer -- la
    // cible de mastering (LUFS) et le Style (offset statique), chacune
    // activable separement (voir AiLearnAnalyzer::Result). On les
    // additionne et on pousse une seule fois si au moins une est active.
    if (result.applyLoudnessTarget || result.applyStyle)
    {
        float outputDb = 0.0f;
        if (result.applyLoudnessTarget) outputDb += result.outputDb;
        if (result.applyStyle)          outputDb += result.styleOutputDb;
        rampParamTo (SuppressorAudioProcessor::idOutput, outputDb);
    }

    if (result.applyLoudnessTarget && result.recommendSoftClipOn)
        setParamValue (SuppressorAudioProcessor::idSoftClip, 1.0f);
}

void SuppressorAudioProcessorEditor::setParamValue (const juce::String& paramID, float rawValue)
{
    if (auto* p = processorRef.apvts.getParameter (paramID))
        p->setValueNotifyingHost (p->convertTo0to1 (rawValue));
}

void SuppressorAudioProcessorEditor::rampParamTo (const juce::String& paramID, float rawValue, int durationMs)
{
    if (auto* p = processorRef.apvts.getParameter (paramID))
        knobRamper.rampTo (p, p->convertTo0to1 (rawValue), durationMs);
}

void SuppressorAudioProcessorEditor::showOptionsOverlay()
{
    optionsOverlay.setCurrentAiTargetIndex ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiMasteringTarget)));
    optionsOverlay.setCurrentAiCompressionMode ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiCompressionMode)));
    optionsOverlay.setCurrentAiCharacter ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiAttackReleaseCharacter)));
    optionsOverlay.setCurrentAiGainReduction ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiGainReductionMode)));
    optionsOverlay.setAiScopeStates (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiScopeDetectors) > 0.5f,
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiScopeCompression) > 0.5f);
    optionsOverlay.setCurrentAiStyle ((int) std::round (
        *processorRef.apvts.getRawParameterValue (SuppressorAudioProcessor::idAiStyle)));

    optionsOverlay.setVisible (true);
    optionsOverlay.toFront (true);
}

void SuppressorAudioProcessorEditor::setupKnob (KnobControl& knob, const juce::String& paramID, const juce::String& labelText,
                                                 const juce::String& unitSuffix, int decimalPlaces)
{
    addAndMakeVisible (knob.slider);
    knob.slider.setTextValueSuffix (unitSuffix);
    knob.slider.setNumDecimalPlacesToDisplay (decimalPlaces);

    // Double clic sur le corps du knob (hors ecran LCD, voir KnobSlider) :
    // retour a la valeur par defaut du parametre.
    if (auto* param = processorRef.apvts.getParameter (paramID))
        knob.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, paramID, knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centredLeft);
    knob.label.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
    addAndMakeVisible (knob.label);
}

void SuppressorAudioProcessorEditor::rebindKnobsForMode (int mode)
{
    const bool useExpand = (mode == 1);

    auto rebindKnob = [this] (KnobControl& knob, const juce::String& newParamId)
    {
        knob.attachment.reset();
        // Meme comportement double-clic que setupKnob() -- sans ce
        // re-appel, le double-clic remettrait toujours vers l'ancien
        // parametre (ex : 0dB d'Input au lieu de 0dB de Range).
        if (auto* param = processorRef.apvts.getParameter (newParamId))
            knob.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processorRef.apvts, newParamId, knob.slider);
    };

    // Le knob "IN" est REAFFECTE en "RANGE" pour Expand (Comp garde "IN"
    // normal, voir FeedbackCompressor::setExpandRangeDb()).
    inputKnob.label.setText (useExpand ? "RANGE" : "IN", juce::dontSendNotification);

    rebindKnob (inputKnob,   useExpand ? SuppressorAudioProcessor::idInputExpand   : SuppressorAudioProcessor::idInput);
    rebindKnob (attackKnob,  useExpand ? SuppressorAudioProcessor::idAttackExpand  : SuppressorAudioProcessor::idAttack);
    rebindKnob (releaseKnob, useExpand ? SuppressorAudioProcessor::idReleaseExpand : SuppressorAudioProcessor::idRelease);
    // Output : PAS de rebind -- knob PARTAGE entre Comp et Expand (Above et
    // Below tournent toujours ensemble, un seul makeup gain a du sens).

    // Degrade violet sur les 3 knobs mode-dependants en Expand -- retire
    // (retour au gris neutre d'origine) des qu'on repasse sur Comp. Output
    // exclu (knob partage, jamais teinte).
    for (auto* knob : { &inputKnob, &attackKnob, &releaseKnob })
    {
        if (useExpand)
            SuppressorLookAndFeel::setKnobAccentGradient (knob->slider, juce::Colour (0xff5a1fb8), SuppressorColours::ledPurple);
        else
            SuppressorLookAndFeel::clearKnobAccentGradient (knob->slider);
        knob->slider.repaint();
    }

    // Meme mecanique manuelle que le cablage initial du RatioSelector --
    // pas de ButtonAttachment/SliderAttachment possible pour ce composant
    // custom.
    auto* newRatioParam = processorRef.apvts.getParameter (
        useExpand ? SuppressorAudioProcessor::idRatioIndexExpand : SuppressorAudioProcessor::idRatioIndex);
    ratioAttachment = std::make_unique<juce::ParameterAttachment> (
        *newRatioParam,
        [this] (float v) { ratioSelector.setSelectedIndex ((int) std::round (v)); },
        nullptr);
    ratioSelector.onSelect = [this] (int idx) { ratioAttachment->setValueAsCompleteGesture ((float) idx); };
    ratioAttachment->sendInitialUpdate();
}

void SuppressorAudioProcessorEditor::resized()
{
    panelBounds = juce::Rectangle<float> (3.0f, 3.0f, 1394.0f, 144.0f);
    secondPlateBounds = juce::Rectangle<float> (3.0f, 153.0f, 1394.0f, 144.0f);

    // Zone GR/Ratio recalee sur le 1er divider deplace a x=100 (au lieu de
    // 80, pour laisser respirer le logo) -- meme marge de 8px, largeur
    // reduite de 300 a 280 (fin de zone inchangee a x=388, 4px avant le
    // 2e divider a 392). Hauteur portee a 44 (etait 36) pour accueillir la
    // rangee Expand dediee (8px) ajoutee au-dessus des diodes de reduction.
    grMeter.setBounds (108, 20, 280, 44);

    // AI + engrenage (au-dessus du logo) et BYPASS (sous le logo) --
    // colonne de gauche, avant le 1er divider (x=100), meme zone que le
    // logo (voir paint()). AI/engrenage tiennent entre le bord (x=6) et le
    // divider, au-dessus du logo (qui commence a y=58) ; BYPASS de meme,
    // en dessous (le logo se termine a y=82).
    aiLearnButton.setBounds (6, 28, 62, 24);
    aiSettingsButton.setBounds (72, 28, 22, 24);
    bypassButton.setBounds (6, 84, 88, 24);
    abButton.setBounds (6, 112, 88, 24);

    // OPTICAL + BRITISH + SC (sidechain) decales a droite du logo (qui
    // s'etend maintenant jusque sous GAIN REDUCTION) -- resserres
    // (84->76) pour caser le 3e bouton dans la meme rangee, zone
    // 178-388 (210px) : 76+6+76+8+44 = 210, pile.
    opticalButton.setBounds (178, 64, 76, 24);
    britishButton.setBounds (260, 64, 76, 24);
    sidechainToggle.setBounds (344, 64, 44, 24);

    // Comp/Expand : espace libre entre le GR meter (x=108-388) et OPTICAL
    // (x=178) sur cette rangee -- meme y que OPTICAL/BRITISH/SC.
    aboveBelowButton.setBounds (108, 64, 66, 24);

    // Boutons Ratio callés en bas de leur emplacement (contre la ligne du
    // bas, y=140), au lieu d'etre colles sous le GR meter.
    ratioSelector.setBounds (108, 96, 280, 44);

    // Centre entre les 2 barres blanches (dividers a x=392 et x=612,
    // zone de 220px) : contenu large de 178px (86+6+86), marge (220-178)/2
    // = 21 de chaque cote -> demarre a 392+21=413 (au lieu de 402).
    detectorHpToggle.setBounds   (413, 24, 86, 24);
    audioHpToggle.setBounds      (505, 24, 86, 24);
    detectorBellToggle.setBounds (413, 52, 86, 24);
    audioDist2Toggle.setBounds   (505, 52, 86, 24);
    detectorLinkToggle.setBounds (413, 80, 86, 24);
    audioDist3Toggle.setBounds   (505, 80, 86, 24);

    filterGraph.setBounds (413, 110, 178, 30);

    // Knobs : taille/position INCHANGEES. Seul le libelle change (IN/ATT/
    // REL/OUT, coin bas-gauche du knob au lieu de centre en dessous).
    auto layoutKnob = [] (KnobControl& knob, int cx, int cy, int diameter)
    {
        knob.slider.setBounds (cx - diameter / 2, cy - diameter / 2, diameter, diameter);
        const int lw = 32, lh = 12;
        knob.label.setBounds (cx - diameter / 2 + 2, cy + diameter / 2 - lh - 2, lw, lh);
    };

    layoutKnob (inputKnob,   702,  72, 124);
    layoutKnob (attackKnob,  872,  72, 124);
    layoutKnob (releaseKnob, 1042, 72, 124);
    layoutKnob (outputKnob,  1212, 72, 124);

    // Agrandi (etait 46x68) -- trop petit dans la zone dediee (x>1302).
    powerButton.setBounds (1313, 42, 70, 92);

    // Plaque du bas, de gauche a droite : les 2 knobs Expandeur (Range/
    // Attack, meme taille EXACTE que ceux du haut), puis les 2 VU a
    // aiguille (chacun avec son propre selecteur de mode a gauche du
    // cadran, desormais de largeur IDENTIQUE -- avant il y avait 6px
    // d'ecart entre les deux, artefact d'un ajustement vis qui ne se
    // justifie plus vu le reste du rack), puis THD Valve (retrecie,
    // voir ThdValveModule -- l'ecran barre a ete retire, ne reste que le
    // scope + le %). Meme enveloppe globale que l'ancien agencement
    // (x=26..1371) pour rester aligne sur les vis de coin de la plaque.
    layoutKnob (rangeExpandKnob,  88,  227, 124);
    layoutKnob (attackExpandKnob, 222, 227, 124);

    auto layoutNeedle = [] (NeedleVU& needle, NeedleModeSelector& modes, int x, int y, int unitW, int h)
    {
        constexpr int columnW = 54, gap = 6;
        modes.setBounds (x, y, columnW, h);
        needle.setBounds (x + columnW + gap, y, unitW - columnW - gap, h);
    };

    layoutNeedle (needleVu1, needleVu1Modes, 304, 166, 303, 122);
    layoutNeedle (needleVu2, needleVu2Modes, 627, 166, 303, 122);

    thdValve.setBounds (950, 166, 421, 122);

    aboutOverlay.setBounds (getLocalBounds());
    optionsOverlay.setBounds (getLocalBounds());
    registrationOverlay.setBounds (getLocalBounds());

    rebuildBackgroundCache();
}

void SuppressorAudioProcessorEditor::rebuildBackgroundCache()
{
    const int w = getWidth();
    const int h = getHeight();

    backgroundCache = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (backgroundCache);

    // Chassis (plaque de fond) sur tout le contour -- visible dans la fine
    // bordure et l'interstice entre les 2 plaques, qui elles recoivent leur
    // propre remplissage (renderMetalPanel) + une ombre portee, pour se
    // detacher visuellement comme des plaques vissees SUR ce chassis.
    SuppressorLookAndFeel::renderChassisBackplate (g, backgroundCache.getBounds().toFloat());

    const juce::DropShadow plateShadow (juce::Colours::black.withAlpha (0.6f), 7, { 0, 2 });

    // --- Plaque principale (1U, haut) ---
    {
        juce::Path p;
        p.addRoundedRectangle (panelBounds, 6.0f);
        plateShadow.drawForPath (g, p);
    }
    SuppressorLookAndFeel::renderMetalPanel (g, panelBounds);
    SuppressorLookAndFeel::renderCornerScrews (g, panelBounds, 15.0f, 5.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (panelBounds.reduced (0.5f), 6.0f, 1.0f);

    // Effet extrusion : socle legerement encastre derriere chaque knob
    // (memes centres/diametre que layoutKnob dans resized()).
    for (int cx : { 702, 872, 1042, 1212 })
        SuppressorLookAndFeel::renderKnobSocket (g, { (float) cx, 72.0f }, 63.0f);

    // Idem pour les 2 knobs Expandeur du rack du bas (meme centres que
    // layoutKnob (rangeExpandKnob/attackExpandKnob, ...) dans resized()).
    for (int cx : { 88, 222 })
        SuppressorLookAndFeel::renderKnobSocket (g, { (float) cx, 227.0f }, 63.0f);

    // --- Plaque de mesure (1U, bas) -- memes vis que la plaque du haut. ---
    {
        juce::Path p;
        p.addRoundedRectangle (secondPlateBounds, 6.0f);
        plateShadow.drawForPath (g, p);
    }
    SuppressorLookAndFeel::renderMetalPanel (g, secondPlateBounds);
    SuppressorLookAndFeel::renderCornerScrews (g, secondPlateBounds, 15.0f, 5.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (secondPlateBounds.reduced (0.5f), 6.0f, 1.0f);

    // Le logo tient maintenant ENTIEREMENT dans la colonne de gauche (voir
    // paint()) -- il ne traverse plus cette ligne (x=100), qui redevient
    // continue.
    SuppressorLookAndFeel::renderDividerLine (g, 100.0f, 10.0f, 140.0f);
    SuppressorLookAndFeel::renderDividerLine (g, 392.0f, 10.0f, 140.0f);
    SuppressorLookAndFeel::renderDividerLine (g, 612.0f, 10.0f, 140.0f);
    SuppressorLookAndFeel::renderDividerLine (g, 1302.0f, 10.0f, 140.0f);

    // Rack du bas : 3 dividers separant les 4 zones (knobs Expandeur | VU L
    // | VU R | THD Valve), aux milieux des interstices de resized().
    SuppressorLookAndFeel::renderDividerLine (g, 294.0f, 163.0f, 290.0f);
    SuppressorLookAndFeel::renderDividerLine (g, 617.0f, 163.0f, 290.0f);
    SuppressorLookAndFeel::renderDividerLine (g, 940.0f, 163.0f, 290.0f);
}

void SuppressorAudioProcessorEditor::paint (juce::Graphics& g)
{
    if (backgroundCache.isValid())
        g.drawImageAt (backgroundCache, 0, 0);
    else
        SuppressorLookAndFeel::renderChassisBackplate (g, getLocalBounds().toFloat());

    // Logotype "SUPPRESSOR" -- caps, bold italic, kerning resserre, style
    // wordmark hardware. Il debordait avant a droite sous la rangee COMP/
    // OPTICAL/OVERKILL/SC (x>=108), qui le recouvrait -> le "R" final etait
    // coupe. Il est maintenant calcule pour tenir EXACTEMENT dans la colonne
    // de gauche (entre le bord et la ligne x=100, meme largeur que AI/
    // BYPASS/A-B) : taille max 22pt, reduite au besoin pour que la largeur
    // reelle du texte rentre. Pas de soulignement (le vrai n'en a pas).
    {
        constexpr float logoMaxW = 90.0f;                  // colonne x=5..95
        const juce::String logoText ("SUPPRESSOR");
        const auto logoFlags = juce::Font::bold | juce::Font::italic;

        juce::GlyphArrangement ga;
        ga.addLineOfText (juce::Font (juce::FontOptions (22.0f, logoFlags)).withExtraKerningFactor (-0.02f),
                          logoText, 0.0f, 0.0f);
        const float widthAt22 = ga.getBoundingBox (0, -1, true).getWidth();
        const float logoSize = juce::jmin (22.0f, 22.0f * logoMaxW / juce::jmax (1.0f, widthAt22));

        g.setColour (SuppressorColours::textBright);
        g.setFont (juce::Font (juce::FontOptions (logoSize, logoFlags)).withExtraKerningFactor (-0.02f));
        g.drawFittedText (logoText, { 5, 54, (int) logoMaxW, 28 }, juce::Justification::centred, 1);
    }

    g.setColour (SuppressorColours::textBright);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)).withExtraKerningFactor (0.1f));
    g.drawFittedText ("GAIN REDUCTION / EXPAND", { 108, 4, 280, 12 }, juce::Justification::centred, 1);

    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)).withExtraKerningFactor (0.1f));
    g.drawFittedText ("DETECTOR", { 413, 4, 86, 14 }, juce::Justification::centred, 1);
    g.drawFittedText ("AUDIO",    { 505, 4, 86, 14 }, juce::Justification::centred, 1);

    g.setColour (SuppressorColours::textDim);
    g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)).withExtraKerningFactor (0.1f));
    g.drawFittedText ("POWER", { 1313, 24, 70, 14 }, juce::Justification::centred, 1);

    // Rack du bas : les 2 knobs Range/Attack sont TOUJOURS l'Expandeur
    // (pas de bascule Comp/Expand ici) -- l'etiquette le precise, teintee
    // violette comme les knobs eux-memes (voir leur setKnobAccentGradient
    // dans le constructeur).
    g.setColour (SuppressorColours::ledPurple);
    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)).withExtraKerningFactor (0.1f));
    g.drawFittedText ("EXPANDER", { 26, 154, 258, 11 }, juce::Justification::centred, 1);
}
