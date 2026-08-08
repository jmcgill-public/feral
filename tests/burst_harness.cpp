// ─────────────────────────────────────────────────────────────────────────────
// burst_harness — the digital burst.cir
//
// Runs the SAME code the plugin ships (Source/FX/JimothyShape.h: shape,
// ChokeState, deriveUnit — there is no reimplementation to drift) through the
// protocols the hardware was measured with, and holds it to the behavioral
// contract from hardware/JIMOTHY_HW.md:
//
//   "SPUTTER eats sustain, decays, and the note after the hit.
//    It respects the hit itself."
//
// Scope: the mechanism only — preamp gain, charge organ, clipper. The tone
// stack, oversampling, gate, blend and warren are plumbing and are not under
// test; level metrics are computed mean-subtracted (and the onset thump is
// estimated through a modelled 1-pole 150 Hz highpass) because the shipped
// chain removes DC and any loudness claim that ignores that is a lie.
//
// Exit code 0 = contract holds. Nonzero = the animal is broken; fix the code,
// not the test. The tables printed here are what BRIEF.md publishes.
// ─────────────────────────────────────────────────────────────────────────────

#include "../Source/FX/JimothyShape.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace Jimothy;

namespace
{
    constexpr double fs      = 48000.0;
    constexpr double freq    = 500.0;    // the sim's stimulus; not a guitar,
                                         // same caveat as the hardware doc
    constexpr float  drive   = 0.55f;    // knob 5.5, the factory default
    constexpr float  gainAt  = 1.0f + drive * drive * 90.0f;   // ≈ 28.2

    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
        if (! ok) ++failures;
    }

    struct Rig
    {
        Unit       unit;
        ChokeState state;
        float      pumpFrac;
        float      pumpCoef;
        float      leakCoef;

        explicit Rig (float sputterNorm, uint32_t seed = 0)
            : unit (deriveUnit (seed))
        {
            pumpFrac = pumpFracForSputter (sputterNorm, unit);
            pumpCoef = 1.0f - std::exp (-1.0f / (float) (pumpTauSeconds * fs));
            leakCoef = std::exp (-1.0f / (float) (tauRecForSputter (sputterNorm, unit) * fs));
        }

        // One base-rate sample through preamp + charge + clipper, mono.
        // Half-wave rectified pump, same as Pedal::process.
        float step (float x, float lean = 0.0f)
        {
            const float choke = state.tick (std::max (x, 0.0f) * gainAt, pumpFrac,
                                            pumpCoef, leakCoef);
            return shape (x, drive, lean, choke, unit);
        }
    };

    struct Window
    {
        double sum = 0.0, sumSq = 0.0, peakAboveMean = 0.0;
        int    n = 0, conducting = 0;
        std::vector<float> samples;

        void add (float y) { samples.push_back (y); }

        // Metrics that can see (Brief 5 III): conduction duty and
        // mean-subtracted level. Peak-to-peak is blind to blocking.
        void finalise (const Unit& u)
        {
            for (float y : samples) { sum += y; ++n; }
            const double mean = n > 0 ? sum / n : 0.0;
            const float offBand = u.railLo + 0.05f * (u.railHi - u.railLo);
            for (float y : samples)
            {
                sumSq += (y - mean) * (y - mean);
                peakAboveMean = std::max (peakAboveMean, (double) y - mean);
                if (y > offBand) ++conducting;
            }
        }

        double duty() const { return n > 0 ? (double) conducting / n : 0.0; }
        double rms()  const { return n > 0 ? std::sqrt (sumSq / n) : 0.0; }
    };

    double dB (double lin) { return 20.0 * std::log10 (std::max (lin, 1.0e-9)); }
}

