#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// JimothyShape — the pure core — JUCE-free
//  * transfer curve
//  * charge (starvation)
//  * per-unit constants
//   Pedal::process        — the audio
//   TransferCurveDisplay  — the curve the panel draws
//   tests/burst_harness   — the measurements published in BRIEF.md
//
// We measured hardware tables for real circuits:
//   hardware/JIMOTHY_HW.md, spice/burst.cir
// Instead of running a component-level simulation, we informed a model
// with behavioral data derived from different (real) silicon fuzz pedals.
// Credit where due: the basic bazz fuss, the Minotaur Evil Eye, The Swede,
// and not-so-much-really the SF-2 and SF-300.
//
// Template-driven, pro-forma, lifted-from-well-known-sources, all GPL,
// using math I only understand superficially.
// TL;DR this is what you get when you _don't_ model your vst on spice.
// ─────────────────────────────────────────────────────────────────────────────

namespace Jimothy
{
    // ── Unit — the lid ───────────────────────────────────────────────────────
    // Knobs are performance, trimmers are identity. A hardware unit gets BIAS
    // and GATE trimmers, a CLIP socket, and a provenance sheet; a plugin
    // instance gets a seed. The spread below is the edition: bounded tight
    // enough that every unit is shippable, wide enough that two instances of
    // the same preset are audibly siblings, not clones. Ten sightings, one
    // cryptid.
    //
    // Seed 0 is reserved: it derives the exact v0.2 constants (unit 000, the
    // reference clone), which is what sessions saved before units existed
    // resolve to. Their sound does not change.
    struct Unit
    {
        float kPos    = 1.55f;   // tanh slope, positive half — clips harder
        float kNeg    = 0.72f;   // tanh slope, negative half
        float railHi  =  0.97f;  // lopsided hard rails
        float railLo  = -0.88f;
        float tauRec  = 0.20f;   // charge recovery constant at SPUTTER=10, s
        float pumpMax = 0.97f;   // how much of the over-threshold crest the
                                 // charge may approach, ≤ 1. The railing is
                                 // pumpTheta itself: the charge tops out at
                                 // pumpMax·(crest − θ) < crest, so the top θ
                                 // of every hit always conducts — the same
                                 // way the B-E junction drop guarantees
                                 // punch-through in hardware. The pump's
                                 // latency (τ_pump) is what lets the attack
                                 // of a fresh note escape. Tuned against the
                                 // harness contract (theory, not a measured
                                 // hardware constant).
        int      number = 0;     // painted unit number, 000–999
        uint32_t seed   = 0;
    };

    // splitmix32 — small, seedable, reproducible everywhere the header goes.
    inline uint32_t splitmix32 (uint32_t& state) noexcept
    {
        uint32_t z = (state += 0x9E3779B9u);
        z = (z ^ (z >> 16)) * 0x21F0AAADu;
        z = (z ^ (z >> 15)) * 0x735A2D97u;
        return z ^ (z >> 15);
    }

    inline Unit deriveUnit (uint32_t seed) noexcept
    {
        Unit u;                       // defaults are the v0.2 constants
        u.seed = seed;

        if (seed == 0)
            return u;                 // unit 000 — the reference clone

        uint32_t s = seed;
        auto spread = [&s] (float centre, float frac)
        {
            const float r = (float) (splitmix32 (s) >> 8) / (float) 0xFFFFFF;
            return centre * (1.0f + frac * (2.0f * r - 1.0f));
        };
        auto jitter = [&s] (float centre, float halfWidth)
        {
            const float r = (float) (splitmix32 (s) >> 8) / (float) 0xFFFFFF;
            return centre + halfWidth * (2.0f * r - 1.0f);
        };

        u.kPos    = spread (1.55f, 0.10f);
        u.kNeg    = spread (0.72f, 0.10f);
        u.railHi  = jitter ( 0.97f, 0.02f);
        u.railLo  = jitter (-0.88f, 0.03f);
        u.tauRec  = spread (0.20f, 0.35f);
        u.pumpMax = std::min (jitter (0.97f, 0.03f), 1.0f);

        // 000 is reserved for the reference clone; real units are 001–999.
        u.number  = 1 + (int) (splitmix32 (s) % 999u);
        return u;
    }

    // ── The transfer function ────────────────────────────────────────────────
    // Two tanh regions with different slopes either side of zero, offset by
    // LEAN, starved by the charge organ, run into a pair of lopsided rails.
    // `choke` is the pumped charge in biased units: it slides the operating
    // point down the curve toward cutoff, exactly the way the measured card's
    // base gets pumped negative. A deeply choked stage parks on railLo and
    // conducts only the tips that climb past the charge — the spit.
    //
    //   x     : input sample
    //   drive : 0–1 normalised
    //   lean  : −1–1 normalised static asymmetry offset
    //   choke : ≥ 0, biased-domain units, from ChokeState
    // A starved stage does not fade below its operating point — it switches
    // off. The junction's exponential makes hardware cutoff a razor, so
    // starvation sharpens the negative-side slope — quadratically in park
    // depth (charge over the choke knee), so the first milliseconds of an
    // attack barely feel it and a deeply parked stage falls off a cliff onto
    // railLo. At choke 0 the curve is the v0.2 original bit-exactly. Bounded
    // (park saturates at 1), and drawn as-is by the transfer display, cliff
    // and all.
    inline constexpr float starveSharpen = 60.0f;

