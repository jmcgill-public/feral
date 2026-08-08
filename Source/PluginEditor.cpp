#include "PluginEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
// PedalKnob
// ─────────────────────────────────────────────────────────────────────────────

PedalKnob::PedalKnob (const juce::String& caption,
                      JimothyLookAndFeel& laf,
                      juce::AudioProcessorValueTreeState& apvts,
                      const juce::String& paramID)
{
    label.setText (caption, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setLookAndFeel (&laf);
    addAndMakeVisible (label);

    slider.setLookAndFeel (&laf);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (apvts, paramID, slider);
}

void PedalKnob::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromTop (14));
    slider.setBounds (area);
}

// ─────────────────────────────────────────────────────────────────────────────
// JimothyEditor
// ─────────────────────────────────────────────────────────────────────────────

JimothyEditor::JimothyEditor (JimothyProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      drive   ("DRIVE",   laf, p.apvts, Jimothy::ParamID::DRIVE),
      tone    ("TONE",    laf, p.apvts, Jimothy::ParamID::TONE),
      level   ("LEVEL",   laf, p.apvts, Jimothy::ParamID::LEVEL),
      lean    ("LEAN",    laf, p.apvts, Jimothy::ParamID::LEAN),
      grime   ("GRIME",   laf, p.apvts, Jimothy::ParamID::GRIME),
      sputter ("SPUTTER", laf, p.apvts, Jimothy::ParamID::SPUTTER),
      gate    ("GATE",    laf, p.apvts, Jimothy::ParamID::GATE)
{
    setLookAndFeel (&laf);

    setSize (440, 650);

    // Added before every control: the face is the background, the knobs sit
    // on Jimothy.
    addAndMakeVisible (face);

    for (auto* k : { &drive, &tone, &level, &lean, &grime, &sputter, &gate })
        addAndMakeVisible (*k);

    blendSlider.setLookAndFeel (&laf);
    blendSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    blendSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (blendSlider);
    blendAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (processor.apvts, Jimothy::ParamID::BLEND, blendSlider);

    warrenSlider.setLookAndFeel (&laf);
    warrenSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    warrenSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (warrenSlider);
    warrenAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (processor.apvts, Jimothy::ParamID::WARREN, warrenSlider);

    startTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (30);
}

JimothyEditor::~JimothyEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void JimothyEditor::timerCallback()
{
    // The eyes only tell the truth if they are reading live state.
    const float driveNorm = processor.apvts.getRawParameterValue (Jimothy::ParamID::DRIVE)->load() * 0.1f;
    const float leanNorm  = processor.apvts.getRawParameterValue (Jimothy::ParamID::LEAN) ->load() * 0.1f;

    // The one sanctioned motion in the design system: 0.15Hz. The ember
    // breathes through the whiskers.
    const double t = juce::Time::getMillisecondCounterHiRes() * 0.001 - startTime;
    const float pulse = JimothyLookAndFeel::hiljaisuusPulse (t);

    face.refresh (driveNorm, leanNorm, processor.pedal.getUnitSeed(), pulse);

    // A state restore can change the unit or set the legacy notice; repaint
    // when either moves.
    const auto seedNow   = processor.pedal.getUnitSeed();
    const bool legacyNow = processor.legacyStateLoaded.load (std::memory_order_relaxed);
    if (seedNow != lastSeenSeed || legacyNow != lastSeenLegacy)
    {
        lastSeenSeed   = seedNow;
        lastSeenLegacy = legacyNow;
        repaint();
    }
}

void JimothyEditor::paint (juce::Graphics& g)
{
    g.fillAll (JimothyLookAndFeel::voidBlack());

    JimothyLookAndFeel::drawHexGrid (g, getLocalBounds().toFloat(),
                                     20.0f, juce::Colour (0xFF130D0C));

    // No unit register, no product name — the face is the identification;
    // the host titlebar can do the paperwork. The legacy notice stays: it is
    // a functional warning, not identification.
    if (processor.legacyStateLoaded.load (std::memory_order_relaxed))
    {
        g.setFont (laf.terminal (9.0f));
        g.setColour (JimothyLookAndFeel::hiljaisuus());
        g.drawText (juce::String::fromUTF8 (
            "PRE-0.3 STATE \xe2\x80\x94 SPUTTER now starves (values \xc3\x97"
            "0.7, automation unchanged)"),
            0, getHeight() - 30, getWidth(), 14,
            juce::Justification::centred);
    }

    // Footer tagline, as ruled: long live Jimothy. The (c) year is Jimothy's,
    // not ours — nobody claims he isn't a time traveler.
    g.setColour (JimothyLookAndFeel::inactive().brighter (0.3f));
    g.setFont (laf.terminal (9.0f));
    g.drawText (juce::String::fromUTF8 (
        "El\xc3\xa4k\xc3\xb6\xc3\xb6n Jimothy  \xe2\x80\x94  (c) 2006 zero dollar studio"
        "  \xe2\x80\x94  v0.3-feral"),
        0, getHeight() - 16, getWidth(), 14,
        juce::Justification::centred);
}

void JimothyEditor::paintOverChildren (juce::Graphics& g)
{
    // American English — he's Washingtonian.
    g.setFont (laf.terminal (9.5f));
    g.setColour (JimothyLookAndFeel::dimHehku().brighter (0.5f));
    g.drawText ("BEHAVIOR",    coreHeader,        juce::Justification::centredLeft);
    g.drawText ("MISBEHAVIOR", misbehaviorHeader, juce::Justification::centredLeft);
    g.drawText ("THE WARREN",  warrenHeader,      juce::Justification::centredLeft);
}

void JimothyEditor::resized()
{
    // The face is the whole background; the three labeled sections sit on its
    // lower half. The top strip is left to the ears and the eyes.
    face.setBounds (getLocalBounds());

    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (290);

    coreHeader = area.removeFromTop (14);
    auto coreRow = area.removeFromTop (100);
    {
        const int w = coreRow.getWidth() / 3;
        drive.setBounds (coreRow.removeFromLeft (w));
        tone .setBounds (coreRow.removeFromLeft (w));
        level.setBounds (coreRow);
    }

    area.removeFromTop (10);

    misbehaviorHeader = area.removeFromTop (14);
    auto misRow = area.removeFromTop (100);
    {
        const int w = misRow.getWidth() / 4;
        lean   .setBounds (misRow.removeFromLeft (w));
        grime  .setBounds (misRow.removeFromLeft (w));
        sputter.setBounds (misRow.removeFromLeft (w));
        gate   .setBounds (misRow);
    }

    area.removeFromTop (10);

    // One header for the pair; the sliders themselves stay unlabeled and
    // unnumbered, full row each.
    warrenHeader = area.removeFromTop (14);
    blendSlider.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);
    warrenSlider.setBounds (area.removeFromTop (24));
}
