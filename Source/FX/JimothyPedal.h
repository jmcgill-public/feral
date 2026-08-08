#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>

#include "JimothyShape.h"

// ─────────────────────────────────────────────────────────────────────────────
// JimothyPedal — the nonlinear core
//
// A hyperfuzz in the SF-2 / SF-300 family, with a trash-panda temperament.
// The pedal is the nonlinearity plus the charge organ that starves it (both in
// JimothyShape.h); everything else here exists to keep the mechanism legible
// and to stop it aliasing into gravel.
//
// Signal flow (see BRIEF.md):
//   preamp  →  4× oversampled { grime + starved asymmetric clip }  →  tone
//           →  gate  →  blend against delay-compensated dry  →  level
// ─────────────────────────────────────────────────────────────────────────────

namespace Jimothy
{
    namespace ParamID
    {
        inline constexpr const char* DRIVE   = "drive";
        inline constexpr const char* TONE    = "tone";
        inline constexpr const char* LEAN    = "lean";
        inline constexpr const char* GRIME   = "grime";
        inline constexpr const char* SPUTTER = "sputter";
        inline constexpr const char* GATE    = "gate";
        inline constexpr const char* BLEND   = "blend";
        inline constexpr const char* LEVEL   = "level";
        inline constexpr const char* WARREN  = "warren";

        // ── The canonical order ──────────────────────────────────────────────
        // This array is the parameter set's identity. PresetIO fingerprints it,
        // and that fingerprint is stamped into every preset, bank, and saved
        // session so a state blob written by one build cannot be silently
        // loaded by a build that means something different by the same slots.
        //
        // APPEND ONLY. Never insert, never reorder, never delete. Wusik X1
        // inserted sixty parameters at index 5 between two betas while keeping
        // the VST3 class UID byte-identical, and hosts loaded the old state with
        // no warning and no mismatch dialog. That is the failure this prevents.
        inline constexpr const char* ordered[] =
        {
            DRIVE, TONE, LEAN, GRIME, SPUTTER, GATE, BLEND, LEVEL,
            WARREN,   // appended in 0.2.0 — see the note above about why never inserted
        };

        inline constexpr int count = (int) (sizeof (ordered) / sizeof (ordered[0]));
    }

    // Knob positions, in pedal units: 0–10, as painted on the enclosure.
    // LEAN is the exception — it is bipolar, with a centre detent at zero.
    struct Parameters
    {
        float drive   = 5.5f;
        float tone    = 5.0f;
        float lean    = 0.0f;
        float grime   = 1.5f;
        float sputter = 0.0f;
        float gate    = 0.0f;
        float blend   = 10.0f;
        float level   = 3.5f;
        float warren  = 2.5f;
    };

    // ── ScopeFifo ────────────────────────────────────────────────────────────
    // Single-producer (audio thread) / single-consumer (message thread) ring of
    // post-clip samples. The listening surface reads the most recent window.
    class ScopeFifo
    {
    public:
        static constexpr int size = 2048;

        void push (float v) noexcept
        {
            const int w = writeIndex.load (std::memory_order_relaxed);
            buffer[(size_t) (w & (size - 1))].store (v, std::memory_order_relaxed);
            writeIndex.store (w + 1, std::memory_order_release);
        }

        // Copies the most recent `n` samples into dest, oldest first.
        void read (float* dest, int n) const noexcept
        {
            const int w = writeIndex.load (std::memory_order_acquire);
            for (int i = 0; i < n; ++i)
                dest[i] = buffer[(size_t) ((w - n + i) & (size - 1))]
                              .load (std::memory_order_relaxed);
        }

        void clear() noexcept
        {
            for (auto& s : buffer) s.store (0.0f, std::memory_order_relaxed);
        }

    private:
        std::array<std::atomic<float>, size> buffer {};
        std::atomic<int> writeIndex { 0 };
    };

