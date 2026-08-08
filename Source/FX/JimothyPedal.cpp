#include "JimothyPedal.h"
#include <cmath>

using namespace Jimothy;

// The transfer function and the charge organ live in JimothyShape.h — pure,
// JUCE-free, and shared with the transfer display and tests/burst_harness, so
// the audio, the panel, and the published measurements are the same code.

void Pedal::prepare (double sr, int maxBlockSize, int numChannels)
{
    sampleRate = sr;
    maxBlock   = juce::jmax (1, maxBlockSize);
    channels   = juce::jlimit (1, 2, numChannels);

    const juce::dsp::ProcessSpec spec { sr,
                                        (juce::uint32) maxBlock,
                                        (juce::uint32) channels };

    // 4× is enough headroom for tanh plus a hard clip. Past that you are paying
    // for aliasing you cannot hear under this much grime.
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) channels, osFactorLog2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampler->initProcessing ((size_t) maxBlock);

    const float osLatency = oversampler->getLatencyInSamples();
    reportedLatency = juce::roundToInt (osLatency);

    // Boxy low cut — the low end is not invited to the drive stage.
    lowCut.prepare (spec);
    lowCut.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    lowCut.setCutoffFrequency (150.0f);
    lowCut.setResonance (0.707f);

    // Harshness — swept by TONE.
    harshness.prepare (spec);
    harshness.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    harshness.setCutoffFrequency (3000.0f);
    harshness.setResonance (0.9f);

    // Mid-forward: the nasal honk that puts a fuzz in front of the mix
    // instead of underneath it.
    auto midCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        sr, 900.0f, 0.8f, juce::Decibels::decibelsToGain (6.0f));
    for (auto& f : midPeak)
        f.coefficients = midCoeffs;

    // The dry path has to be pushed back by the oversampler's latency or BLEND
    // becomes a comb filter instead of a mix control.
    dryDelay.prepare (spec);
    dryDelay.setDelay (juce::jmax (1.0f, osLatency));

    dryBuffer.setSize (channels, maxBlock, false, true, false);

    warren.prepare (spec);
    warren.reset();
    warrenBuffer.setSize (channels, maxBlock, false, true, false);

    driveRamp.assign ((size_t) maxBlock, 0.0f);
    leanRamp .assign ((size_t) maxBlock, 0.0f);
    grimeRamp.assign ((size_t) maxBlock, 0.0f);
    chokeRamp.assign ((size_t) maxBlock, 0.0f);

    pumpCoef = 1.0f - std::exp (-1.0f / (float) (pumpTauSeconds * sr));

    constexpr double ramp = 0.02;
    smDrive.reset (sr, ramp);
    smLean .reset (sr, ramp);
    smGrime.reset (sr, ramp);
    smBlend.reset (sr, ramp);
    smLevel.reset (sr, ramp);
    smWarren.reset (sr, 0.05);   // a room does not appear instantly
    gateGain.reset (sr, 0.015);

    envAttackCoef  = std::exp (-1.0f / (float) (0.001 * sr));
    envReleaseCoef = std::exp (-1.0f / (float) (0.120 * sr));

    reset();
}

void Pedal::reset()
{
    if (oversampler != nullptr)
        oversampler->reset();

    lowCut.reset();
    harshness.reset();
    for (auto& f : midPeak)
        f.reset();

    dryDelay.reset();
    dryBuffer.clear();
    warren.reset();
    warrenBuffer.clear();
    scope.clear();

    envelope   = 0.0f;
    choke.reset();
    chokeOnset = false;
    gateGain.setCurrentAndTargetValue (1.0f);

    telemetry.inPeak.store (0.0f, std::memory_order_relaxed);
    telemetry.outPeak.store (0.0f, std::memory_order_relaxed);
    telemetry.gateOpen.store (true, std::memory_order_relaxed);
    telemetry.chokeBias.store (0.0f, std::memory_order_relaxed);
    telemetry.starved.store (0.0f, std::memory_order_relaxed);
}

void Pedal::setParameters (const Parameters& p)
{
    // Knobs are painted 0–10. Everything downstream works in normalised units.
    smDrive.setTargetValue (juce::jlimit (0.0f, 1.0f, p.drive * 0.1f));
    smLean .setTargetValue (juce::jlimit (-1.0f, 1.0f, p.lean  * 0.1f));
    smGrime.setTargetValue (juce::jlimit (0.0f, 1.0f, p.grime * 0.1f));
    smBlend.setTargetValue (juce::jlimit (0.0f, 1.0f, p.blend * 0.1f));
    smLevel.setTargetValue (juce::jlimit (0.0f, 1.0f, p.level * 0.1f));
    smWarren.setTargetValue (juce::jlimit (0.0f, 1.0f, p.warren * 0.1f));

    updateToneStack (juce::jlimit (0.0f, 1.0f, p.tone * 0.1f));

    // GATE: from wide open to aggressively rude. At zero the gate never closes.
    const float gate = juce::jlimit (0.0f, 1.0f, p.gate * 0.1f);
    gateThreshold = gate <= 0.0f
        ? 0.0f
        : juce::Decibels::decibelsToGain (juce::jmap (gate, -72.0f, -26.0f));

    // SPUTTER: how starved the stage's bias is — from healthy to spitting.
    // The knob's work happens per block in process(), where the unit's own
    // pump depth and recovery constant are known.
    sputterNorm = juce::jlimit (0.0f, 1.0f, p.sputter * 0.1f);
}

