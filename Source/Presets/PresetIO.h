#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../FX/JimothyPedal.h"

// ─────────────────────────────────────────────────────────────────────────────
// PresetIO — .jimpreset / .jimbank
// Wusik X1 is not GPL, or even released.
// The file convention is Wusik X1's, because it is a good one: a binary or text
// payload that opens with a single human-readable ASCII line naming the product,
// the kind of file, the date, and the build. `head -1` on a bank tells you what
// it is. That survives being emailed, renamed, and found on a backup drive three
// years later.
//
//   Wusik X1 - Bank Of Presets - Date: 25 Jul 2026 2:26pm - Version #00100
//
// Two fields are added that X1 does not carry, and their absence is the reason
// this file exists:
//
//   Schema   — the state format version. A newer schema is refused, not guessed.
//   ParamSet — a fingerprint of the ordered parameter ID list.
//
// X1 inserted sixty parameters at index 5 between two betas while leaving the
// VST3 class UID byte-identical. Hosts loaded the old state into the new build
// with no warning, no mismatch dialog, and silently wrong values. Every X1 beta
// reports FileVersion 1.0.0, so nothing in the file could have caught it.
//
// Jimothy stamps the fingerprint into every preset, bank, and session blob it
// writes, and checks it on the way back in.
// ─────────────────────────────────────────────────────────────────────────────

namespace Jimothy
{
    struct Preset
    {
        juce::String name;
        // Values in knob units (0–10, LEAN -10–10), keyed by ParamID.
        std::map<juce::String, float> values;
    };

    struct HeaderInfo
    {
        bool         valid    = false;
        juce::String product;
        juce::String kind;
        juce::String date;
        int          version  = 0;   // e.g. 100 for v0.1.0
        int          schema   = 0;
        juce::uint32 paramSet = 0;
    };

    class PresetIO
    {
    public:
        // Bump only when the FORMAT of the state changes incompatibly — when a
        // newer file can no longer be parsed correctly by an older reader.
        // 0.3 re-meant SPUTTER but did not touch the format, so schema stays 1:
        // a bump would have made v0.2 refuse v0.3 sessions outright, silently
        // hard-resetting every knob on downgrade — a strictly worse failure
        // than "SPUTTER acts old-style in the old build". Semantic changes are
        // caught by the product version stamp instead (see PluginProcessor's
        // SPUTTER migration and describeIncompatibility below).
        static constexpr int schemaVersion = 1;

        // major*10000 + minor*100 + patch, printed zero-padded as #00300.
        static constexpr int productVersion = 300;

        // The version that re-mechanized SPUTTER (random breaks → charge
        // starvation). State stamped older than this gets its sputter value
        // migrated and the user gets told.
        static constexpr int sputterRemechVersion = 300;

        // FNV-1a 32 over the ordered parameter IDs joined with '|'. Documented
        // and trivially reproducible outside C++ — tools/make_factory_bank.py
        // computes the same value, so generated banks and the plugin agree
        // without anyone copying a magic number by hand.
        static juce::uint32 parameterSetFingerprint() noexcept;

        static juce::String buildHeader (const juce::String& kind,
                                         const juce::Time& when = juce::Time::getCurrentTime());
        static HeaderInfo   parseHeader (const juce::String& line);

        // Why a loaded file cannot be trusted, or an empty string if it can.
        static juce::String describeIncompatibility (const HeaderInfo&);

        static juce::String toText   (const std::vector<Preset>&, bool asBank);
        static bool         fromText (const juce::String& text,
                                      std::vector<Preset>& out,
                                      juce::String& problem);

        // Applies a preset to an APVTS by parameter ID. IDs absent from the
        // preset keep their current value; IDs the plugin does not know are
        // ignored. Never positional.
        static void apply (const Preset&, juce::AudioProcessorValueTreeState&);
        static Preset capture (const juce::String& name,
                               juce::AudioProcessorValueTreeState&);
    };
}
