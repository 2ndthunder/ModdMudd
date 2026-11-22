# ModdMudd

A universal modulation VST3 plugin for macOS and Linux. Insert ModdMudd in your DAW, load a third-party VST3 instrument or effect inside it, and modulate that plugin's parameters with LFOs, randomizers, macros, an XY pad, a four-state preset blender and an envelope follower.

ModdMudd is an independent project, built with C++, JUCE 9 and CMake. It produces VST3 only: no AU, VST2, AAX or standalone builds.

## Status

Early development. See `docs/` for the architecture, build instructions, known limitations and the list of DAWs and plugins that have actually been tested.

## Builds

| Target | VST3 type | Hosts |
|---|---|---|
| ModdMudd FX | Audio effect | VST3 effects |
| ModdMudd Instrument | Instrument | A VST3 instrument, optionally followed by effects |

## Platform notes

- Logic Pro and GarageBand do not load VST3 plugins, so they cannot run ModdMudd.
- Pro Tools requires AAX, which ModdMudd does not build.
