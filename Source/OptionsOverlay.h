#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"
#include "ToggleSwitch.h"

/**
    OptionsOverlay
    --------------
    Voile plein ecran affiche au clic sur l'engrenage (AiSettingsButton).
    Meme langage visuel que AboutOverlay (voile noir + panneau vitre
    encadre ambre).

    Contrairement a la version Tripressor (3 bandes), Suppressor est
    MONO-BANDE : pas de section GENERAL (taille de fenetre, budget CPU du
    crossover LINEAR) -- retiree sur demande explicite ("tu peux retirer
    le changement de taille ainsi que les precisions de crossover vu que
    c'est un monobande"). Ce voile ne contient donc QUE la section AI.

    Le panneau prend TOUTE la largeur du plugin (demande explicite : "le
    menu option peut prendre toute la largeur du VST") et chaque categorie
    tient sur UNE seule rangee (titre COMPLET au-dessus -- demande
    explicite : on doit voir que ces boutons touchent aux parametres AI,
    ET LEQUEL), le tout SANS scroll (demande explicite : "tu peux
    surement tout faire tenir sans devoir scroll").

    Les choix utilisent desormais ToggleSwitch (meme composant que
    DETECTOR/AUDIO/SC dans le panneau principal -- diode a gauche, fond
    mat degrade, ombre portee) au lieu de juce::TextButton brut : demande
    explicite ("c'est vraiment moche") -- l'aspect pilule plate generique
    de TextButton ne correspondait pas a l'esthetique "rack hardware" du
    reste du plugin.
      - AI MASTERING TARGET : cible de loudness (LUFS) par plateforme,
        radio ("OFF" = comportement d'origine).
      - AI COMPRESSION MODE : intensite generale (Off/Soft/Medium/Hard).
      - AI ATTACK/RELEASE CHARACTER : forme (Off/Smooth/Medium/Punchy).
      - AI GAIN REDUCTION : combien de compression au total
        (Off/Smooth/Mid/Hard) -- axe supplementaire, independant des 2
        ci-dessus. Chacun de ces 3 axes a son propre "Off" -- Off n'ecrit
        QUE sa propre cible (Input/Ratio/Attack+Release), le reste
        continue d'etre pousse normalement.
      - AI SCOPE : 2 interrupteurs INDEPENDANTS (Detecteurs/Compression) --
        PAS de Crossover ici (rien a scinder sur une seule bande).
      - AI STYLE : offset d'Output par type de source -- liste ETENDUE
        (demande explicite) avec les styles musicaux d'origine (Bass/Loud/
        Rock/Pop/Electronic) PLUS des types d'instrument/source (Guitar,
        Synth, Drum Buss, Vocal, Kick, Snare, Tom, Horn, Mastering, Bass
        Synth, Percussions) -- sur 2 rangees (17 boutons ne tiennent pas
        lisiblement sur une seule).
    PAS de section AI FIDELITY : cette fonction de Tripressor restaure
    l'equilibre RELATIF entre bandes apres compression, ce qui n'a pas de
    sens sur une unite a bande unique -- retiree ici.

    Se ferme au clic hors du panneau, ou via le bouton CLOSE / la croix.
*/
class OptionsOverlay : public juce::Component
{
public:
    OptionsOverlay()
    {
        addAndMakeVisible (aiPanel);
        aiPanel.build();

        // Croix de fermeture en haut a droite du panneau -- seul bouton de
        // fermeture (en plus du clic hors du panneau) : le bouton CLOSE du
        // bas etait redondant avec elle, retire pour recuperer de la place
        // verticale (demande explicite).
        addAndMakeVisible (closeCrossButton);
        closeCrossButton.setButtonText ("X");
        closeCrossButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        closeCrossButton.setColour (juce::TextButton::textColourOffId, SuppressorColours::textDim);
        closeCrossButton.onClick = [this] { if (onDismiss) onDismiss(); };
    }

    void setCurrentAiTargetIndex (int idx)     { aiPanel.setCurrentAiTargetIndex (idx); }
    void setCurrentAiCompressionMode (int idx) { aiPanel.setCurrentAiCompressionMode (idx); }
    void setCurrentAiCharacter (int idx)       { aiPanel.setCurrentAiCharacter (idx); }
    void setCurrentAiGainReduction (int idx)   { aiPanel.setCurrentAiGainReduction (idx); }
    void setAiScopeStates (bool detectorsOn, bool compressionOn)
    {
        aiPanel.setAiScopeStates (detectorsOn, compressionOn);
    }
    void setCurrentAiStyle (int idx) { aiPanel.setCurrentAiStyle (idx); }

