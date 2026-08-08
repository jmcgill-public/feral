#include "PresetIO.h"

using namespace Jimothy;

namespace
{
    juce::uint32 fnv1a (const juce::String& s) noexcept
    {
        juce::uint32 h = 2166136261u;

        for (auto* p = s.toRawUTF8(); *p != 0; ++p)
        {
            h ^= (juce::uint32) (juce::uint8) *p;
            h *= 16777619u;
        }

        return h;
    }

    // "25 Jul 2026 2:26pm" — the borrowed stamp: lowercase meridian, no zero pad
    // on the hour, so a bank written by either tool reads the same way.
    juce::String wusikDate (const juce::Time& t)
    {
        const int hour24 = t.getHours();
        const int hour12 = hour24 % 12 == 0 ? 12 : hour24 % 12;

        return juce::String (t.getDayOfMonth())
             + " " + t.getMonthName (true)
             + " " + juce::String (t.getYear())
             + " " + juce::String (hour12)
             + ":" + juce::String (t.getMinutes()).paddedLeft ('0', 2)
             + (hour24 < 12 ? "am" : "pm");
    }

    juce::String field (const juce::String& line, const juce::String& key)
    {
        const int at = line.indexOf (key);
        if (at < 0)
            return {};

        auto rest = line.substring (at + key.length()).trim();
        const int dash = rest.indexOf (" - ");
        return (dash >= 0 ? rest.substring (0, dash) : rest).trim();
    }
}

juce::uint32 PresetIO::parameterSetFingerprint() noexcept
{
    juce::StringArray ids;
    for (int i = 0; i < ParamID::count; ++i)
        ids.add (ParamID::ordered[i]);

    return fnv1a (ids.joinIntoString ("|"));
}

juce::String PresetIO::buildHeader (const juce::String& kind, const juce::Time& when)
{
    return "Jimothy - " + kind
         + " - Date: " + wusikDate (when)
         + " - Version #" + juce::String (productVersion).paddedLeft ('0', 5)
         + " - Schema " + juce::String (schemaVersion)
         + " - ParamSet " + juce::String::toHexString ((int) parameterSetFingerprint())
                                 .paddedLeft ('0', 8);
}

HeaderInfo PresetIO::parseHeader (const juce::String& line)
{
    HeaderInfo info;

    if (! line.startsWith ("Jimothy - "))
        return info;

    info.product = "Jimothy";
    info.kind    = field (line, "Jimothy - ");
    info.date    = field (line, "Date: ");
    info.version = field (line, "Version #").getIntValue();
    info.schema  = field (line, "Schema ").getIntValue();

    const auto ps = field (line, "ParamSet ");
    info.paramSet = (juce::uint32) ps.getHexValue64();

    // A file with no Schema field predates the guard, which means it predates
    // any release, which means it is not ours.
    info.valid = info.kind.isNotEmpty() && ps.isNotEmpty() && info.schema > 0;
    return info;
}

juce::String PresetIO::describeIncompatibility (const HeaderInfo& info)
{
    if (! info.valid)
        return "not a Jimothy preset file";

    if (info.schema > schemaVersion)
        return "written by a newer Jimothy (schema " + juce::String (info.schema)
             + ", this build reads " + juce::String (schemaVersion) + ")";

    if (info.paramSet != parameterSetFingerprint())
        return "parameter set differs from this build (file "
             + juce::String::toHexString ((int) info.paramSet)
             + ", build " + juce::String::toHexString ((int) parameterSetFingerprint())
             + ") — values are matched by name, anything unrecognised is left alone";

    // Same format, same parameters — but a knob can change what it MEANS
    // between versions, which no fingerprint can see. Say so.
    if (info.version > 0 && info.version < sputterRemechVersion)
        return "written for pre-0.3 SPUTTER (random breaks) — the knob now "
               "starves the stage instead; sputter values load as written "
               "and may want revoicing";

    return {};
}

juce::String PresetIO::toText (const std::vector<Preset>& presets, bool asBank)
{
    juce::XmlElement root (asBank ? "JIMOTHY_BANK" : "JIMOTHY_PRESET");

    for (const auto& p : presets)
    {
        auto* node = root.createNewChildElement ("PRESET");
        node->setAttribute ("name", p.name);

        // Written in canonical order so two banks with the same content produce
        // byte-identical files and diff cleanly.
        for (int i = 0; i < ParamID::count; ++i)
        {
            const juce::String id { ParamID::ordered[i] };
            const auto it = p.values.find (id);
            if (it == p.values.end())
                continue;

            auto* pn = node->createNewChildElement ("PARAM");
            pn->setAttribute ("id", id);
            pn->setAttribute ("value", juce::String (it->second, 2));
        }
    }

    return buildHeader (asBank ? "Bank Of Presets" : "Preset") + "\n"
         + root.toString (juce::XmlElement::TextFormat().withoutHeader());
}

bool PresetIO::fromText (const juce::String& text,
                         std::vector<Preset>& out,
                         juce::String& problem)
{
    const int nl = text.indexOfChar ('\n');
    if (nl < 0)
    {
        problem = "file has no header line";
        return false;
    }

    const auto info = parseHeader (text.substring (0, nl).trim());
    const auto trouble = describeIncompatibility (info);

    // A newer schema is refused outright — never guess at state.
    if (! info.valid || info.schema > schemaVersion)
    {
        problem = trouble;
        return false;
    }

    auto xml = juce::XmlDocument::parse (text.substring (nl + 1));
    if (xml == nullptr)
    {
        problem = "body is not valid XML";
        return false;
    }

    out.clear();
    for (auto* node : xml->getChildWithTagNameIterator ("PRESET"))
    {
        Preset p;
        p.name = node->getStringAttribute ("name", "Untitled");

        for (auto* pn : node->getChildWithTagNameIterator ("PARAM"))
            p.values[pn->getStringAttribute ("id")] =
                (float) pn->getDoubleAttribute ("value");

        out.push_back (std::move (p));
    }

    if (out.empty())
    {
        problem = "no presets in file";
        return false;
    }

    // A fingerprint mismatch is survivable — we match by name — but the caller
    // is told, rather than finding out by ear six months later.
    problem = trouble;
    return true;
}

void PresetIO::apply (const Preset& preset, juce::AudioProcessorValueTreeState& apvts)
{
    for (const auto& [id, value] : preset.values)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }
}

Preset PresetIO::capture (const juce::String& name,
                          juce::AudioProcessorValueTreeState& apvts)
{
    Preset p;
    p.name = name;

    for (int i = 0; i < ParamID::count; ++i)
    {
        const juce::String id { ParamID::ordered[i] };
        if (auto* raw = apvts.getRawParameterValue (id))
            p.values[id] = raw->load();
    }

    return p;
}
