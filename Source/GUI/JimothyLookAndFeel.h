#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// ─────────────────────────────────────────────────────────────────────────────
// 100% machine-generated. Mostly pro-forma JUCE template.
// JimothyLookAndFeel — the feral register
//
// Jimothy's own voice, and only his. Hehku is the ember — the heat from
// within; a fuzz pedal is a chokepoint that heats what passes through it.
//
// Palette:
//   Primary:   hehku          #C85020  on void black  #070711
//   Active:    station green  #40C080  — the reference, never the signal
//   Warning:   hiljaisuus     #8030C0
//   Inactive:  #2A2A5A
//
// Rules:
//   No rounded corners. The grid has edges.
//   No gradients. Flat color, sharp edges, intentional.
//   No shadows unless load-bearing (Z-order).
//   If something moves, it is communicating information.
//   The one exception: the hiljaisuus pulse, and it breathes in the whiskers.
//
// Do not rename colors to generic values.
// ─────────────────────────────────────────────────────────────────────────────

class JimothyLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // ── Palette ───────────────────────────────────────────────────────────────
    static constexpr uint32_t COL_VOID_BLACK     = 0xFF070711;
    static constexpr uint32_t COL_HEHKU          = 0xFFC85020;
    static constexpr uint32_t COL_STATION_GREEN  = 0xFF40C080;
    static constexpr uint32_t COL_HILJAISUUS     = 0xFF8030C0;
    static constexpr uint32_t COL_INACTIVE       = 0xFF2A2A5A;
    static constexpr uint32_t COL_DIM_HEHKU      = 0xFF6B2810;

    static juce::Colour voidBlack()     { return juce::Colour (COL_VOID_BLACK); }
    static juce::Colour hehku()         { return juce::Colour (COL_HEHKU); }
    static juce::Colour stationGreen()  { return juce::Colour (COL_STATION_GREEN); }
    static juce::Colour hiljaisuus()    { return juce::Colour (COL_HILJAISUUS); }
    static juce::Colour inactive()      { return juce::Colour (COL_INACTIVE); }
    static juce::Colour dimHehku()      { return juce::Colour (COL_DIM_HEHKU); }

    JimothyLookAndFeel();

    // ── Accent ───────────────────────────────────────────────────────────────
    // One voice. The ember does not change color.
    juce::Colour accent()    const { return hehku(); }
    juce::Colour accentDim() const { return dimHehku(); }

    // ── Overrides ─────────────────────────────────────────────────────────────
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawLinearSlider  (juce::Graphics&, int x, int y, int w, int h,
                            float sliderPos, float minSliderPos, float maxSliderPos,
                            const juce::Slider::SliderStyle,
                            juce::Slider&) override;

    juce::Font getLabelFont (juce::Label&) override;

    // The terminal register, for components that paint their own text.
    juce::Font terminal (float height) const { return terminalFont.withHeight (height); }

    // ── Hex grid utility — decorative motif ──────────────────────────────────
    static void drawHexGrid (juce::Graphics& g,
                              juce::Rectangle<float> bounds,
                              float cellSize = 18.0f,
                              juce::Colour colour = juce::Colour (0xFF0E0E22));

    // ── Hiljaisuus pulse ─ if you have to ask you don't know jaakko ──────────
    // Returns a pulse value [0,1] for the current time. Rate: ~0.15Hz.
    static float hiljaisuusPulse (double timeInSeconds);

private:
    juce::Font terminalFont;  // Iosevka / Fira Code / system mono fallback
};