// ── Protocol 1 — steady state (the digital block.cir) ────────────────────────
// Continuous 500 Hz at a crest that exercises the curve hard, SPUTTER swept.
// Contract: duty collapses as starvation deepens; the spikes still contact the
// top rail (mean-subtracted peak does not fall); the steady-state RMS drop is
// accepted character, printed, not judged.
static void steadyState()
{
    std::printf ("\nP1 steady state — 500 Hz, amp 0.30 (crest %.1f biased), drive 5.5, unit 000\n",
                 0.30f * gainAt);
    std::printf ("  %-9s %-12s %-16s %-14s %s\n",
                 "SPUTTER", "duty", "peak-mean", "rms(mean-sub)", "charge");

    double dutyHealthy = 0.0, peakHealthy = 0.0;
    double dutyChoked  = 1.0, peakChoked  = 0.0;

    for (float s : { 0.0f, 0.3f, 0.7f, 1.0f })
    {
        Rig rig (s);
        Window w;
        const int n = (int) (2.0 * fs);
        const int settle = (int) (1.0 * fs);
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.30f * (float) std::sin (2.0 * 3.14159265358979 * freq * i / fs);
            const float y = rig.step (x);
            if (i >= settle) w.add (y);
        }
        w.finalise (rig.unit);
        std::printf ("  %-9.1f %-12.3f %-16.3f %-14.3f %.2f\n",
                     s * 10.0f, w.duty(), w.peakAboveMean, w.rms(), rig.state.charge);

        if (s == 0.0f) { dutyHealthy = w.duty(); peakHealthy = w.peakAboveMean; }
        if (s == 1.0f) { dutyChoked  = w.duty(); peakChoked  = w.peakAboveMean; }
    }

    check (dutyChoked < 0.6 * dutyHealthy,
           "P1 duty collapses at full starvation (choked < 60% of healthy)");
    check (peakChoked > 0.9 * peakHealthy,
           "P1 spikes still slam the rail (mean-sub peak within 10% of healthy)");
}

// ── Protocol 2 — burst then quiet note (the digital burst.cir) ───────────────
// 150 ms hit at 0.30, then a note at 0.06 (0.2×). Fresh-stage control gets the
// note with no history. Contract: the post-hit stage parks the note (duty ≈ 0
// in the +10–35 ms window); the fresh stage passes the note's attack then
// chokes it within 10–30 ms; charge decays with τ ≈ τ_rec.
static void burstThenNote()
{
    std::printf ("\nP2 burst-then-note — hit 0.30 x 150 ms, note 0.06, SPUTTER 10, unit 000\n");

    auto note = [] (int i) {
        return 0.06f * (float) std::sin (2.0 * 3.14159265358979 * freq * i / fs);
    };

    // Post-hit stage.
    Rig rig (1.0f);
    const int hitN = (int) (0.150 * fs);
    for (int i = 0; i < hitN; ++i)
        rig.step (0.30f * (float) std::sin (2.0 * 3.14159265358979 * freq * i / fs));

    const float chargeAtHitEnd = rig.state.charge;

    Window post10_35, noteBody;
    const int n10 = (int) (0.010 * fs), n35 = (int) (0.035 * fs);
    const int noteN = (int) (0.400 * fs);
    double chargeAt35 = 0.0;
    for (int i = 0; i < noteN; ++i)
    {
        const float y = rig.step (note (i));
        if (i >= n10 && i < n35) post10_35.add (y);
        if (i == n35) chargeAt35 = rig.state.charge;
        if (i < (int) (0.200 * fs)) noteBody.add (y);
    }
    post10_35.finalise (rig.unit);
    noteBody.finalise (rig.unit);

    // Fresh-stage control, judged against a HEALTHY stage playing the same
    // note — the hardware table compares across stages (choked note mean
    // 7.1 V vs healthy 0.9 V), not a stage against its own attack.
    Rig fresh (1.0f), healthy (0.0f);
    Window attack, later, healthyAttack, healthyLater;
    for (int i = 0; i < noteN; ++i)
    {
        const float y = fresh.step (note (i));
        const float h = healthy.step (note (i));
        // Attack = the first two cycles. The hardware's own measurement says a
        // fresh starved stage is already choking "within a few cycles", so a
        // wider window would average in samples the ground truth calls chewed.
        if (i < (int) (0.004 * fs)) { attack.add (y); healthyAttack.add (h); }
        if (i >= (int) (0.060 * fs) && i < (int) (0.200 * fs))
        {
            later.add (y);
            healthyLater.add (h);
        }
    }
    attack.finalise (fresh.unit);
    later.finalise (fresh.unit);
    healthyAttack.finalise (healthy.unit);
    healthyLater.finalise (healthy.unit);

    // Measure the recovery constant from the charge decay in silence.
    Rig decayRig (1.0f);
    for (int i = 0; i < hitN; ++i)
        decayRig.step (0.30f * (float) std::sin (2.0 * 3.14159265358979 * freq * i / fs));
    const float c0 = decayRig.state.charge;
    int tauSamples = 0;
    while (decayRig.state.charge > c0 * 0.3678794f && tauSamples < (int) (2 * fs))
    {
        decayRig.step (0.0f);
        ++tauSamples;
    }
    const double tauMeasured = tauSamples / fs;
    const double tauExpected = tauRecForSputter (1.0f, decayRig.unit);

    std::printf ("  charge at hit end          %.2f biased (crest was %.2f)\n",
                 chargeAtHitEnd, 0.30f * gainAt);
    std::printf ("  note duty +10-35 ms        %.3f   (post-hit — even the attack is eaten)\n",
                 post10_35.duty());
    std::printf ("  note duty, fresh attack    %.3f   vs healthy %.3f (the attack is respected)\n",
                 attack.duty(), healthyAttack.duty());
    std::printf ("  note duty, fresh @60-200ms %.3f   vs healthy %.3f (sustain is chewed)\n",
                 later.duty(), healthyLater.duty());
    std::printf ("  charge at +35 ms           %.2f   (still choking, like -1.0 V at +10-35 ms)\n",
                 chargeAt35);
    std::printf ("  recovery tau               %.3f s measured vs %.3f s design\n",
                 tauMeasured, tauExpected);

    check (post10_35.duty() < 0.10,
           "P2 note after the hit is parked (+10-35 ms duty < 0.10)");
    check (attack.duty() > 0.75 * healthyAttack.duty(),
           "P2 fresh stage passes the note's attack (duty within 25% of healthy)");
    check (later.duty() < 0.65 * healthyLater.duty(),
           "P2 fresh stage chokes its own sustain (duty < 65% of healthy)");
    check (chargeAt35 > 0.2f * chargeAtHitEnd,
           "P2 charge persists into the +10-35 ms window");
    check (std::abs (tauMeasured - tauExpected) < 0.25 * tauExpected,
           "P2 recovery tau within 25% of design value");
}