void Pedal::updateToneStack (float tone)
{
    harshness.setCutoffFrequency (juce::jmap (tone, 700.0f, 6800.0f));
}

void Pedal::process (juce::AudioBuffer<float>& buffer)
{
    const int numSamples  = juce::jmin (buffer.getNumSamples(), maxBlock);
    const int numChannels = juce::jmin (buffer.getNumChannels(), channels);

    if (numSamples <= 0 || numChannels <= 0)
        return;

    // ── Parameter ramps, one value per base-rate sample ──────────────────────
    for (int i = 0; i < numSamples; ++i)
    {
        driveRamp[(size_t) i] = smDrive.getNextValue();
        leanRamp [(size_t) i] = smLean .getNextValue();
        grimeRamp[(size_t) i] = smGrime.getNextValue();
    }

    // ── Which unit, and how starved ──────────────────────────────────────────
    // The unit is derived from the seed every block — pure and cheap. The
    // SPUTTER knob scales pump depth and recovery together, the way the
    // hardware chain's resistance sets both the starvation and Rb·C.
    const Unit unit = deriveUnit (unitSeed.load (std::memory_order_relaxed));

    pumpFrac = pumpFracForSputter (sputterNorm, unit);
    leakCoef = std::exp (-1.0f / (float) (tauRecForSputter (sputterNorm, unit) * sampleRate));

    const float knee = chokeKnee (unit);

    // ── Input peak, gate envelope, and the charge pump — all pre-drive ───────
    // The gate follows what you play, not what the fuzz does to it. The pump
    // follows the half-wave rectified driven signal, max(x·gain, 0) — the
    // junction pumps only when it conducts. LEAN is not in it, GRIME noise is
    // not in it: nothing pumps the charge but the signal. That is the digital
    // form of the hardware's hard constraint; the measured LEAN deletion
    // showed one static bias path is an off-switch for the whole mechanism.
    float inPeak = 0.0f;
    int   onsets = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        float mag = 0.0f, pos = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float x = buffer.getReadPointer (ch)[i];
            mag = juce::jmax (mag, std::abs (x));
            pos = juce::jmax (pos, x);
        }

        inPeak = juce::jmax (inPeak, mag);

        const float coef = mag > envelope ? envAttackCoef : envReleaseCoef;
        envelope = mag + coef * (envelope - mag);

        const float d      = driveRamp[(size_t) i];
        const float driven = pos * (1.0f + d * d * 90.0f);

        chokeRamp[(size_t) i] = choke.tick (driven, pumpFrac, pumpCoef, leakCoef);

        // Choke onsets, counted with hysteresis so the meter's flash means
        // "the stage just parked", not jitter around a threshold.
        const float starvedNow = chokeRamp[(size_t) i] / knee;
        if (! chokeOnset && starvedNow > 0.5f) { chokeOnset = true; ++onsets; }
        else if (chokeOnset && starvedNow < 0.3f) chokeOnset = false;
    }

    if (onsets > 0)
        telemetry.sputterCount.fetch_add (onsets, std::memory_order_relaxed);

    const bool gateIsOpen = gateThreshold <= 0.0f || envelope > gateThreshold;
    telemetry.inPeak.store (inPeak, std::memory_order_relaxed);
    telemetry.gateOpen.store (gateIsOpen, std::memory_order_relaxed);
    gateGain.setTargetValue (gateIsOpen ? 1.0f : 0.0f);

    // ── Stash the dry path before the drive stage eats it ────────────────────
    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // ── The nonlinearity, oversampled ────────────────────────────────────────
    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(),
                                        (size_t) numChannels,
                                        (size_t) numSamples);
    auto osBlock = oversampler->processSamplesUp (block);

    constexpr int osFactor = 1 << osFactorLog2;
    const int osSamples = (int) osBlock.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = osBlock.getChannelPointer ((size_t) ch);
        auto& r    = rng[(size_t) ch];

        for (int n = 0; n < osSamples; ++n)
        {
            const size_t base = (size_t) juce::jmin (n / osFactor, numSamples - 1);

            // GRIME goes in before the clipper, so it gets fuzzed along with
            // everything else rather than sitting on top as hiss. When the
            // stage is choked, noise riding a signal near the conduction edge
            // is what makes the spitting irregular — the misbehaviour emerges
            // from the curve, not from a dice roll.
            const float grime = grimeRamp[base];
            const float noise = grime > 0.0f
                ? (r.nextFloat() * 2.0f - 1.0f) * grime * 0.02f
                : 0.0f;

            data[n] = Jimothy::shape (data[n] + noise, driveRamp[base],
                                      leanRamp[base], chokeRamp[base], unit);
        }
    }

    oversampler->processSamplesDown (block);

    // ── Tone stack, misbehaviour, and the blend ──────────────────────────────
    float outPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // The sputter itself happened inside the clipper: a choked stage parks
        // on railLo and conducts only the tips that clear the charge. Nothing
        // to do here — the gate is the only post-stage misbehaviour left.
        const float misbehaviour = gateGain.getNextValue();
        const float blend        = smBlend.getNextValue();
        const float level        = smLevel.getNextValue();
        const float levelGain    = level * level;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* wet = buffer.getWritePointer (ch);

            float x = wet[i];
            x = lowCut.processSample (ch, x);
            x = midPeak[(size_t) ch].processSample (x);
            x = harshness.processSample (ch, x);
            x *= misbehaviour;

            dryDelay.pushSample (ch, dryBuffer.getSample (ch, i));
            const float dry = dryDelay.popSample (ch);

            wet[i] = (x * blend + dry * (1.0f - blend)) * levelGain;
        }
    }

    lowCut.snapToZero();
    harshness.snapToZero();
    for (auto& f : midPeak)
        f.snapToZero();

    // ── the warren ───────────────────────────────────────────────────────────
    // Linear, and last. The brief is explicit that the habitat is not the source
    // of the drive character — it is the room the pedal is standing in. Run
    // unconditionally rather than skipped at zero, so the tail is always in the
    // right state and turning it up does not splice into a stale convolution.
    for (int ch = 0; ch < numChannels; ++ch)
        warrenBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    {
        juce::dsp::AudioBlock<float> block (warrenBuffer.getArrayOfWritePointers(),
                                            (size_t) numChannels,
                                            (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);
        warren.process (context);
    }

    // ── output stage ─────────────────────────────────────────────────────────
    // Peak and scope are measured here, after everything, because the listening
    // surface has to show what actually leaves the pedal. A scope tapped before
    // the room would be telling you about a signal nobody hears.
    for (int i = 0; i < numSamples; ++i)
    {
        const float room = smWarren.getNextValue();
        float monoSum = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* out = buffer.getWritePointer (ch);
            out[i] += warrenBuffer.getSample (ch, i) * room;

            monoSum += out[i];
            outPeak = juce::jmax (outPeak, std::abs (out[i]));
        }

        scope.push (monoSum / (float) numChannels);
    }

    telemetry.outPeak.store (outPeak, std::memory_order_relaxed);
    telemetry.chokeBias.store (choke.charge, std::memory_order_relaxed);
    telemetry.starved.store (juce::jlimit (0.0f, 1.0f, choke.charge / knee),
                             std::memory_order_relaxed);
}

