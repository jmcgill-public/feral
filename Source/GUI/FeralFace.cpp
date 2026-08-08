#include "FeralFace.h"

#include <BinaryData.h>

namespace
{
    // Eye interiors match the full version's panel fill: inside Jimothy's
    // eyes are the instruments.
    const juce::Colour panelFill { 0xFF0C0C1A };
    const juce::Colour gridLine  { 0xFF16162A };

    // The face: ember-dark fur on void. Dim enough that the knobs stay the
    // loudest thing on the panel.
    const juce::Colour furTone  { 0xFF30140C };
    const juce::Colour voidTone { JimothyLookAndFeel::COL_VOID_BLACK };

    // Geometry from art/jimothy-face.svg — the drill template. The eyes are
    // EQUAL circles at (334,278) and (466,278) r=21, inside a group rotated
    // -3° about (400,300). Keep in sync with the art file.
    // "Left"/"right" are the VIEWER'S here and everywhere in this file.
    constexpr float svgEyeScreenLeft[]  = { 334.0f, 278.0f };
    constexpr float svgEyeScreenRight[] = { 466.0f, 278.0f };
    constexpr float svgEyeR      = 21.0f;
    constexpr float svgTiltDeg   = -3.0f;
    constexpr float svgPivot[]   = { 400.0f, 300.0f };

    // The ear tips. His ears are his best feature — the face is positioned
    // from them so they are never clipped by the top of the panel.
    constexpr float svgEarTipL[] = { 310.0f, 84.0f };
    constexpr float svgEarTipR[] = { 490.0f, 84.0f };

    // The whiskers, from the art's stroke group: x1,y1,x2,y2 per line,
    // stroke-width 5, round caps. Drawn twice — fur-tone in the cached face,
    // and live on top in hehku at the hiljaisuus pulse. The feral panel has
    // no status readout; the whiskers ARE the ember breathing.
    constexpr float svgWhiskers[][4] = {
        { 348.0f, 360.0f, 312.0f, 352.0f },
        { 348.0f, 374.0f, 308.0f, 374.0f },
        { 348.0f, 388.0f, 314.0f, 396.0f },
        { 452.0f, 360.0f, 488.0f, 352.0f },
        { 452.0f, 374.0f, 492.0f, 374.0f },
        { 452.0f, 388.0f, 486.0f, 396.0f },
    };
    constexpr float svgWhiskerWidth = 5.0f;
}

FeralFace::FeralFace (JimothyLookAndFeel& lookAndFeel,
                      Jimothy::ScopeFifo& source,
                      Jimothy::Telemetry& tel)
    : laf (lookAndFeel), fifo (source), telemetry (tel)
{
    setInterceptsMouseClicks (false, false);
    samples.resize (Jimothy::ScopeFifo::size / 2, 0.0f);

    if (auto xml = juce::XmlDocument::parse (
            juce::String::fromUTF8 (BinaryData::jimothyface_svg,
                                    BinaryData::jimothyface_svgSize)))
    {
        // The art's white paper rect would blank the whole panel — the panel
        // is the paper now. It is the only direct <rect> child of the root;
        // the philtrum rect lives inside the rotated group.
        if (auto* bg = xml->getChildByName ("rect"))
            xml->removeChildElement (bg, true);

        faceDrawable = juce::Drawable::createFromSVG (*xml);
    }

    if (faceDrawable != nullptr)
    {
        faceDrawable->replaceColour (juce::Colours::white, voidTone);
        faceDrawable->replaceColour (juce::Colour (0xFF111111), furTone);
    }
}

