# Compatibility

This file lists only what has actually been tested. Anything not listed is untested.

## Build platforms

| Platform | Compiler | Status | Date |
|---|---|---|---|
| Ubuntu 24.04 x86_64 | GCC 13.3 | Builds; unit tests, hosting test and pluginval pass | 2026-11-22 |
| macOS (GitHub Actions `macos-latest`, arm64 + x86_64 universal) | Apple Clang | Builds; unit tests, hosting test and pluginval (strictness 8) pass on the arm64 runner | 2026-11-22 |
| Windows x64 (GitHub Actions `windows-latest`) | MSVC (Visual Studio 2022) | Set up in CI; results pending | — |
| Ubuntu 24.04 x86_64 (GitHub Actions) | GCC | Builds; unit tests, hosting test and pluginval (strictness 8) pass | 2026-11-22 |

## Validation

| Tool | Build | Result |
|---|---|---|
| pluginval 1.0.4, strictness 10 | ModdMudd FX, Linux | Pass |
| pluginval 1.0.4, strictness 8 | ModdMudd Instrument, Linux | Pass |
| pluginval 1.0.4, strictness 5 | both builds, Linux | Pass (includes opening the editor under Xvfb) |
| pluginval 1.0.4, strictness 8 | both builds, macOS arm64 (CI) | Pass |

The x86_64 half of the macOS universal binary is built but has not been run.

## Hosting (ModdMudd as a VST3 inside a host, hosting VST3s)

| Host | Hosted plug-ins | Result |
|---|---|---|
| JUCE 9.0.6 headless VST3 host (`ModdMuddHostTest`), Linux and macOS (CI) | MuddTestGain, MuddTestSynth (test plug-ins in this repo) | All 18 checks pass |

## DAWs

None tested yet. Please add rows as you test, with the DAW version, OS, the hosted plug-ins you tried, and what worked or didn't.

| DAW | Version | OS | Build | Hosted plug-ins | Result | Notes |
|---|---|---|---|---|---|---|
| | | | | | | |

Worth checking in each DAW:

1. Load both builds. Load an effect or instrument inside ModdMudd.
2. The hosted plug-in's own window opens, resizes, and closes cleanly.
3. Modulation is audible, and assign-by-touch works.
4. ModdMudd's DAW parameter names update after loading a plug-in.
5. DAW automation of a slot and of a macro both work.
6. Save, close and reopen the project: the hosted plug-in, its settings and the modulation come back.
7. MIDI reaches a hosted instrument. MIDI learn works.
8. Tempo-synced LFOs follow the transport.
9. Removing and replacing hosted plug-ins during playback doesn't glitch or crash.

Linux DAWs to try: Bitwig Studio, Reaper, Ardour, Tracktion Waveform, Carla.
macOS: Reaper, Bitwig Studio, Ableton Live, Cubase, Studio One.
Windows: Reaper, Bitwig Studio, Ableton Live, Cubase, Studio One, FL Studio. FL Studio is worth checking specifically: JUCE handles saved plug-in state differently in FL Studio, which affects ModdMudd reloading its hosted plug-ins.