// ── Protocol 3 — punch-through and lean-blindness ────────────────────────────
static void punchThroughAndLean()
{
    std::printf ("\nP3 punch-through + lean-blindness, SPUTTER 10, unit 000\n");

    // Five equal hits with 100 ms gaps: every hit must reach the top rail.
    Rig rig (1.0f);
    bool allPunch = true;
    for (int hit = 0; hit < 5; ++hit)
    {
        float crest = -2.0f;
        for (int i = 0; i < (int) (0.120 * fs); ++i)
        {
            const float x = 0.30f * (float) std::sin (2.0 * 3.14159265358979 * freq * i / fs);
            crest = std::max (crest, rig.step (x));
        }
        allPunch = allPunch && crest > 0.9f * rig.unit.railHi;
        for (int i = 0; i < (int) (0.100 * fs); ++i)
            rig.step (0.0f);
    }
    check (allPunch, "P3 every equal hit punches through to the top rail");

    // Silence + lean at both extremes must never pump: the pump sees only the
    // signal (the digital hard constraint — the LEAN deletion, honored).
    for (float lean : { -1.0f, 1.0f })
    {
        Rig quiet (1.0f);
        for (int i = 0; i < (int) (1.0 * fs); ++i)
            quiet.step (0.0f, lean);
        check (quiet.state.charge == 0.0f,
               lean < 0 ? "P3 silence + LEAN -10 pumps nothing"
                        : "P3 silence + LEAN +10 pumps nothing");
    }

    // The charge trace must be identical with lean = ±0.4 vs 0 — structural in
    // the code, proven here so a regression cannot sneak the entanglement back.
    Rig a (1.0f), b (1.0f), c (1.0f);
    float maxDelta = 0.0f;
    for (int i = 0; i < (int) (0.300 * fs); ++i)
    {
        const float x = (i < (int) (0.150 * fs) ? 0.30f : 0.06f)
                      * (float) std::sin (2.0 * 3.14159265358979 * freq * i / fs);
        a.step (x, 0.0f); b.step (x, 0.4f); c.step (x, -0.4f);
        maxDelta = std::max ({ maxDelta,
                               std::abs (a.state.charge - b.state.charge),
                               std::abs (a.state.charge - c.state.charge) });
    }
    check (maxDelta == 0.0f, "P3 charge trace is lean-blind (delta == 0)");
}

