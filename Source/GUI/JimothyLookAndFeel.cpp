#include "JimothyLookAndFeel.h"
#include <cmath>

JimothyLookAndFeel::JimothyLookAndFeel()
{
    terminalFont = juce::Font ("Iosevka", 12.0f, juce::Font::plain);
    juce::Font::setFallbackFontName ("Fira Code");

    setColour (juce::ResizableWindow::backgroundColourId,   voidBlack());
    setColour (juce::DocumentWindow::backgroundColourId,    voidBlack());
    setColour (juce::Slider::backgroundColourId,            inactive());
    setColour (juce::Slider::trackColourId,                 hehku());
    setColour (juce::Slider::thumbColourId,                 hehku());
    setColour (juce::Label::textColourId,                   hehku());
}

void JimothyLookAndFeel::drawRotarySlider (juce::Graphics& g,
    int x, int y, int w, int h,
    float sliderPos, float startAngle, float endAngle,
    juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> ((float)x, (float)y, (float)w, (float)h)
                      .reduced (4.0f);
    auto centre = bounds.getCentre();
    auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

    // Background ring — inactive
    g.setColour (inactive());
    juce::Path bgArc;
    bgArc.addCentredArc (centre.x, centre.y, radius, radius,
                         0.0f, startAngle, endAngle, true);
    g.strokePath (bgArc, juce::PathStrokeType (2.0f));

    // Value arc — accent. Bipolar controls fill outward from twelve o'clock,
    // so that "no bias" reads as no arc at all.
    const bool bipolar = slider.getMinimum() < -0.001;
    const float angle  = startAngle + sliderPos * (endAngle - startAngle);
    const float origin = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    g.setColour (hehku());
    juce::Path valArc;
    valArc.addCentredArc (centre.x, centre.y, radius, radius,
                          0.0f, juce::jmin (origin, angle), juce::jmax (origin, angle), true);
    g.strokePath (valArc, juce::PathStrokeType (2.0f));

    // Pointer — sharp line, no thumb blob
    juce::Point<float> tip (
        centre.x + (radius - 5.0f) * std::sin (angle),
        centre.y - (radius - 5.0f) * std::cos (angle));
    g.setColour (hehku());
    g.drawLine (centre.x, centre.y, tip.x, tip.y, 1.5f);

    // Centre dot
    g.fillEllipse (centre.x - 2.0f, centre.y - 2.0f, 4.0f, 4.0f);
}

void JimothyLookAndFeel::drawLinearSlider (juce::Graphics& g,
    int x, int y, int w, int h,
    float sliderPos, float /*minPos*/, float /*maxPos*/,
    const juce::Slider::SliderStyle style, juce::Slider&)
{
    auto bounds = juce::Rectangle<float> ((float)x, (float)y, (float)w, (float)h);

    if (style == juce::Slider::LinearVertical)
    {
        float trackX = bounds.getCentreX() - 1.0f;
        // Track
        g.setColour (inactive());
        g.fillRect (trackX, bounds.getY(), 2.0f, bounds.getHeight());
        // Value
        g.setColour (hehku());
        g.fillRect (trackX, sliderPos, 2.0f, bounds.getBottom() - sliderPos);
        // Thumb — sharp notch, no circle
        g.fillRect (trackX - 5.0f, sliderPos - 1.0f, 12.0f, 2.0f);
    }
    else
    {
        float trackY = bounds.getCentreY() - 1.0f;
        g.setColour (inactive());
        g.fillRect (bounds.getX(), trackY, bounds.getWidth(), 2.0f);
        g.setColour (hehku());
        g.fillRect (bounds.getX(), trackY, sliderPos - bounds.getX(), 2.0f);
        g.fillRect (sliderPos - 1.0f, trackY - 5.0f, 2.0f, 12.0f);
    }
}

juce::Font JimothyLookAndFeel::getLabelFont (juce::Label&) { return terminalFont; }

void JimothyLookAndFeel::drawHexGrid (juce::Graphics& g,
    juce::Rectangle<float> bounds, float cellSize, juce::Colour colour)
{
    g.setColour (colour);
    float h  = cellSize;
    float w  = h * 1.1547f;  // 2/sqrt(3)
    float x0 = bounds.getX();
    float y0 = bounds.getY();

    for (float row = -1; row * h * 0.75f < bounds.getHeight() + h; ++row)
    {
        for (float col = -1; col * w < bounds.getWidth() + w; ++col)
        {
            float cx = x0 + col * w + ((int)row % 2 == 0 ? 0 : w * 0.5f);
            float cy = y0 + row * h * 0.75f;

            juce::Path hex;
            for (int i = 0; i < 6; ++i)
            {
                float angle = juce::MathConstants<float>::pi / 3.0f * (float)i
                              - juce::MathConstants<float>::pi / 6.0f;
                float px = cx + (h * 0.5f) * std::cos (angle);
                float py = cy + (h * 0.5f) * std::sin (angle);
                if (i == 0) hex.startNewSubPath (px, py);
                else        hex.lineTo (px, py);
            }
            hex.closeSubPath();
            g.strokePath (hex, juce::PathStrokeType (0.5f));
        }
    }
}

float JimothyLookAndFeel::hiljaisuusPulse (double t)
{
    // 0.15Hz slow pulse — the ember breathes
    return 0.5f + 0.5f * (float)std::sin (2.0 * juce::MathConstants<double>::pi * 0.15 * t);
}