void Pedal::loadWarren (const void* irData, size_t irBytes)
{
    if (irData == nullptr || irBytes == 0)
        return;

    // Normalise::no — the IR is already energy-normalised by
    // tools/make_warren_ir.py, so convolution comes out near unity gain.
    warren.loadImpulseResponse (irData, irBytes,
                                juce::dsp::Convolution::Stereo::yes,
                                juce::dsp::Convolution::Trim::no,
                                0,
                                juce::dsp::Convolution::Normalise::no);
}

juce::AudioProcessorValueTreeState::ParameterLayout Pedal::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Pedal units. A knob on an enclosure goes 0 to 10, so these do too.
    const juce::NormalisableRange<float> knob    (0.0f, 10.0f, 0.01f);
    const juce::NormalisableRange<float> bipolar (-10.0f, 10.0f, 0.01f);

    auto text = [] (float v, int) { return juce::String (v, 1); };

    auto add = [&] (const char* id, const char* name,
                    const juce::NormalisableRange<float>& range, float def)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (text)));
    };

    add (ParamID::DRIVE,   "Drive",   knob,     5.5f);
    add (ParamID::TONE,    "Tone",    knob,     5.0f);
    add (ParamID::LEAN,    "Lean",    bipolar,  0.0f);
    add (ParamID::GRIME,   "Grime",   knob,     1.5f);
    add (ParamID::SPUTTER, "Sputter", knob,     0.0f);
    add (ParamID::GATE,    "Gate",    knob,     0.0f);
    add (ParamID::BLEND,   "Blend",   knob,    10.0f);
    add (ParamID::LEVEL,   "Level",   knob,     3.5f);
    add (ParamID::WARREN,  "Warren",  knob,     2.5f);

    return layout;
}