// ── Protocol 4 — the healthy floor ───────────────────────────────────────────
static void healthyFloor()
{
    std::printf ("\nP4 healthy floor\n");

    // s = 0: charge identically zero whatever the input.
    Rig off (0.0f);
    float maxCharge = 0.0f;
    for (int i = 0; i < (int) (0.5 * fs); ++i)
    {
        off.step (0.9f * (float) std::sin (2.0 * 3.14159265358979 * freq * i / fs));
        maxCharge = std::max (maxCharge, off.state.charge);
    }
    check (maxCharge == 0.0f, "P4 SPUTTER 0 -> charge identically 0 (the v0.2 path)");

    // Below the junction drop nothing pumps at any SPUTTER: quiet playing at
    // low drive stays healthy. Crest here: 0.30 * (1 + 0.15^2*90) ≈ 0.9 < θ+…
    // — use drive 1.5 explicitly.
    ChokeState st;
    const Unit u = deriveUnit (0);
    const float lowGain = 1.0f + 0.15f * 0.15f * 90.0f;   // ≈ 3.0
    const float pumpCoef = 1.0f - std::exp (-1.0f / (float) (pumpTauSeconds * fs));
    const float leakCoef = std::exp (-1.0f / (float) (tauRecForSputter (1.0f, u) * fs));
    float charge = 0.0f;
    for (int i = 0; i < (int) (0.5 * fs); ++i)
    {
        const float x = 0.20f * (float) std::sin (2.0 * 3.14159265358979 * freq * i / fs);
        charge = st.tick (std::max (x, 0.0f) * lowGain, pumpFracForSputter (1.0f, u),
                          pumpCoef, leakCoef);
    }
    check (charge == 0.0f,
           "P4 below the junction drop nothing pumps (drive 1.5, amp 0.2, SPUTTER 10)");

    // deriveUnit(0) must be the v0.2 literals, bit-exactly.
    check (u.kPos == 1.55f && u.kNeg == 0.72f
           && u.railHi == 0.97f && u.railLo == -0.88f && u.number == 0,
           "P4 unit 000 is the reference clone (v0.2 constants, bit-exact)");

    // One absurd sample from a misbehaving upstream plugin must not latch the
    // charge (inf leaks to inf) or buy seconds of dead air. chokeMagCap is
    // the railing; this proves it holds.
    Rig absurd (1.0f);
    absurd.step (std::numeric_limits<float>::infinity());
    absurd.step (std::numeric_limits<float>::quiet_NaN());
    absurd.step (1.0e6f);
    const bool boundedNow = std::isfinite (absurd.state.charge)
                         && absurd.state.charge <= chokeMagCap;
    for (int i = 0; i < (int) (1.0 * fs); ++i)
        absurd.step (0.0f);
    check (boundedNow && absurd.state.charge < 0.01f,
           "P4 inf/NaN/1e6 input cannot latch the charge (bounded, inaudible in 1 s)");
}

