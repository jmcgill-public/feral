#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FX/JimothyPedal.h"
#include "Presets/PresetIO.h"

// ─────────────────────────────────────────────────────────────────────────────
// JimothyProcessor — Jimothy Feral, VST3/AU effect
//
// An effect, not an instrument. It does not clear its input buffer, it does not
// ask for MIDI, and it registers as Fx|Distortion so the host will let you put
// it on a guitar track where it belongs.
//
// The feral edition carries no presets: one host program, named for what it is.
// Session state still round-trips in full — a host reloading its own project
// is not preset loading, and breaking it would be sabotage rather than an
// edition.
// ─────────────────────────────────────────────────────────────────────────────

class JimothyProcessor : public juce::AudioProcessor
{
public:
    JimothyProcessor();
    ~JimothyProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return "Jimothy Feral"; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return "it starts here"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    Jimothy::Pedal pedal;

    // True after loading state saved by a pre-0.3 build. SPUTTER was
    // re-mechanized in 0.3 (random breaks → charge starvation) and host
    // automation lanes cannot be migrated, so the editor shows this instead
    // of a DBG that compiles out of release.
    std::atomic<bool> legacyStateLoaded { false };

private:
    // Cached so the audio thread never does a string lookup.
    std::atomic<float>* pDrive   = nullptr;
    std::atomic<float>* pTone    = nullptr;
    std::atomic<float>* pLean    = nullptr;
    std::atomic<float>* pGrime   = nullptr;
    std::atomic<float>* pSputter = nullptr;
    std::atomic<float>* pGate    = nullptr;
    std::atomic<float>* pBlend   = nullptr;
    std::atomic<float>* pLevel   = nullptr;
    std::atomic<float>* pWarren  = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JimothyProcessor)
};
