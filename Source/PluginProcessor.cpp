#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>

JimothyProcessor::JimothyProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "JIMOTHY", Jimothy::Pedal::createParameterLayout())
{
    pDrive   = apvts.getRawParameterValue (Jimothy::ParamID::DRIVE);
    pTone    = apvts.getRawParameterValue (Jimothy::ParamID::TONE);
    pLean    = apvts.getRawParameterValue (Jimothy::ParamID::LEAN);
    pGrime   = apvts.getRawParameterValue (Jimothy::ParamID::GRIME);
    pSputter = apvts.getRawParameterValue (Jimothy::ParamID::SPUTTER);
    pGate    = apvts.getRawParameterValue (Jimothy::ParamID::GATE);
    pBlend   = apvts.getRawParameterValue (Jimothy::ParamID::BLEND);
    pLevel   = apvts.getRawParameterValue (Jimothy::ParamID::LEVEL);
    pWarren  = apvts.getRawParameterValue (Jimothy::ParamID::WARREN);

    pedal.loadWarren (BinaryData::warren_wav, (size_t) BinaryData::warren_wavSize);

    // A virgin insert mints a new unit. Anything that replays a state blob —
    // session reload, track duplication, host default presets, templates —
    // clones an existing one via setStateInformation, which is correct: a
    // duplicated track must sound identical or mixes break. Seed 0 is
    // reserved for the reference clone (the exact v0.2 constants), so a
    // fresh unit re-rolls until nonzero.
    auto& sysRng = juce::Random::getSystemRandom();
    uint32_t seed = 0;
    while (seed == 0)
        seed = (uint32_t) sysRng.nextInt64();
    pedal.setUnitSeed (seed);
}

bool JimothyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    // Mono and stereo only. A pedal has one jack in and one jack out; stereo is
    // a courtesy to people running it on a bus.
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    // Mono in / stereo out is fine. Stereo in / mono out is not — we would be
    // silently discarding a channel.
    return in.size() <= out.size();
}

void JimothyProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numChannels = juce::jmax (1, getTotalNumOutputChannels());

    pedal.prepare (sampleRate, samplesPerBlock, numChannels);
    setLatencySamples (pedal.getLatencyInSamples());
}

void JimothyProcessor::releaseResources()
{
    pedal.reset();
}

void JimothyProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                     juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    midiMessages.clear();

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // Mono in, stereo out: fan the input across the extra channels before the
    // pedal sees them. Never clear the input — this is an effect.
    for (int ch = numIn; ch < numOut; ++ch)
    {
        if (numIn > 0) buffer.copyFrom (ch, 0, buffer, 0, 0, buffer.getNumSamples());
        else           buffer.clear (ch, 0, buffer.getNumSamples());
    }

    Jimothy::Parameters params;
    params.drive   = pDrive  ->load();
    params.tone    = pTone   ->load();
    params.lean    = pLean   ->load();
    params.grime   = pGrime  ->load();
    params.sputter = pSputter->load();
    params.gate    = pGate   ->load();
    params.blend   = pBlend  ->load();
    params.level   = pLevel  ->load();
    params.warren  = pWarren ->load();

    pedal.setParameters (params);
    pedal.process (buffer);
}

void JimothyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (! state.isValid())
        return;

    // Stamp the session blob the same way a bank file is stamped. Without this
    // a future build with a different parameter set would load an old session
    // and silently mean something else by the same slots — the X1 failure.
    state.setProperty ("jimothySchema",   Jimothy::PresetIO::schemaVersion,  nullptr);
    state.setProperty ("jimothyVersion",  Jimothy::PresetIO::productVersion, nullptr);
    state.setProperty ("jimothyParamSet",
                       (int) Jimothy::PresetIO::parameterSetFingerprint(), nullptr);

    // Which Jimothy this is. The unit rides the session — a preset is knob
    // positions; the unit is which animal is wearing them.
    state.setProperty ("jimothyUnitSeed",
                       (juce::int64) pedal.getUnitSeed(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void JimothyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    const int schema = (int) tree.getProperty ("jimothySchema", 0);

    // Refuse state from a newer schema outright. Loading it would be a guess,
    // and a guess here is indistinguishable from working correctly until the
    // moment it isn't.
    if (schema > Jimothy::PresetIO::schemaVersion)
    {
        DBG ("Jimothy Feral: refusing session state from schema " << schema);
        return;
    }

    // A differing fingerprint is survivable, because APVTS restores by
    // parameter ID rather than by index. Unknown IDs are dropped, missing ones
    // keep their default. Say so rather than restoring quietly.
    const auto stamped = (juce::uint32) (int) tree.getProperty ("jimothyParamSet", 0);
    if (stamped != 0 && stamped != Jimothy::PresetIO::parameterSetFingerprint())
        DBG ("Jimothy Feral: session parameter set "
             << juce::String::toHexString ((int) stamped)
             << " differs from this build; restoring by name");

    // ── SPUTTER migration, pre-0.3 state ─────────────────────────────────────
    // 0.3 re-mechanized SPUTTER: random transient breaks became charge-memory
    // starvation. The parameter ID is unchanged, so the fingerprint guard
    // cannot see this — the stamped version is what catches it. Old values
    // are remapped ×0.7 toward a comparable amount of disruption, and the
    // editor shows a visible notice (automation lanes cannot be migrated).
    const int stampedVersion = (int) tree.getProperty ("jimothyVersion", 0);
    if (stampedVersion < 300)
    {
        for (auto param : tree)
        {
            if (param.hasType ("PARAM")
                && param.getProperty ("id").toString() == juce::String (Jimothy::ParamID::SPUTTER))
            {
                const float old = (float) param.getProperty ("value", 0.0f);
                if (old > 0.0f)
                    param.setProperty ("value", juce::jlimit (0.0f, 10.0f, old * 0.7f), nullptr);
            }
        }

        legacyStateLoaded.store (true, std::memory_order_relaxed);
    }
    else
    {
        // Loading current-version state (host preset browser, A/B slot) makes
        // the legacy notice a lie about the state actually in use — clear it.
        legacyStateLoaded.store (false, std::memory_order_relaxed);
    }

    apvts.replaceState (tree);

    // The unit. Absent from older state → seed 0 → unit 000, the reference
    // clone with the exact v0.2 curve: old sessions do not change sound.
    pedal.setUnitSeed ((uint32_t) (juce::int64) tree.getProperty ("jimothyUnitSeed", (juce::int64) 0));
}

juce::AudioProcessorEditor* JimothyProcessor::createEditor()
{
    return new JimothyEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JimothyProcessor();
}