// ── Protocol 5 — the edition ─────────────────────────────────────────────────
static void unitSpread()
{
    std::printf ("\nP5 unit spread — 20 seeds\n");
    bool bounds = true, numbered = true;
    for (uint32_t seed = 1; seed <= 20; ++seed)
    {
        const Unit u = deriveUnit (seed * 0x9E3779B9u + seed);
        bounds = bounds
            && u.kPos > 1.39f && u.kPos < 1.71f
            && u.kNeg > 0.64f && u.kNeg < 0.80f
            && u.railHi >  0.94f && u.railHi <  1.00f
            && u.railLo > -0.92f && u.railLo < -0.84f
            && u.tauRec > 0.12f  && u.tauRec < 0.28f
            && u.pumpMax > 0.93f && u.pumpMax <= 1.0f;
        numbered = numbered && u.number >= 1 && u.number <= 999;
        if (seed <= 3)
            std::printf ("  unit %03d [%08x]  k+%.2f/-%.2f  rails %.2f/+%.2f  tau %.2fs  pump %.2f\n",
                         u.number, u.seed, u.kPos, u.kNeg, u.railLo, u.railHi,
                         u.tauRec, u.pumpMax);
    }
    check (bounds,   "P5 every unit within shippable bounds");
    check (numbered, "P5 unit numbers in 001-999 (000 reserved)");
}

// ── Protocol 6 — informational: drive-step dropout and onset thump ───────────
// Printed, not judged: accepted character, published so nobody discovers it by
// support ticket. The thump estimate runs the output through a modelled 1-pole
// 150 Hz highpass standing in for the shipped tone stack's low cut.
static void informational()
{
    std::printf ("\nP6 informational (accepted character, measured)\n");

    // DRIVE 8 -> 2 mid-charge: how long until the note conducts again?
    {
        ChokeState st;
        const Unit u = deriveUnit (0);
        const float hiGain  = 1.0f + 0.8f * 0.8f * 90.0f;
        const float loGain  = 1.0f + 0.2f * 0.2f * 90.0f;
        const float pumpCoef = 1.0f - std::exp (-1.0f / (float) (pumpTauSeconds * fs));
        const float leakCoef = std::exp (-1.0f / (float) (tauRecForSputter (1.0f, u) * fs));
        const float pf = pumpFracForSputter (1.0f, u);

        for (int i = 0; i < (int) (0.5 * fs); ++i)
        {
            const float x = 0.30f * (float) std::sin (2.0 * 3.14159265358979 * freq * i / fs);
            st.tick (std::max (x, 0.0f) * hiGain, pf, pumpCoef, leakCoef);
        }

        int deadAir = 0;
        const int cap = (int) (3.0 * fs);
        while (deadAir < cap)
        {
            const float x = 0.30f * (float) std::sin (2.0 * 3.14159265358979 * freq * deadAir / fs);
            const float choke = st.tick (std::max (x, 0.0f) * loGain, pf, pumpCoef, leakCoef);
            if (0.30f * loGain > choke) break;   // crest conducts again
            ++deadAir;
        }
        std::printf ("  drive 8->2 mid-charge: %.0f ms of parked stage before the crest conducts\n",
                     1000.0 * deadAir / fs);
    }

    // Choke-onset thump through a 1-pole 150 Hz HP.
    {
        Rig rig (1.0f);
        const double rc = 1.0 / (2.0 * 3.14159265358979 * 150.0);
        const double a  = rc / (rc + 1.0 / fs);
        double hpPrevIn = 0.0, hpPrevOut = 0.0, thumpPeak = 0.0;
        for (int i = 0; i < (int) (0.100 * fs); ++i)
        {
            const float x = 0.30f * (float) std::sin (2.0 * 3.14159265358979 * freq * i / fs);
            const double y = rig.step (x);
            const double hp = a * (hpPrevOut + y - hpPrevIn);
            hpPrevIn = y; hpPrevOut = hp;
            if (i > (int) (0.002 * fs))
                thumpPeak = std::max (thumpPeak, std::abs (hp) - 1.06);
        }
        std::printf ("  onset residual above steady clip through 150 Hz HP: %.2f (%.1f dB re rail)\n",
                     std::max (thumpPeak, 0.0), dB (std::max (thumpPeak, 1e-9) / 0.97));
    }
}

int main()
{
    std::printf ("jimothy burst harness — the digital burst.cir\n");
    std::printf ("same code as the plugin: Source/FX/JimothyShape.h\n");

    steadyState();
    burstThenNote();
    punchThroughAndLean();
    healthyFloor();
    unitSpread();
    informational();

    std::printf ("\n%s (%d failure%s)\n",
                 failures == 0 ? "CONTRACT HOLDS" : "CONTRACT BROKEN",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
