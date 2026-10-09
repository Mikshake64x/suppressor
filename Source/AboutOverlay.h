#pragma once
#include <JuceHeader.h>
#include "SuppressorLookAndFeel.h"

/**
    AboutOverlay
    ------------
    Voile plein ecran affiche au clic sur le monogramme Side Key.

    Mise en page EN LONGUEUR (3 colonnes cote a cote, separees par de
    fines lignes -- meme langage que les dividers du panneau principal),
    pour coller a la forme reelle de la fenetre du plugin (1400x300, tres
    large et basse) au lieu d'un pave portrait qui ne matchait pas.
    Se ferme au clic (n'importe ou sur le voile, hors liens).
*/
class AboutOverlay : public juce::Component
{
public:
    AboutOverlay()
    {
        badgeImage = juce::ImageCache::getFromMemory (BinaryData::sidekey_monogram_png, BinaryData::sidekey_monogram_pngSize);
        photoImage = juce::ImageCache::getFromMemory (BinaryData::mikshake_about_png, BinaryData::mikshake_about_pngSize);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (juce::Colours::black.withAlpha (0.88f));
        g.fillRect (bounds);

        auto panel = bounds.reduced (juce::jmax (20.0f, bounds.getWidth() * 0.015f),
                                       juce::jmax (12.0f, bounds.getHeight() * 0.04f));

        SuppressorLookAndFeel::renderGlassPanel (g, panel, 12.0f);
        SuppressorLookAndFeel::renderGlossySheen (g, panel);
        g.setColour (SuppressorColours::ledAmber.withAlpha (0.5f));
        g.drawRoundedRectangle (panel.reduced (0.5f), 12.0f, 1.0f);

        auto content = panel.reduced (20.0f, 12.0f);

        // --- En-tete : monogramme + titre + tagline (gauche), version (droite) ---
        auto headerRow = content.removeFromTop (30.0f);

        if (! badgeImage.isNull())
        {
            auto badgeArea = headerRow.removeFromLeft (28.0f);
            const float bh = badgeArea.getHeight();
            const float bw = bh * ((float) badgeImage.getWidth() / (float) badgeImage.getHeight());
            auto badgeBounds = juce::Rectangle<float> (bw, bh).withCentre (badgeArea.getCentre());
            g.drawImage (badgeImage, (int) badgeBounds.getX(), (int) badgeBounds.getY(), (int) badgeBounds.getWidth(), (int) badgeBounds.getHeight(),
                         0, 0, badgeImage.getWidth(), badgeImage.getHeight());
            headerRow.removeFromLeft (10.0f);
        }

        auto versionArea = headerRow.removeFromRight (56.0f);
        g.setColour (SuppressorColours::textDim);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
        g.drawFittedText ("v1.2.2", versionArea.toNearestInt(), juce::Justification::centredRight, 1);

        auto titleArea = headerRow.removeFromLeft (juce::jmin (220.0f, headerRow.getWidth() * 0.45f));
        g.setColour (SuppressorColours::ledAmber);
        g.setFont (juce::Font (juce::FontOptions (21.0f, juce::Font::bold)).withExtraKerningFactor (0.1f));
        g.drawFittedText ("ABOUT SUPPRESSOR", titleArea.toNearestInt(), juce::Justification::centredLeft, 1);

        headerRow.removeFromLeft (14.0f);
        g.setColour (SuppressorColours::ledBlue.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::italic)).withExtraKerningFactor (0.10f));
        g.drawFittedText ("Dynamics. Refined.", headerRow.toNearestInt(), juce::Justification::centredLeft, 1);

        content.removeFromTop (8.0f);
        g.setColour (SuppressorColours::gridLine);
        g.drawLine (content.getX(), content.getY(), content.getRight(), content.getY(), 1.0f);
        content.removeFromTop (10.0f);

        // --- Pied de page (en bas, sur toute la largeur) : photo + lien + tampon ---
        auto footerRow = content.removeFromBottom (54.0f);
        content.removeFromBottom (10.0f);
        drawFooter (g, footerRow);

        g.setColour (SuppressorColours::gridLine);
        g.drawLine (content.getX(), content.getBottom(), content.getRight(), content.getBottom(), 1.0f);
        content.removeFromBottom (10.0f);

        // --- Corps : 3 colonnes cote a cote, separees par de fines lignes
        // (meme style que les dividers du panneau principal) -- c'est ce
        // qui rend la mise en page "en longueur" au lieu d'un long
        // scroll vertical. ---
        const float gap = 18.0f;
        const float colW = (content.getWidth() - gap * 2.0f) / 3.0f;
        const float colTop = content.getY(), colBottom = content.getBottom();

        auto colA = content.removeFromLeft (colW);
        content.removeFromLeft (gap);
        auto colB = content.removeFromLeft (colW);
        content.removeFromLeft (gap);
        auto colC = content;

        g.setColour (SuppressorColours::gridLine);
        g.drawLine (colA.getRight() + gap * 0.5f, colTop, colA.getRight() + gap * 0.5f, colBottom, 1.0f);
        g.drawLine (colB.getRight() + gap * 0.5f, colTop, colB.getRight() + gap * 0.5f, colBottom, 1.0f);

        const juce::Font bodyFont (juce::FontOptions (11.5f, juce::Font::plain));
        const juce::Font headFont (juce::FontOptions (13.5f, juce::Font::bold));

        // --- Colonne A : presentation ---
        float yA = colA.getY();
        g.setColour (SuppressorColours::ledBlue);
        g.setFont (headFont);
        g.drawFittedText ("Overview", { (int) colA.getX(), (int) yA, (int) colA.getWidth(), 16 }, juce::Justification::centredLeft, 1);
        yA += 20.0f;

        yA = drawParagraph (g,
            "Suppressor is a precision feedforward compressor built for engineers "
            "who want full control over dynamics without losing musicality.",
            { colA.getX(), yA, colA.getWidth(), 0.0f }, bodyFont, SuppressorColours::textBright, 1.28f) + 8.0f;

        drawParagraph (g,
            "Inspired by classic analog feedforward compressors, it blends "
            "character and accuracy - from transparent gain control to "
            "rich harmonic coloration, tuned to respond naturally across "
            "mixing and mastering.",
            { colA.getX(), yA, colA.getWidth(), 0.0f }, bodyFont, SuppressorColours::textBright, 1.28f);

        // --- Colonne B : fonctions (liste, une par ligne) ---
        float yB = colB.getY();
        g.setColour (SuppressorColours::ledBlue);
        g.setFont (headFont);
        g.drawFittedText ("Features", { (int) colB.getX(), (int) yB, (int) colB.getWidth(), 16 }, juce::Justification::centredLeft, 1);
        yB += 22.0f;

        static const char* features[] =
        {
            "Feedforward Compression",
            "Discrete LED Ratio Selection",
            "Soft Clip / Limit Stage",
            "Stereo Detector Link",
            "British Mode (all-buttons-in)",
            "Dual Needle VU + THD Valve"
        };
        g.setFont (bodyFont);
        for (auto* f : features)
        {
            g.setColour (SuppressorColours::ledAmber);
            g.fillEllipse (colB.getX(), yB + 5.5f, 4.0f, 4.0f);
            g.setColour (SuppressorColours::textBright);
            g.drawFittedText (f, { (int) colB.getX() + 10, (int) yB, (int) colB.getWidth() - 10, 15 }, juce::Justification::centredLeft, 1);
            yB += 17.0f;
        }

        // --- Colonne C : philosophie ---
        float yC = colC.getY();
        g.setColour (SuppressorColours::ledBlue);
        g.setFont (headFont);
        g.drawFittedText ("The Side Key System Philosophy",
                           { (int) colC.getX(), (int) yC, (int) colC.getWidth(), 16 }, juce::Justification::centredLeft, 1);
        yC += 20.0f;

        yC = drawParagraph (g,
            "Calibration is not a final adjustment - it is part of the "
            "engineering process itself.",
            { colC.getX(), yC, colC.getWidth(), 0.0f }, bodyFont, SuppressorColours::textBright, 1.28f) + 8.0f;

        drawParagraph (g,
            "Every control is measured and tuned to deliver predictable, "
            "repeatable results - a workflow you can trust, whether on a "
            "single track or a full mastering session.",
            { colC.getX(), yC, colC.getWidth(), 0.0f }, bodyFont, SuppressorColours::textBright, 1.28f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (websiteBounds.contains (e.position))
        {
            juce::URL ("https://sidekey.fr").launchInDefaultBrowser();
            return;
        }

        if (onDismiss)
            onDismiss();
    }

    std::function<void()> onDismiss;

