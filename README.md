# Jimothy Feral

A hyperfuzz that will not be tamed. VST3/AU/Standalone, Windows-first.

This is the released edition of Jimothy. The full version exists and is not
released. What this one doesn't have is paperwork: no presets (the host menu
shows one entry — *it starts here*), no numbers on the knobs, no meters.
Instead you get Jimothy's eyes: the right one is the gate, the left one is the
knee, and behind each lid is a clipped glimpse of the real instrument — drawn
by the same code the audio goes through, so even the glimpse cannot lie. If
the whiskers are breathing, the instance is alive.

[Readme.txt](Readme.txt) is the shipped documentation.

## Build

```
cmake -B build
cmake --build build --config Release
```

JUCE 7.0.9 is fetched automatically at configure time. The burst harness
(`ctest`-free, JUCE-free) builds alongside and enforces the behavioral
contract:

```
./build/Release/jimothy_harness
```

## License

Two licenses, one animal — see [LICENSE](LICENSE) for the map:

* **Software** — [AGPL-3.0-or-later](COPYING).
* **Artwork** (`art/jimothy-face.svg`, the face) — [CC BY-NC-ND 4.0](LICENSE-ART).
  Fork the code freely; Jimothy himself is not yours to rebrand. Jimothy is a
  real raccoon, and his likeness stays his.

Eläköön Jimothy.
