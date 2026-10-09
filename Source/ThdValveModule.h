#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"
#include "ThdEngageSwitch.h"

/**
    ThdValveModule
    --------------
    "THD Valve" -- port du scope + pourcentage THD de M76 (SabotageThdScope
    + SabotageThdMeter fusionnes en un seul module), recolore sur la
    palette Suppressor. L'ecran barre horizontale du meter M76 d'origine a
    ete retire (gain de place dans le rack du bas) -- ne reste que le scope
    circulaire + le %, tous deux pilotes par le SoftClipper existant
    (processorRef.clipMeterAmount, 0..1 -- 0 si SOFTCLIP est desengage).

    L'actionneur ON/OFF est le sprite ThdEngageSwitch (Button-08-S-
    Filmstrip.png, engageSwitch, membre public attache au parametre
    SOFTCLIP par PluginEditor) -- CE module reste un simple Component
    d'affichage (scope + barre + diode d'etat), pas un bouton lui-meme.
*/
class ThdValveModule : public juce::Component, private juce::Timer
{
public:
    ThdValveModule (std::atomic<float>& clipAmountToWatch, std::atomic<float>& softClipOnToWatch)
        : clipAmount (clipAmountToWatch), softClipOnParam (softClipOnToWatch)
    {
        setPaintingIsUnclipped (true);
        addAndMakeVisible (engageSwitch);
        startTimerHz (30);
    }

    ~ThdValveModule() override { stopTimer(); }

    // Actionneur physique -- attache a idSoftClip via ButtonAttachment
    // dans PluginEditor (comme n'importe quel autre ToggleSwitch).
    ThdEngageSwitch engageSwitch;

    // Colonne de gauche reservee au gros bouton d'engagement -- doit
    // rester IDENTIQUE entre resized() (position reelle du bouton) et
    // rebuildStaticCache() (espace reserve/vide dans le decor peint).
    static constexpr float engageColW = 76.0f;

    void resized() override
    {
        staticCacheDirty = true;

        auto content = getLocalBounds().toFloat().reduced (16.0f, 14.0f);
        auto engageCol = content.removeFromLeft (engageColW);
        const float btnSize = juce::jmin (64.0f, engageCol.getWidth(), content.getHeight());
        engageSwitch.setBounds (juce::Rectangle<float> (btnSize, btnSize).withCentre (engageCol.getCentre()).toNearestInt());
    }