    // ── Telemetry ────────────────────────────────────────────────────────────
    // What the signal meter reports. Written on the audio thread, read on the
    // message thread. The instrument must be self-evidencing.
    struct Telemetry
    {
        std::atomic<float> inPeak       { 0.0f };  // linear, pre-drive
        std::atomic<float> outPeak      { 0.0f };  // linear, post-level
        std::atomic<bool>  gateOpen     { true };
        std::atomic<int>   sputterCount { 0 };     // lifetime choke onsets
        std::atomic<float> chokeBias    { 0.0f };  // charge, biased units —
                                                   // the transfer display
                                                   // draws the curve with it
        std::atomic<float> starved      { 0.0f };  // charge / chokeKnee, 0–1
    };

    // ── Pedal ────────────────────────────────────────────────────────────────
    class Pedal
    {
    public:
        Pedal() = default;

        void prepare (double sampleRate, int maxBlockSize, int numChannels);
        void reset();

        // The warren — the cabinet/habitat impulse response. Passed in from the
        // processor so the DSP does not have to know where resources live.
        void loadWarren (const void* irData, size_t irBytes);

        // Safe to call from the audio thread once per block.
        void setParameters (const Parameters& params);

        void process (juce::AudioBuffer<float>& buffer);

        int getLatencyInSamples() const noexcept { return reportedLatency; }

        static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

        // ── Which Jimothy this is ────────────────────────────────────────────
        // Written by the message thread (construction, state restore), read by
        // the audio thread once per block. The transfer function itself is
        // Jimothy::shape() in JimothyShape.h — pure and shared, so the curve
        // drawn in the UI, the audio, and the harness cannot disagree.
        void     setUnitSeed (uint32_t seed) noexcept { unitSeed.store (seed, std::memory_order_relaxed); }
        uint32_t getUnitSeed() const noexcept         { return unitSeed.load (std::memory_order_relaxed); }

        Telemetry telemetry;
        ScopeFifo scope;

    private:
        void updateToneStack (float tone);

        double sampleRate    = 44100.0;
        int    maxBlock      = 512;
        int    channels      = 2;
        int    reportedLatency = 0;

        // Nonlinear stage
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
        static constexpr int osFactorLog2 = 2;   // 4× — enough for tanh + hard clip
        std::vector<float> driveRamp, leanRamp, grimeRamp, chokeRamp;

        // Tone stack: boxy low cut → mid-forward peak → harshness lowpass
        juce::dsp::StateVariableTPTFilter<float> lowCut;
        juce::dsp::StateVariableTPTFilter<float> harshness;
        std::array<juce::dsp::IIR::Filter<float>, 2> midPeak;

        // Habitat
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> dryDelay { 64 };
        juce::AudioBuffer<float> dryBuffer;

        // The warren — linear, and last. A room, not a drive stage.
        juce::dsp::Convolution warren;
        juce::AudioBuffer<float> warrenBuffer;

        // Smoothed knob values, in normalised units
        juce::SmoothedValue<float> smDrive, smLean, smGrime, smBlend, smLevel, smWarren;

        // Gate — level-dependent collapse, the fuzz's rude noise gate
        float envelope        = 0.0f;
        float envAttackCoef   = 0.0f;
        float envReleaseCoef  = 0.0f;
        float gateThreshold   = 0.0f;   // linear
        juce::SmoothedValue<float> gateGain;

        // Sputter — charge-memory starvation. The state machine is
        // Jimothy::ChokeState (JimothyShape.h); these are its per-block
        // control values, derived from the SPUTTER knob and the unit.
        Jimothy::ChokeState choke;
        float pumpFrac      = 0.0f;
        float pumpCoef      = 0.0f;
        float leakCoef      = 0.0f;
        float sputterNorm   = 0.0f;
        bool  chokeOnset    = false;    // hysteresis state for the counter

        std::atomic<uint32_t> unitSeed { 0 };

        std::array<juce::Random, 2> rng { juce::Random (0x4A494D31),
                                          juce::Random (0x4A494D32) };
    };
}
