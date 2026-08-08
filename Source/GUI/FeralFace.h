#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>
#include "JimothyLookAndFeel.h"
#include "FX/JimothyPedal.h"

// ─────────────────────────────────────────────────────────────────────────────
// FeralFace — the feral edition's entire listening surface: a face.
//
// The full version gets the instruments: the transfer curve, the scope, the
// meter that tells the truth when the rig lies. The feral edition gets Jimothy
// looking back at you. The same two renderers still run — drawn by the same
// Jimothy::shape() and the same ScopeFifo, so nothing shown is fake — but each
// is clipped to an eye and the eyelids are the data.
//
// "Left" and "right" are the VIEWER'S — the operator convention, ruled on
// explicitly. You are looking at Jimothy; you are not Jimothy.
//
//   Right eye (screen right) — the dirt gate. Open when the gate passes
//                              signal, shut when it collapses. Behind the
//                              lid: the post-pedal scope, a glimpse.
//   Left eye  (screen left)  — the knee. Narrows as the charge organ starves
//                              the stage; parked on the off rail is a closed
//                              eye. Behind the lid: the live transfer curve.
//
// The plots are drawn larger than the apertures and clipped, so the grid runs
// off the edge of the iris: an indication that there is a graph to be seen, if
// only we could see into Jimothy's soul.
//
// House rule kept: everything that moves here is communicating information.
// The eyes never blink idly — a lid moves because the gate or the charge
// moved. The whiskers carry the one sanctioned exception, the hiljaisuus
// pulse: if the whiskers are breathing, the instance is alive.
// ─────────────────────────────────────────────────────────────────────────────

class FeralFace : public juce::Component
{
public:
    FeralFace (JimothyLookAndFeel&, Jimothy::ScopeFifo&, Jimothy::Telemetry&);

    // Called at the editor's timer rate; repaints only the eyes and whiskers.
    // pulse is the hiljaisuus value [0,1].
    void refresh (float driveNorm, float leanNorm, uint32_t unitSeed, float pulse);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebuildFaceCache();

    juce::Rectangle<float> aperture (juce::Point<float> centre) const;
    juce::Rectangle<float> plotRect (juce::Point<float> centre) const;

    void paintGateEye (juce::Graphics&);
    void paintKneeEye (juce::Graphics&);
    void paintLids    (juce::Graphics&, juce::Point<float> centre, float openness);

    JimothyLookAndFeel& laf;
    Jimothy::ScopeFifo& fifo;
    Jimothy::Telemetry& telemetry;

    // The face art, recolored for the void and cached as an image so the
    // 30Hz eye repaints never re-tessellate the SVG.
    std::unique_ptr<juce::Drawable> faceDrawable;
    juce::Image           faceCache;
    juce::AffineTransform faceTransform;

    // Component space. "Gate" sits in the viewer's right eye, "knee" in the
    // viewer's left — see the header comment for the convention ruling.
    juce::Point<float> gateEyeCentre, kneeEyeCentre;
    float eyeRadius = 0.0f;

    std::vector<std::array<juce::Point<float>, 2>> whiskerLines;
    float whiskerStroke = 5.0f;
    float breath        = 0.0f;   // hiljaisuus pulse, set each refresh

    // Live state for the knee curve — same fields, same ballistics as the
    // full version's transfer display, including the widen-fast /
    // contract-slow zoom.
    float drive = 0.55f;
    float lean  = 0.0f;
    float peak  = 0.0f;
    float choke      = 0.0f;
    float chokeRange = 0.0f;

    uint32_t      seed = 0;
    Jimothy::Unit unit;

    // Lid positions, 0 shut – 1 open.
    float gateOpenness = 1.0f;
    float kneeOpenness = 1.0f;

    std::vector<float> samples;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FeralFace)
};