    void resized() override
    {
        auto bounds = getLocalBounds().toFloat();
        // Prend quasiment toute la fenetre du plugin -- demande explicite
        // ("toute la largeur du VST"). Vu la hauteur fixe tres reduite du
        // plugin (300px), le panneau prend aussi presque toute la hauteur
        // disponible : c'est ce qui permet a AiPanel::layout() de caser
        // les 6 categories sans scroll (voir doc de classe).
        panelBounds = bounds.reduced (8.0f);
        auto content = panelBounds.reduced (20.0f, 10.0f);

        titleArea = content.removeFromTop (14.0f).toNearestInt();
        content.removeFromTop (3.0f); // ligne + marge

        aiPanel.setBounds (content.toNearestInt());
        aiPanel.layout (juce::roundToInt (content.getWidth()));

        closeCrossButton.setBounds (juce::Rectangle<int> (20, 20)
            .withCentre ({ (int) panelBounds.getRight() - 14, (int) panelBounds.getY() + 11 }));
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.88f));
        g.fillRect (bounds);

        SuppressorLookAndFeel::renderGlassPanel (g, panelBounds, 12.0f);
        SuppressorLookAndFeel::renderGlossySheen (g, panelBounds);
        g.setColour (SuppressorColours::ledAmber.withAlpha (0.5f));
        g.drawRoundedRectangle (panelBounds.reduced (0.5f), 12.0f, 1.0f);

        g.setColour (SuppressorColours::ledAmber);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.1f));
        g.drawFittedText ("OPTIONS", titleArea, juce::Justification::centredLeft, 1);

        g.setColour (SuppressorColours::gridLine);
        const float lineY = (float) titleArea.getBottom() + 4.0f;
        g.drawLine ((float) titleArea.getX(), lineY, panelBounds.getRight() - 20.0f, lineY, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! panelBounds.contains (e.position) && onDismiss)
            onDismiss();
    }

    std::function<void()> onDismiss;
    std::function<void(int)> onAiTargetSelected;
    std::function<void(int)> onAiCompressionModeSelected;
    std::function<void(int)> onAiCharacterSelected;
    std::function<void(int)> onAiGainReductionSelected;
    // scopeIndex : 0=Detectors, 1=Compression.
    std::function<void(int scopeIndex, bool enabled)> onAiScopeChanged;
    std::function<void(int)> onAiStyleSelected;