void FeralFace::resized()
{
    // Scale so the mask band dominates the upper panel; the cheeks are allowed
    // to bleed off both edges, the ears are not — the face hangs from its ear
    // tips, a small margin below the top edge, and everything else follows.
    const auto rot = juce::AffineTransform::rotation (
        juce::degreesToRadians (svgTiltDeg), svgPivot[0], svgPivot[1]);

    const auto eSL = juce::Point<float> (svgEyeScreenLeft[0],  svgEyeScreenLeft[1]) .transformedBy (rot);
    const auto eSR = juce::Point<float> (svgEyeScreenRight[0], svgEyeScreenRight[1]).transformedBy (rot);
    const auto mid = (eSL + eSR) / 2.0f;

    const float earTop = juce::jmin (
        juce::Point<float> (svgEarTipL[0], svgEarTipL[1]).transformedBy (rot).y,
        juce::Point<float> (svgEarTipR[0], svgEarTipR[1]).transformedBy (rot).y);

    const float scale     = (float) getWidth() / 340.0f;
    const float topMargin = 12.0f;
    const float eyeBandY  = topMargin + (mid.y - earTop) * scale;

    faceTransform = juce::AffineTransform::translation (-mid.x, -mid.y)
                        .scaled (scale)
                        .followedBy (juce::AffineTransform::translation (
                            (float) getWidth() * 0.5f, eyeBandY));

    // Gate on the right, knee on the left — the viewer's right and left.
    gateEyeCentre = eSR.transformedBy (faceTransform);
    kneeEyeCentre = eSL.transformedBy (faceTransform);
    eyeRadius     = svgEyeR * scale;

    // Whisker endpoints, into component space for the pulse overlay.
    whiskerLines.clear();
    for (const auto& wl : svgWhiskers)
    {
        const auto a = juce::Point<float> (wl[0], wl[1]).transformedBy (rot).transformedBy (faceTransform);
        const auto b = juce::Point<float> (wl[2], wl[3]).transformedBy (rot).transformedBy (faceTransform);
        whiskerLines.push_back ({ a, b });
    }
    whiskerStroke = svgWhiskerWidth * scale;

    rebuildFaceCache();
}

void FeralFace::rebuildFaceCache()
{
    if (getWidth() <= 0 || getHeight() <= 0 || faceDrawable == nullptr)
        return;

    faceCache = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics ig (faceCache);
    faceDrawable->draw (ig, 1.0f, faceTransform);
}

void FeralFace::refresh (float driveNorm, float leanNorm, uint32_t unitSeed, float pulse)
{
    breath = pulse;

    if (unitSeed != seed)
    {
        seed = unitSeed;
        unit = Jimothy::deriveUnit (seed);
    }

    drive = driveNorm;
    lean  = leanNorm;
    peak  = telemetry.inPeak   .load (std::memory_order_relaxed);
    choke = telemetry.chokeBias.load (std::memory_order_relaxed);

    // Zoom ballistics: widen instantly, contract at watchable speed.
    chokeRange = choke > chokeRange ? choke
                                    : chokeRange + (choke - chokeRange) * 0.08f;

    // The dirt gate eye. Shut snaps — the gate collapsing is an event; the
    // reopen is wary. Both are the gate's own state, never an idle blink.
    const bool  open       = telemetry.gateOpen.load (std::memory_order_relaxed);
    const float gateTarget = open ? 1.0f : 0.0f;
    gateOpenness += (gateTarget - gateOpenness) * (gateTarget < gateOpenness ? 0.55f : 0.22f);

    // The knee eye narrows with starvation. starved is absolute (charge over
    // the unit's choke knee), so a fully parked stage is a fully closed eye,
    // and the reopen crawls back at the τ you hear because starved does.
    const float kneeTarget = 1.0f - telemetry.starved.load (std::memory_order_relaxed);
    kneeOpenness += (kneeTarget - kneeOpenness) * 0.35f;

    fifo.read (samples.data(), (int) samples.size());

    for (auto c : { gateEyeCentre, kneeEyeCentre })
        repaint (aperture (c).expanded (2.0f).getSmallestIntegerContainer());

    // The whiskers breathe every tick. One dirty rect per cluster of three.
    juce::Rectangle<float> left, right;
    for (size_t i = 0; i < whiskerLines.size(); ++i)
    {
        auto span = juce::Rectangle<float>::findAreaContainingPoints (whiskerLines[i].data(), 2);
        auto& side = i < 3 ? left : right;
        side = side.isEmpty() ? span : side.getUnion (span);
    }
    for (auto& r : { left, right })
        repaint (r.expanded (whiskerStroke).getSmallestIntegerContainer());
}

