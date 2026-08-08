Jimothy [Feral Edition]
v0.3.0 - Windows - VST3
No Taming

INSTALL

Copy Jimothy Feral.vst3 into:

    C:\Program Files\Common Files\VST3\

That is the whole install. There is no installer, and there is nothing here
Windows will block: a .vst3 is loaded by your DAW, never launched by Explorer,
so SmartScreen never evaluates it and no "Windows protected your PC" prompt can
appear. If your host does not see it after copying, rescan your plugin folders.

Jimothy is an effect. Put it on a track that already has audio on it - a guitar,
a bass, a drum bus. It is not an instrument and will not respond to MIDI.

WHAT THIS ISN'T

  * Presets. Your host's preset menu shows one entry: "it starts here".
    It does. Your session still saves and reloads exactly as you left it -
    what you cannot do is save a sound and carry it somewhere else.

  * Numbers. Every knob says what it is and never says where it sits.

  * Meters. The full version has a transfer curve display, a scope, and a
    signal meter that doesn't lie. Here you get Jimothy's spirit:
      The right eye (yours, not his) is the gate. Open when signal passes,
      shut when it collapses. Behind the lid, the post-pedal waveform.
      The left eye is the knee. It narrows as hard playing starves gain, 
      and reopens as the note swells. Behind his eyelid, clip.

SOURCE

The software is AGPL-3 free software; the face is CC BY-NC-ND. The code is
yours to fork, Jimothy is not. Full texts: COPYING and LICENSE-ART.

The source this was built from is in this zip under source\. Build it:
cmake -B build, then cmake --build build --config Release. JUCE 7.0.9 arrives
at configure time from github.com/juce-framework/JUCE under its GPL-3 tier.

It lives at github.com/jmcgill-public/feral.

Elakoon Jimothy.