private:
    // Contenu de la section AI -- chaque categorie = titre pleine largeur
    // au-dessus d'une rangee de ToggleSwitch (voir doc de classe
    // d'OptionsOverlay). Plus de Viewport/scroll.
    class AiPanel : public juce::Component
    {
    public:
        explicit AiPanel (OptionsOverlay& ownerIn) : owner (ownerIn) {}

        void build()
        {
            static const char* aiTargetLabels[kNumAiTargets] =
                { "OFF", "APPLE MUSIC", "YOUTUBE", "SPOTIFY", "DEEZER", "SOUNDCLOUD", "TV", "CD", "SIDE KEY" };
            static const char* compressionLabels[kNumCompressionModes] = { "OFF", "SOFT", "MEDIUM", "HARD" };
            static const char* characterLabels[kNumAttackReleaseCharacters] = { "OFF", "SMOOTH", "MEDIUM", "PUNCHY" };
            static const char* grLabels[kNumGainReductionModes] = { "OFF", "SMOOTH", "MID", "HARD" };
            static const char* scopeLabels[kNumScopes] = { "DETECTORS", "COMPRESSION" };
            static const char* styleLabels[kNumStyles] =
            {
                "OFF", "BASS", "LOUD", "ROCK", "POP", "ELECTRONIC",
                "GUITAR", "SYNTH", "DRUM BUSS", "VOCAL", "KICK", "SNARE",
                "TOM", "HORN", "MASTERING", "BASS SYNTH", "PERCUSSIONS"
            };

            auto buildGroup = [this] (juce::OwnedArray<ToggleSwitch>& group, const char* const* labels, int count,
                                       int radioGroupId, juce::Colour accent, std::function<void(int)> onSelect)
            {
                for (int i = 0; i < count; ++i)
                {
                    auto* btn = new ToggleSwitch (labels[i], accent);
                    if (radioGroupId != 0)
                        btn->setRadioGroupId (radioGroupId, juce::dontSendNotification);
                    // Appele a CHAQUE clic, meme quand le bouton passe a
                    // OFF -- necessaire pour les cases INDEPENDANTES (AI
                    // SCOPE, radioGroupId=0) : ne se declencher que si
                    // getToggleState() est vrai (comme avant) ignorait
                    // silencieusement le decochage, le parametre restait
                    // bloque a son ancienne valeur "on" et reapparaissait
                    // coche a la reouverture du menu -- bug remonte
                    // explicitement ("quand je ferme le menu ca ne garde
                    // pas les reglages"). Sans effet sur les groupes radio
                    // (leur bouton clique est toujours a true de toute
                    // facon, seul lui recoit onClick).
                    btn->onClick = [i, onSelect]
                    {
                        if (onSelect)
                            onSelect (i);
                    };
                    group.add (btn);
                    addAndMakeVisible (btn);
                }
            };

            buildGroup (aiTargetButtons, aiTargetLabels, kNumAiTargets, 9103, SuppressorColours::ledCyan,
                        [this] (int i) { if (owner.onAiTargetSelected) owner.onAiTargetSelected (i); });
            buildGroup (compressionModeButtons, compressionLabels, kNumCompressionModes, 9104, SuppressorColours::ledCyan,
                        [this] (int i) { if (owner.onAiCompressionModeSelected) owner.onAiCompressionModeSelected (i); });
            buildGroup (characterButtons, characterLabels, kNumAttackReleaseCharacters, 9105, SuppressorColours::ledCyan,
                        [this] (int i) { if (owner.onAiCharacterSelected) owner.onAiCharacterSelected (i); });
            buildGroup (gainReductionButtons, grLabels, kNumGainReductionModes, 9106, SuppressorColours::ledCyan,
                        [this] (int i) { if (owner.onAiGainReductionSelected) owner.onAiGainReductionSelected (i); });
            // AI SCOPE -- 2 interrupteurs INDEPENDANTS (pas de radioGroupId :
            // pas de Crossover a exclure sur une unite mono-bande, chaque
            // case se coche/decoche librement, comme DETECTOR/AUDIO).
            buildGroup (scopeButtons, scopeLabels, kNumScopes, 0, SuppressorColours::ledGreen,
                        [this] (int i) { if (owner.onAiScopeChanged) owner.onAiScopeChanged (i, scopeButtons.getUnchecked (i)->getToggleState()); });
            buildGroup (styleButtons, styleLabels, kNumStyles, 9107, SuppressorColours::ledCyan,
                        [this] (int i) { if (owner.onAiStyleSelected) owner.onAiStyleSelected (i); });

            for (auto* lbl : { &targetLabel, &compressionLabel, &characterLabel, &gainReductionLabel, &scopeLabel, &styleLabel })
            {
                lbl->setJustificationType (juce::Justification::centredLeft);
                lbl->setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)).withExtraKerningFactor (0.06f));
                lbl->setColour (juce::Label::textColourId, SuppressorColours::ledBlue);
                addAndMakeVisible (*lbl);
            }
            // Titres complets (comme l'original Tripressor) -- demande
            // explicite : des libelles trop courts ne disent pas que ces
            // boutons touchent aux parametres AI, ni LEQUEL.
            targetLabel.setText ("AI MASTERING TARGET (LUFS)", juce::dontSendNotification);
            compressionLabel.setText ("AI COMPRESSION MODE", juce::dontSendNotification);
            characterLabel.setText ("AI ATTACK / RELEASE CHARACTER", juce::dontSendNotification);
            gainReductionLabel.setText ("AI GAIN REDUCTION", juce::dontSendNotification);
            scopeLabel.setText ("AI SCOPE (WHAT THE AI BUTTON IS ALLOWED TO TOUCH)", juce::dontSendNotification);
            styleLabel.setText ("AI STYLE (OUTPUT BALANCE)", juce::dontSendNotification);
        }

        void setCurrentAiTargetIndex (int idx)
        {
            for (int i = 0; i < kNumAiTargets; ++i)
                aiTargetButtons.getUnchecked (i)->setToggleState (i == idx, juce::dontSendNotification);
        }

        void setCurrentAiCompressionMode (int idx)
        {
            for (int i = 0; i < kNumCompressionModes; ++i)
                compressionModeButtons.getUnchecked (i)->setToggleState (i == idx, juce::dontSendNotification);
        }

        void setCurrentAiCharacter (int idx)
        {
            for (int i = 0; i < kNumAttackReleaseCharacters; ++i)
                characterButtons.getUnchecked (i)->setToggleState (i == idx, juce::dontSendNotification);
        }

        void setCurrentAiGainReduction (int idx)
        {
            for (int i = 0; i < kNumGainReductionModes; ++i)
                gainReductionButtons.getUnchecked (i)->setToggleState (i == idx, juce::dontSendNotification);
        }

        void setAiScopeStates (bool detectorsOn, bool compressionOn)
        {
            scopeButtons.getUnchecked (0)->setToggleState (detectorsOn,   juce::dontSendNotification);
            scopeButtons.getUnchecked (1)->setToggleState (compressionOn, juce::dontSendNotification);
        }

        void setCurrentAiStyle (int idx)
        {
            for (int i = 0; i < kNumStyles; ++i)
                styleButtons.getUnchecked (i)->setToggleState (i == idx, juce::dontSendNotification);
        }

        // Chaque categorie = titre COMPLET pleine largeur au-dessus d'une
        // rangee de ToggleSwitch compacts -- dimensionne pour que les 6
        // categories tiennent ENTIERES dans la hauteur fixe du plugin
        // (300px), sans scroll (voir doc de classe d'OptionsOverlay). Le
        // Style (17 choix) prend 2 demi-rangees au lieu d'une pour rester
        // lisible.
        void layout (int width)
        {
            constexpr float labelH = 11.0f;
            constexpr float labelGap = 1.0f;
            constexpr float gap = 5.0f;
            constexpr float rowH = 20.0f;
            constexpr float subRowGap = 1.0f;  // entre les 2 demi-rangees du Style
            constexpr float rowGap = 3.0f;      // entre categories

            float y = 0.0f;

            auto placeRow = [&] (juce::Label& label, juce::OwnedArray<ToggleSwitch>& buttons, int count, int perRow = 0)
            {
                label.setBounds (juce::Rectangle<float> (0.0f, y, (float) width, labelH).toNearestInt());
                y += labelH + labelGap;

                const int cols = perRow > 0 ? perRow : count;
                const int numRows = (count + cols - 1) / cols;
                const float bw = ((float) width - gap * (cols - 1)) / (float) cols;

                for (int r = 0; r < numRows; ++r)
                {
                    const int rowStart = r * cols;
                    const int rowCount = juce::jmin (cols, count - rowStart);
                    float x = 0.0f;
                    for (int i = 0; i < rowCount; ++i)
                    {
                        buttons.getUnchecked (rowStart + i)->setBounds (
                            juce::Rectangle<float> (x, y + (float) r * (rowH + subRowGap), bw, rowH).toNearestInt());
                        x += bw + gap;
                    }
                }

                y += rowH * (float) numRows + subRowGap * (float) (numRows - 1) + rowGap;
            };

            placeRow (targetLabel,         aiTargetButtons,         kNumAiTargets);
            placeRow (compressionLabel,    compressionModeButtons,  kNumCompressionModes);
            placeRow (characterLabel,      characterButtons,        kNumAttackReleaseCharacters);
            placeRow (gainReductionLabel,  gainReductionButtons,    kNumGainReductionModes);
            placeRow (scopeLabel,          scopeButtons,            kNumScopes);
            placeRow (styleLabel,          styleButtons,            kNumStyles, 9);
        }

    private:
        static constexpr int kNumAiTargets = 9;
        static constexpr int kNumCompressionModes = 4;
        static constexpr int kNumAttackReleaseCharacters = 4;
        static constexpr int kNumGainReductionModes = 4;
        static constexpr int kNumScopes = 2;
        static constexpr int kNumStyles = 17;

        OptionsOverlay& owner;

        juce::OwnedArray<ToggleSwitch> aiTargetButtons;
        juce::OwnedArray<ToggleSwitch> compressionModeButtons;
        juce::OwnedArray<ToggleSwitch> characterButtons;
        juce::OwnedArray<ToggleSwitch> gainReductionButtons;
        juce::OwnedArray<ToggleSwitch> scopeButtons;
        juce::OwnedArray<ToggleSwitch> styleButtons;

        juce::Label targetLabel, compressionLabel, characterLabel, gainReductionLabel, scopeLabel, styleLabel;
    };

    juce::TextButton closeCrossButton;

    AiPanel aiPanel { *this };

    juce::Rectangle<float> panelBounds;
    juce::Rectangle<int> titleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptionsOverlay)
};
