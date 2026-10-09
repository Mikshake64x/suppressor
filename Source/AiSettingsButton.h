#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"
#include "PressAnimator.h"

/**
    AiSettingsButton
    ----------------
    Petite icone engrenage, collee au bouton AI (au-dessus du logo
    Distressor) : ouvre au clic l'OptionsOverlay (section AI -- portee,
    styles, cibles de mastering, etc., voir OptionsOverlay.h). Ce composant
    ne dessine que l'icone et gere le clic -- l'overlay lui-meme est gere
    par l'editeur (voir SuppressorAudioProcessorEditor::showOptionsOverlay()).

    Porte depuis Tripressor (LatencyModeButton.h, dessin d'engrenage
    procedural inchange) -- reaffecte ici a l'ouverture des reglages AI
    plutot qu'a un menu de latence de crossover (Suppressor est mono-bande,
    aucun crossover a regler).
*/
class AiSettingsButton : public juce::Button, private PressAnimator
{
public:
    AiSettingsButton() : juce::Button ("AiSettings")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void buttonStateChanged() override
    {
        if (getState() == juce::Button::buttonDown) notePressed(); else noteReleased();
    }

    void onPressAnimated() override { repaint(); }

    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);
        applyPressTransform (g, bounds);
        const auto centre = bounds.getCentre();
        const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

        const juce::Colour colour = SuppressorColours::textDim.withAlpha (isDown ? 0.55f : (isHighlighted ? 1.0f : 0.8f));
        g.setColour (colour);

        constexpr int numTeeth = 8;
        const float outerR = r * 0.95f;
        const float innerR = r * 0.68f;

        juce::Path gear;
        for (int i = 0; i < numTeeth; ++i)
        {
            const float toothWidth = juce::MathConstants<float>::twoPi / (float) numTeeth * 0.55f;
            const float a0 = (float) i / (float) numTeeth * juce::MathConstants<float>::twoPi - toothWidth * 0.5f;
            const float a1 = a0 + toothWidth;
            gear.addPieSegment (centre.x - outerR, centre.y - outerR, outerR * 2.0f, outerR * 2.0f,
                                 a0, a1, innerR / outerR);
        }
        g.fillPath (gear);

        g.fillEllipse (juce::Rectangle<float> (innerR * 1.65f, innerR * 1.65f).withCentre (centre));

        g.setColour (SuppressorColours::bg);
        g.fillEllipse (juce::Rectangle<float> (innerR * 0.85f, innerR * 0.85f).withCentre (centre));
        g.setColour (colour);
        g.drawEllipse (juce::Rectangle<float> (innerR * 0.85f, innerR * 0.85f).withCentre (centre), 1.1f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiSettingsButton)
};
