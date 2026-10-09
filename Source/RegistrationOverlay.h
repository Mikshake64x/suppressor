#pragma once

#include <JuceHeader.h>
#include "FreeRegistration.h"

/**
    RegistrationOverlay
    -------------------
    Mur d'activation PLEIN ECRAN affiche tant que l'utilisateur ne s'est pas
    inscrit (nom + email, une seule fois -- Suppressor est gratuit, pas de
    cle). Dernier enfant de l'editeur : il peint par-dessus tout et avale les
    clics. Une fois inscrit (dans cette instance, une autre instance du meme
    processus, ou au lancement precedent), il disparait et ne revient plus.
    Comme les autres plugins SideKey, c'est un mur d'UI : le DSP n'est pas
    bride.
*/
class RegistrationOverlay : public juce::Component, private juce::Timer
{
public:
    explicit RegistrationOverlay (FreeRegistration& r) : registration (r)
    {
        addAndMakeVisible (titleLabel);
        titleLabel.setText ("Activate Suppressor", juce::dontSendNotification);
        titleLabel.setJustificationType (juce::Justification::centred);
        titleLabel.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
        titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);

        addAndMakeVisible (introLabel);
        introLabel.setText ("Suppressor is free. Enter your name and email once to activate it.",
                             juce::dontSendNotification);
        introLabel.setJustificationType (juce::Justification::centred);
        introLabel.setFont (juce::Font (juce::FontOptions (12.5f)));
        introLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));

        addAndMakeVisible (nameEditor);
        nameEditor.setTextToShowWhenEmpty ("Name (required)", juce::Colours::grey);
        nameEditor.setJustification (juce::Justification::centred);
        nameEditor.onReturnKey = [this] { emailEditor.grabKeyboardFocus(); };

        addAndMakeVisible (emailEditor);
        emailEditor.setTextToShowWhenEmpty ("Email (required)", juce::Colours::grey);
        emailEditor.setJustification (juce::Justification::centred);
        emailEditor.onReturnKey = [this] { activate(); };

        addAndMakeVisible (activateButton);
        activateButton.setButtonText ("Activate");
        activateButton.onClick = [this] { activate(); };

        addAndMakeVisible (statusLabel);
        statusLabel.setJustificationType (juce::Justification::centred);
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::orangered);
        statusLabel.setFont (juce::Font (juce::FontOptions (12.5f)));

        addAndMakeVisible (noteLabel);
        noteLabel.setText ("Your name and email are sent to Side Key System (sidekey.fr) "
                            "to register this free copy.", juce::dontSendNotification);
        noteLabel.setJustificationType (juce::Justification::centred);
        noteLabel.setFont (juce::Font (juce::FontOptions (10.5f)));
        noteLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.45f));

        setInterceptsMouseClicks (true, true);
        updateVisibility();
        startTimerHz (2);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.92f));
    }

    void resized() override
    {
        auto area = getLocalBounds().withSizeKeepingCentre (360, 240);
        titleLabel.setBounds (area.removeFromTop (28));
        area.removeFromTop (6);
        introLabel.setBounds (area.removeFromTop (32));
        area.removeFromTop (10);
        nameEditor.setBounds (area.removeFromTop (28));
        area.removeFromTop (8);
        emailEditor.setBounds (area.removeFromTop (28));
        area.removeFromTop (12);
        activateButton.setBounds (area.removeFromTop (28).withSizeKeepingCentre (140, 28));
        area.removeFromTop (6);
        statusLabel.setBounds (area.removeFromTop (16));
        area.removeFromTop (4);
        noteLabel.setBounds (area.removeFromTop (28));
    }

private:
    static bool looksLikeEmail (const juce::String& email)
    {
        const int at = email.indexOfChar ('@');

        return at > 0
            && ! email.containsAnyOf (" \t\r\n")
            && email.substring (at + 1).contains (".")
            && ! email.endsWithChar ('.')
            && email.indexOfChar (at + 1, '@') < 0;
    }

    void activate()
    {
        const auto name  = nameEditor.getText().trim();
        const auto email = emailEditor.getText().trim();

        if (name.length() < 2)
        {
            statusLabel.setText ("Please enter your name.", juce::dontSendNotification);
            nameEditor.grabKeyboardFocus();
            return;
        }

        if (! looksLikeEmail (email))
        {
            statusLabel.setText ("Please enter a valid email address.", juce::dontSendNotification);
            emailEditor.grabKeyboardFocus();
            return;
        }

        registration.registerUser (name, email);
        statusLabel.setText ({}, juce::dontSendNotification);
        updateVisibility();
    }

    void updateVisibility()
    {
        const bool shouldShow = ! registration.isRegistered();

        if (shouldShow != isVisible())
            setVisible (shouldShow);
    }

    // Une autre instance du meme processus peut s'etre inscrite entre-temps.
    void timerCallback() override { updateVisibility(); }

    FreeRegistration& registration;
    juce::Label titleLabel, introLabel, statusLabel, noteLabel;
    juce::TextEditor nameEditor, emailEditor;
    juce::TextButton activateButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RegistrationOverlay)
};