juce::Rectangle<float> FeralFace::aperture (juce::Point<float> centre) const
{
    return { centre.x - eyeRadius, centre.y - eyeRadius,
             eyeRadius * 2.0f, eyeRadius * 2.0f };
}

juce::Rectangle<float> FeralFace::plotRect (juce::Point<float> centre) const
{
    // Wider than the aperture on purpose: the grid and the trace run off the
    // edge of the iris, so what you get is visibly a glimpse of a larger
    // instrument, not a complete miniature one.
    return { centre.x - eyeRadius * 1.6f, centre.y - eyeRadius * 1.3f,
             eyeRadius * 3.2f, eyeRadius * 2.6f };
}

void FeralFace::paint (juce::Graphics& g)
{
    if (faceCache.isValid())
        g.drawImageAt (faceCache, 0, 0);

    // The ember breathes through the whiskers: hehku over the fur-tone
    // strokes already in the cache, alpha riding the hiljaisuus pulse. This
    // is the feral panel's whole status display — if the whiskers are
    // breathing, the instance is alive.
    if (! whiskerLines.empty())
    {
        g.setColour (JimothyLookAndFeel::hehku().withAlpha (0.10f + 0.65f * breath));
        for (const auto& wl : whiskerLines)
        {
            juce::Path w;
            w.startNewSubPath (wl[0]);
            w.lineTo (wl[1]);
            g.strokePath (w, juce::PathStrokeType (whiskerStroke,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }
    }

    paintGateEye (g);
    paintKneeEye (g);

    // Aperture rings — the same 1px chrome as the full version's panel frames.
    g.setColour (laf.accentDim().withAlpha (0.7f));
    g.drawEllipse (aperture (gateEyeCentre), 1.0f);
    g.drawEllipse (aperture (kneeEyeCentre), 1.0f);
}

void FeralFace::paintGateEye (juce::Graphics& g)
{
    const auto eye  = aperture (gateEyeCentre);
    const auto plot = plotRect (gateEyeCentre);

    juce::Graphics::ScopedSaveState state (g);

    juce::Path clip;
    clip.addEllipse (eye);
    g.reduceClipRegion (clip);

    g.setColour (panelFill);
    g.fillRect (plot.getUnion (eye));

    const float midY = gateEyeCentre.y;

    // Unity rails and the station-green zero line.
    g.setColour (gridLine);
    g.drawLine (plot.getX(), plot.getY(),      plot.getRight(), plot.getY(),      0.5f);
    g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getBottom(), 0.5f);

    g.setColour (JimothyLookAndFeel::stationGreen().withAlpha (0.55f));
    g.drawLine (plot.getX(), midY, plot.getRight(), midY, 1.0f);

    if (! samples.empty())
    {
        juce::Path trace;
        const int n = (int) samples.size();
        const int stride = juce::jmax (1, n / juce::jmax (1, (int) plot.getWidth() * 2));

        for (int i = 0; i < n; i += stride)
        {
            const float x = juce::jmap ((float) i, 0.0f, (float) (n - 1),
                                        plot.getX(), plot.getRight());
            const float y = midY - juce::jlimit (-1.0f, 1.0f, samples[(size_t) i])
                                       * plot.getHeight() * 0.5f;

            if (trace.isEmpty()) trace.startNewSubPath (x, y);
            else                 trace.lineTo (x, y);
        }

        g.setColour (laf.accent());
        g.strokePath (trace, juce::PathStrokeType (1.0f));
    }

    paintLids (g, gateEyeCentre, gateOpenness);
}

void FeralFace::paintKneeEye (juce::Graphics& g)
{
    const auto eye  = aperture (kneeEyeCentre);
    const auto plot = plotRect (kneeEyeCentre);

    juce::Graphics::ScopedSaveState state (g);

    juce::Path clip;
    clip.addEllipse (eye);
    g.reduceClipRegion (clip);

    g.setColour (panelFill);
    g.fillRect (plot.getUnion (eye));

    // Auto-range identical to the full version's transfer display, minus the
    // printed label — the feral build gets the shape of the truth, not the
    // numbers.
    const float gain = 1.0f + drive * drive * 90.0f;
    const float xMax = juce::jlimit (0.02f,
                                     juce::jmax (1.0f, (chokeRange + 1.0f) / gain),
                                     (4.0f + chokeRange) / gain);

    const auto toX = [&] (float x) { return juce::jmap (x, -xMax, xMax, plot.getX(), plot.getRight()); };
    const auto toY = [&] (float y) { return juce::jmap (y, -1.0f, 1.0f, plot.getBottom(), plot.getY()); };

    g.setColour (gridLine);
    for (int i = 1; i < 4; ++i)
    {
        const float fx = plot.getX() + plot.getWidth()  * (float) i / 4.0f;
        const float fy = plot.getY() + plot.getHeight() * (float) i / 4.0f;
        g.drawLine (fx, plot.getY(), fx, plot.getBottom(), 0.5f);
        g.drawLine (plot.getX(), fy, plot.getRight(), fy, 0.5f);
    }

    if (peak > 1.0e-4f)
    {
        const float band = juce::jmin (peak, xMax);
        g.setColour (laf.accent().withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (toX (-band), plot.getY(),
                                            toX (band) - toX (-band), plot.getHeight()));
    }

    g.setColour (JimothyLookAndFeel::stationGreen().withAlpha (0.55f));
    g.drawLine (plot.getX(), toY (0.0f), plot.getRight(), toY (0.0f), 1.0f);
    g.drawLine (toX (0.0f), plot.getY(), toX (0.0f), plot.getBottom(), 1.0f);

    juce::Path curve;
    const int steps = juce::jmax (16, (int) plot.getWidth());
    for (int i = 0; i <= steps; ++i)
    {
        const float x = juce::jmap ((float) i, 0.0f, (float) steps, -xMax, xMax);
        const float y = Jimothy::shape (x, drive, lean, choke, unit);

        if (i == 0) curve.startNewSubPath (toX (x), toY (y));
        else        curve.lineTo          (toX (x), toY (y));
    }

    g.setColour (laf.accent());
    g.strokePath (curve, juce::PathStrokeType (1.2f));

    // The resting operating point — violet when the charge has dragged it
    // off home.
    {
        const float restY = Jimothy::shape (0.0f, drive, lean, choke, unit);
        g.setColour (choke > 0.05f ? JimothyLookAndFeel::hiljaisuus()
                                   : laf.accentDim().brighter (0.4f));
        g.fillRect (juce::Rectangle<float> (toX (0.0f) - 1.5f, toY (restY) - 1.5f,
                                            3.0f, 3.0f));
    }

    paintLids (g, kneeEyeCentre, kneeOpenness);
}

void FeralFace::paintLids (juce::Graphics& g,
                           juce::Point<float> centre, float openness)
{
    // The lids are Jimothy's skin — fur tone, closing symmetrically over the
    // instrument. Caller has already clipped to the aperture.
    const float o   = juce::jlimit (0.0f, 1.0f, openness);
    const float gap = o * eyeRadius;
    const auto  eye = aperture (centre);

    g.setColour (furTone);
    g.fillRect (juce::Rectangle<float> (eye.getX(), eye.getY(),
                                        eye.getWidth(), eyeRadius - gap));
    g.fillRect (juce::Rectangle<float> (eye.getX(), centre.y + gap,
                                        eye.getWidth(), eyeRadius - gap));

    // A shut eye still shows its seam, so the panel reads as asleep rather
    // than empty.
    if (o < 0.12f)
    {
        g.setColour (laf.accentDim().withAlpha (0.8f));
        g.drawLine (centre.x - eyeRadius * 0.8f, centre.y,
                    centre.x + eyeRadius * 0.8f, centre.y, 1.0f);
    }
}
