#pragma once
#include <JuceHeader.h>

/**
    PressAnimator
    -------------
    Petit melange (mixin) qui anime un "enfoncement" ressort (0..1) pour
    n'importe quel bouton plat -- au clic, la valeur monte vers 1 puis
    retombe TOUJOURS en douceur vers 0 des le relachement (jamais de
    position figee "enfoncee", meme si l'etat logique du bouton -- toggle
    allume/eteint -- reste actif), pour un petit retour tactile anime.

    Usage (composants derives de juce::Button) :
      class Foo : public juce::Button, private PressAnimator
      {
          void buttonStateChanged() override
          {
              if (getState() == juce::Button::buttonDown) notePressed(); else noteReleased();
          }
          void onPressAnimated() override { repaint(); }
          void paintButton (Graphics& g, bool, bool) override
          {
              auto bounds = getLocalBounds().toFloat();
              applyPressTransform (g, bounds);
              // ... dessin existant inchange ...
          }
      };
*/
class PressAnimator : private juce::Timer
{
public:
    ~PressAnimator() override { stopTimer(); }

    void notePressed()  { setTarget (1.0f); }
    void noteReleased()  { setTarget (0.0f); }

    float getPressAmount() const noexcept { return pressAmount; }

    // Petit "squash" -- legere reduction d'echelle centree + tassement
    // vertical, proportionnels a l'enfoncement courant. A appeler en tout
    // premier dans paint()/paintButton(), avant le dessin existant.
    void applyPressTransform (juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        if (pressAmount <= 0.001f)
            return;
        const auto c = bounds.getCentre();
        g.addTransform (juce::AffineTransform::scale (1.0f - pressAmount * 0.025f, 1.0f - pressAmount * 0.025f, c.x, c.y)
                             .translated (0.0f, pressAmount * 0.8f));
    }

protected:
    virtual void onPressAnimated() = 0;

private:
    void setTarget (float t)
    {
        pressTarget = t;
        if (! isTimerRunning())
            startTimerHz (60);
    }

    void timerCallback() override
    {
        pressAmount += (pressTarget - pressAmount) * 0.4f;
        if (std::abs (pressAmount - pressTarget) < 0.004f)
        {
            pressAmount = pressTarget;
            if (pressTarget <= 0.0f)
                stopTimer();
        }
        onPressAnimated();
    }

    float pressAmount = 0.0f;
    float pressTarget = 0.0f;
};