private:
    static float drawParagraph (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                                 juce::Font font, juce::Colour colour, float lineSpacing)
    {
        juce::AttributedString as;
        as.append (text, font, colour);
        as.setWordWrap (juce::AttributedString::byWord);
        as.setLineSpacing (lineSpacing);

        juce::TextLayout tl;
        tl.createLayout (as, area.getWidth());
        tl.draw (g, area.withHeight (tl.getHeight()));
        return area.getY() + tl.getHeight();
    }

    // Pied de page : photo (gauche) + lien site + tampon "Calibrated"
    // (droite), sur une seule rangee -- garde tout "en longueur" au lieu
    // d'empiler ces elements verticalement.
    void drawFooter (juce::Graphics& g, juce::Rectangle<float> area)
    {
        if (! photoImage.isNull())
        {
            const float photoH = area.getHeight();
            const float photoW = photoH * ((float) photoImage.getWidth() / (float) photoImage.getHeight());
            auto photoBounds = juce::Rectangle<float> (photoW, photoH).withCentre (area.getCentre()).withX (area.getX());

            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle (photoBounds, 6.0f);
            g.reduceClipRegion (clip);
            g.drawImage (photoImage, (int) photoBounds.getX(), (int) photoBounds.getY(), (int) photoBounds.getWidth(), (int) photoBounds.getHeight(),
                         0, 0, photoImage.getWidth(), photoImage.getHeight());
            g.restoreState();

            g.setColour (SuppressorColours::ledAmber.withAlpha (0.4f));
            g.drawRoundedRectangle (photoBounds, 6.0f, 1.0f);

            area.removeFromLeft (photoBounds.getWidth() + 16.0f);
        }

        auto stampArea = area.removeFromRight (58.0f);
        area.removeFromRight (14.0f);
        drawStamp (g, stampArea);

        auto copyrightRow = area.removeFromBottom (13.0f);
        drawWebsiteLink (g, area);

        g.setColour (SuppressorColours::textDim.withAlpha (0.75f));
        g.setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::plain)).withExtraKerningFactor (0.04f));
        g.drawFittedText ("(c) 2026 Side Key System. All rights reserved.   -   Made in France",
                           copyrightRow.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    // Effet "tampon" (cachet) pour le badge de calibration : double anneau
    // legerement rotate, texte interieur, et un grain deterministe pour
    // simuler une encre de tampon use -- pas juste une icone + texte plat.
    void drawStamp (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const auto centre = area.getCentre();
        const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const juce::Colour ink = SuppressorColours::ledRed.withAlpha (0.82f);

        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (-8.0f), centre.x, centre.y));

        g.setColour (ink);
        g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 1.8f);
        g.drawEllipse (centre.x - r * 0.82f, centre.y - r * 0.82f, r * 1.64f, r * 1.64f, 0.9f);

        g.setFont (juce::Font (juce::FontOptions (r * 0.30f, juce::Font::bold)).withExtraKerningFactor (0.08f));
        g.drawFittedText ("CALIBRATED",
                           juce::Rectangle<float> (centre.x - r * 0.78f, centre.y - r * 0.52f, r * 1.56f, r * 0.4f).toNearestInt(),
                           juce::Justification::centred, 1);
        g.setFont (juce::Font (juce::FontOptions (r * 0.20f, juce::Font::bold)).withExtraKerningFactor (0.04f));
        g.drawFittedText ("SIDE KEY", juce::Rectangle<float> (centre.x - r * 0.7f, centre.y + r * 0.06f, r * 1.4f, r * 0.32f).toNearestInt(),
                           juce::Justification::centred, 1);
        g.drawFittedText ("SYSTEM", juce::Rectangle<float> (centre.x - r * 0.7f, centre.y + r * 0.36f, r * 1.4f, r * 0.3f).toNearestInt(),
                           juce::Justification::centred, 1);

        // Grain "encre use" deterministe (pas de Random, donc jamais de
        // scintillement si le panneau est repeint plusieurs fois)
        for (int i = 0; i < 46; ++i)
        {
            const float ang = (float) i * 0.151f;
            const float rr = r * (0.32f + 0.66f * std::abs (std::sin ((float) i * 2.3f)));
            const float px = centre.x + std::cos (ang) * rr;
            const float py = centre.y + std::sin (ang) * rr;
            g.setColour (ink.withAlpha (0.16f * std::abs (std::sin ((float) i * 1.7f))));
            g.fillEllipse (px - 0.6f, py - 0.6f, 1.2f, 1.2f);
        }

        g.restoreState();
    }

    void drawWebsiteLink (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto row = area.withSizeKeepingCentre (area.getWidth(), 16.0f);
        const auto linkFont = juce::Font (juce::FontOptions (13.0f, juce::Font::bold)).withExtraKerningFactor (0.04f);
        g.setFont (linkFont);

        g.setColour (SuppressorColours::ledBlue);
        g.drawFittedText ("sidekey.fr", row.toNearestInt(), juce::Justification::centredLeft, 1);

        // Zone cliquable reduite au seul texte du lien.
        juce::GlyphArrangement ga;
        ga.addLineOfText (linkFont, "sidekey.fr", 0.0f, 0.0f);
        const float siteW = ga.getBoundingBox (0, -1, true).getWidth();
        websiteBounds = row.withWidth (siteW);
        g.drawLine (websiteBounds.getX(), websiteBounds.getBottom() - 3.0f,
                    websiteBounds.getX() + siteW, websiteBounds.getBottom() - 3.0f, 1.0f);

        // Mention "Free" juste apres le lien (Suppressor est gratuit, pas de
        // cle de licence -- voir FreeRegistration). Simple texte, non cliquable.
        const float gap = 16.0f;
        const float freeX = websiteBounds.getX() + siteW + gap;
        g.setColour (SuppressorColours::textDim);
        g.drawFittedText ("Free", row.withX (freeX).withWidth (juce::jmax (30.0f, row.getRight() - freeX)).toNearestInt(),
                           juce::Justification::centredLeft, 1);
    }

    juce::Image badgeImage;
    juce::Image photoImage;
    juce::Rectangle<float> websiteBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutOverlay)
};
