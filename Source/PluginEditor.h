#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "GUI/JimothyLookAndFeel.h"
#include "GUI/FeralFace.h"

// ─────────────────────────────────────────────────────────────────────────────
// PedalKnob — caption above, rotary below. Labels yes, numbers no: the feral
// panel tells you what a knob is, never where it sits. Settings live in your
// ears or they don't live anywhere.
// ─────────────────────────────────────────────────────────────────────────────
class PedalKnob : public juce::Component
{
public:
    PedalKnob (const juce::String& caption,
               JimothyLookAndFeel& laf,
               juce::AudioProcessorValueTreeState& apvts,
               const juce::String& paramID);

    void resized() override;

private:
    juce::Label  label;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// ─────────────────────────────────────────────────────────────────────────────
// JimothyEditor — the feral panel. The face is the whole background; three
// labeled sections sit on its lower half; the eyes and whiskers do the work
// of the listening surface.
// ─────────────────────────────────────────────────────────────────────────────
class JimothyEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit JimothyEditor (JimothyProcessor&);
    ~JimothyEditor() override;

    void paint (juce::Graphics&) override;
    // Section headers must outdraw the face, which is a child component
    // covering the whole window — parent paint() runs before children, so
    // anything painted there disappears under the fur.
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    JimothyProcessor& processor;

    // Declared first: every child below takes a reference to it.
    JimothyLookAndFeel laf;

    FeralFace face { laf, processor.pedal.scope, processor.pedal.telemetry };

    PedalKnob drive, tone, level;
    PedalKnob lean, grime, sputter, gate;

    juce::Slider blendSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> blendAtt;

    juce::Slider warrenSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> warrenAtt;

    // Section headers, painted rather than laid out as components.
    juce::Rectangle<int> coreHeader, misbehaviorHeader, warrenHeader;

    double startTime = 0.0;

    // Legacy-notice redraw trigger: state restore can set it.
    uint32_t lastSeenSeed   = 0;
    bool     lastSeenLegacy = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JimothyEditor)
};