    inline float shape (float x, float drive, float lean, float choke,
                        const Unit& u) noexcept
    {
        const float gain   = 1.0f + drive * drive * 90.0f;   // up to ~ +39 dB
        const float biased = x * gain + lean - choke;

        const float park = std::min (choke * u.kNeg * (1.0f / 2.2f), 1.0f);
        const float kNeg = u.kNeg * (1.0f + starveSharpen * park * park);
        const float k    = biased >= 0.0f ? u.kPos : kNeg;
        const float soft = std::tanh (k * biased);

        return std::clamp (soft * 1.06f, u.railLo, u.railHi);
    }

    // ── ChokeState — the charge organ ────────────────────────────────────────
    // One state variable, three rules, no exceptions:
    //
    //   1. Only the signal pumps it. drivenMag is the HALF-WAVE rectified
    //      driven signal, max(x·gain, 0) — the junction pumps only when it
    //      conducts, one polarity — and LEAN is not in it, GRIME noise is not
    //      in it. This is the digital form of the hardware's hard constraint
    //      ("no DC path to the card's base but the starved chain"): the
    //      measured LEAN deletion showed one static bias leg is an off-switch
    //      for the whole mechanism.
    //   2. Nothing pumps below the junction drop. target is zero until the
    //      driven crest clears pumpTheta, so silence and clean settings never
    //      choke at any SPUTTER — DRIVE is the how-easily-does-it-choke
    //      control, same as the hardware.
    //   3. The charge always leaks. τ_rec is the recovery the player hears
    //      (the note swelling back in), scaled per unit.
    //
    // drivenMag is the instantaneous rectified value; the pump-when-greater
    // rule does the smoothing (the pump only acts near crests — which is also
    // when a B-E junction rectifies). Pump, then leak, then flush denormals.
    inline constexpr float pumpTheta = 0.65f;   // biased units, ≈ the knee scale

    // The pump input is capped a few times past the choke knee. This is a
    // railing, twice over: a non-finite sample from a misbehaving upstream
    // plugin must not latch the charge at inf (inf leaks to inf — the pedal
    // would be silent until reset), and one absurd finite spike must not buy
    // seconds of dead air (charge is linear, recovery is exponential). With
    // the cap, the deepest possible park recovers in ~0.4 s — inside the
    // eats-the-note-after-the-hit contract.
    inline constexpr float chokeMagCap = 16.0f;

    struct ChokeState
    {
        float charge = 0.0f;

        // pumpCoef = 1 − exp(−1 / (fs · τ_pump)),  τ_pump = 6 ms
        // leakCoef =     exp(−1 / (fs · τ_rec)),   τ_rec  = 0.05 + s·unit.tauRec
        float tick (float drivenMag, float pumpFrac,
                    float pumpCoef, float leakCoef) noexcept
        {
            // Negated comparison so NaN also lands in the clamp.
            if (! (drivenMag <= chokeMagCap))
                drivenMag = chokeMagCap;

            const float excess = drivenMag - pumpTheta;
            const float target = excess > 0.0f ? pumpFrac * excess : 0.0f;

            if (target > charge)
                charge += (target - charge) * pumpCoef;

            charge *= leakCoef;

            if (charge < 1.0e-9f)
                charge = 0.0f;

            return charge;
        }

        void reset() noexcept { charge = 0.0f; }
    };

    inline constexpr float pumpTauSeconds = 0.006f;

    // SPUTTER knob (0–1 normalised) → pump depth. s=0 gives exactly zero, so
    // the charge is identically 0 and the stage is the healthy v0.2 path.
    // s^1.5 compresses the action toward the top of the knob, the same
    // direction the hardware sim pushed its pot taper (reverse-log).
    inline float pumpFracForSputter (float s, const Unit& u) noexcept
    {
        return std::min (u.pumpMax * s * std::sqrt (s), 1.0f);
    }

    // SPUTTER knob → recovery constant, seconds.
    inline float tauRecForSputter (float s, const Unit& u) noexcept
    {
        return 0.05f + s * u.tauRec;
    }

    // The charge at which the resting operating point is parked hard on
    // railLo (tanh(2.2) ≈ 0.976). Absolute and per-unit — the starved meter
    // divides by this, never by a decaying envelope.
    inline float chokeKnee (const Unit& u) noexcept
    {
        return 2.2f / u.kNeg;
    }
}