    void paint (juce::Graphics& g) override
    {
        const auto* laf = dynamic_cast<SuppressorLookAndFeel*> (&getLookAndFeel());
        const bool powered = (laf == nullptr) || laf->isPowered();
        if (staticCacheDirty || powered != cachedPowered)
            rebuildStaticCache (powered);

        g.drawImageAt (staticCache, 0, 0);

        const bool engaged = softClipOnParam.load() > 0.5f;
        const float dim = powered ? 1.0f : 0.18f;
        const juce::Colour levelColour = powered ? SuppressorLookAndFeel::ledGradientColour (displayedAmount) : SuppressorColours::textDim;

        // --- Diode d'etat (seul indicateur ON/OFF, pas de rideau) ---
        {
            const float ledDiameter = 8.0f;
            auto ledBounds = juce::Rectangle<float> (ledDiameter, ledDiameter).withCentre (ledCentre);
            const bool ledLit = powered && engaged;
            g.setColour (ledLit ? SuppressorColours::ledAmber : SuppressorColours::ledAmber.interpolatedWith (juce::Colours::black, 0.85f));
            g.fillEllipse (ledBounds);
        }

        // --- Scope circulaire : crosshair + onde, couleur suit le THD courant ---
        {
            const auto c = scopeBounds.getCentre();
            const float r = juce::jmin (scopeBounds.getWidth(), scopeBounds.getHeight()) * 0.38f;

            g.setColour (levelColour.withAlpha (0.12f * dim));
            g.drawLine (c.x - r * 0.8f, c.y, c.x + r * 0.8f, c.y, 0.6f);
            g.drawLine (c.x, c.y - r * 0.8f, c.x, c.y + r * 0.8f, 0.6f);

            juce::Path wave;
            constexpr int numPts = 48;
            const float waveW = r * 1.7f;
            const float waveH = r * 0.62f;
            const float drive = 1.0f + displayedAmount * 7.0f;

            for (int i = 0; i < numPts; ++i)
            {
                const float xt = (float) i / (float) (numPts - 1);
                const float x = c.x - waveW * 0.5f + xt * waveW;
                const float raw = std::sin (xt * juce::MathConstants<float>::twoPi * 2.0f);
                const float shaped = std::tanh (raw * drive) / std::tanh (drive);
                const float y = c.y - shaped * waveH;

                if (i == 0) wave.startNewSubPath (x, y);
                else wave.lineTo (x, y);
            }

            g.setColour (levelColour.withAlpha (0.3f * dim));
            g.strokePath (wave, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (levelColour.withAlpha (dim));
            g.strokePath (wave, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // --- Pourcentage THD (l'ecran barre horizontale a ete retire pour
        // gagner de la place dans le rack du bas -- scope + % suffisent) ---
        {
            auto pctArea = meterPctBounds;

            auto pctTop = pctArea.removeFromTop (pctArea.getHeight() * 0.66f);
            g.setColour (levelColour.withAlpha (dim));
            g.setFont (juce::Font (juce::FontOptions (juce::jmin (24.0f, pctTop.getHeight() * 0.8f), juce::Font::bold)));
            g.drawFittedText (juce::String (powered ? displayedAmount * 100.0f : 0.0f, 1) + "%",
                               pctTop.toNearestInt(), juce::Justification::centred, 1);

            g.setColour (SuppressorColours::textDim.withAlpha (dim));
            g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)).withExtraKerningFactor (0.14f));
            g.drawFittedText ("THD", pctArea.toNearestInt(), juce::Justification::centred, 1);
        }
    }

private:
    void rebuildStaticCache (bool powered)
    {
        const int W = getWidth(), H = getHeight();
        staticCache = juce::Image (juce::Image::ARGB, juce::jmax (1, W), juce::jmax (1, H), true);
        juce::Graphics g (staticCache);

        // --- Plaque du module (meme langage que MeasurementDisplay) ---
        auto bounds = getLocalBounds().toFloat();
        juce::ColourGradient frameGrad (juce::Colour (0xff3c3f44), bounds.getX(), bounds.getY(),
                                         juce::Colour (0xff1a1c1f), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill (frameGrad);
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (bounds.reduced (0.75f), 6.0f, 1.0f);
        SuppressorLookAndFeel::renderCornerScrews (g, bounds, 10.0f, 3.2f);

        auto content = bounds.reduced (16.0f, 14.0f);

        // Colonne de gauche laissee VIDE dans le decor peint -- le gros
        // bouton d'engagement (sprite) est dessine par-dessus, en tant que
        // vrai composant (voir resized()), pas dans ce cache statique.
        content.removeFromLeft (engageColW);
        content.removeFromLeft (10.0f);

        g.setColour (SuppressorColours::textDim.withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)).withExtraKerningFactor (0.1f));
        auto titleRow = content.removeFromTop (14.0f);
        ledCentre = { titleRow.getX() + 5.0f, titleRow.getCentreY() };
        g.drawFittedText ("THD VALVE", titleRow.withTrimmedLeft (14.0f).toNearestInt(),
                           juce::Justification::centredLeft, 1);
        content.removeFromTop (4.0f);

        // Scope (carre, cale sur la hauteur restante) a gauche, ecran
        // barre+% a droite.
        const float scopeSide = content.getHeight();
        scopeBounds = content.removeFromLeft (scopeSide);
        content.removeFromLeft (10.0f);

        // Plus de partage avec l'ecran barre (retire) -- le % occupe toute
        // la largeur restante apres le scope.
        meterPctBounds = content;

        // --- Fond du scope (cerclage metal, disque noir enrichi, vis) ---
        {
            const auto c = scopeBounds.getCentre();
            const float r = juce::jmin (scopeBounds.getWidth(), scopeBounds.getHeight()) * 0.38f;
            const float ringW = juce::jmax (3.0f, r * 0.16f);
            const float ringOuterR = r + ringW;

            for (int i = 4; i > 0; --i)
            {
                g.setColour (juce::Colours::black.withAlpha (0.06f * (float) i));
                const float rr = ringOuterR + (float) i * (r * 0.035f);
                g.fillEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f);
            }

            juce::ColourGradient ringGrad (juce::Colour (0xff4a4d52).brighter (0.05f), c.x - ringOuterR, c.y - ringOuterR,
                                            juce::Colour (0xff222428), c.x + ringOuterR, c.y + ringOuterR, true);
            g.setGradientFill (ringGrad);
            g.fillEllipse (c.x - ringOuterR, c.y - ringOuterR, ringOuterR * 2.0f, ringOuterR * 2.0f);

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawEllipse (c.x - r - 1.0f, c.y - r - 1.0f, (r + 1.0f) * 2.0f, (r + 1.0f) * 2.0f, 2.0f);

            g.setColour (juce::Colour (0xff08090a));
            g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

            {
                g.saveState();
                juce::Path clip;
                clip.addEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
                g.reduceClipRegion (clip);

                juce::ColourGradient vignette (juce::Colour (0xff282a28).withAlpha (0.55f), c.x, c.y,
                                                juce::Colours::black.withAlpha (0.6f), c.x, c.y - r, true);
                vignette.addColour (0.6, juce::Colours::transparentBlack);
                g.setGradientFill (vignette);
                g.fillRect (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

                juce::Random rng (7);
                const int count = (int) (r * r * 0.35f);
                for (int i = 0; i < count; ++i)
                {
                    const float a = rng.nextFloat() * juce::MathConstants<float>::twoPi;
                    const float rr = std::sqrt (rng.nextFloat()) * r;
                    const float gx = c.x + std::cos (a) * rr;
                    const float gy = c.y + std::sin (a) * rr;
                    g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black)
                                     .withAlpha (rng.nextBool() ? 0.035f : 0.06f));
                    g.fillRect (gx, gy, 1.0f, 1.0f);
                }
                g.restoreState();
            }

            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.drawEllipse (c.x - ringOuterR, c.y - ringOuterR, ringOuterR * 2.0f, ringOuterR * 2.0f, 1.4f);
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.drawEllipse (c.x - ringOuterR - 1.0f, c.y - ringOuterR - 1.0f,
                            (ringOuterR + 1.0f) * 2.0f, (ringOuterR + 1.0f) * 2.0f, 0.8f);

            SuppressorLookAndFeel::drawScrew (g, { c.x, c.y - ringOuterR + ringW * 0.5f }, juce::jmax (1.6f, ringW * 0.34f));
            SuppressorLookAndFeel::drawScrew (g, { c.x, c.y + ringOuterR - ringW * 0.5f }, juce::jmax (1.6f, ringW * 0.34f));
        }

        cachedPowered = powered;
        staticCacheDirty = false;
    }

    void timerCallback() override
    {
        const auto* laf = dynamic_cast<SuppressorLookAndFeel*> (&getLookAndFeel());
        const bool powered = (laf == nullptr) || laf->isPowered();

        const float target = powered ? juce::jlimit (0.0f, 1.0f, clipAmount.load()) : 0.0f;
        if (target > displayedAmount)
            displayedAmount += (target - displayedAmount) * 0.45f;
        else
            displayedAmount += (target - displayedAmount) * 0.10f;

        // BUG corrige : la diode d'etat depend de softClipOnParam, mais ce
        // gate ne regardait que displayedAmount/powered -- si le signal
        // n'entrainait aucun changement de displayedAmount (silence),
        // cliquer sur engageSwitch ne redessinait jamais la diode.
        const bool engagedNow = softClipOnParam.load() > 0.5f;

        if (std::abs (displayedAmount - lastPaintedAmount) > 0.0015f
            || powered != lastPaintedPowered
            || engagedNow != lastPaintedEngaged)
        {
            lastPaintedAmount = displayedAmount;
            lastPaintedPowered = powered;
            lastPaintedEngaged = engagedNow;
            repaint();
        }
    }

    std::atomic<float>& clipAmount;
    std::atomic<float>& softClipOnParam;
    float displayedAmount = 0.0f;
    float lastPaintedAmount = -1.0f;
    bool lastPaintedPowered = true;
    bool lastPaintedEngaged = false;

    juce::Rectangle<float> scopeBounds, meterPctBounds;
    juce::Point<float> ledCentre;

    juce::Image staticCache;
    bool staticCacheDirty = true;
    bool cachedPowered = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThdValveModule)
};
