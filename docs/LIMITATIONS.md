# Known limitations

## Platform and DAW

- **Logic Pro and GarageBand can't run ModdMudd.** They don't load VST3, and ModdMudd builds VST3 only.
- **Pro Tools can't run ModdMudd.** It requires AAX, which is excluded.
- **Parameter names in the DAW.** ModdMudd's 256 DAW slots are renamed after the hosted plug-in's parameters, and ModdMudd asks the host to re-read parameter info. Some DAWs may keep showing "Slot 001" etc. until the project is reloaded. Automation still works either way, because the slot IDs never change.
- **More than 256 parameters.** Only the first 256 automatable parameters across all hosted plug-ins get DAW slots. The rest can still be modulated, but can't be automated from the DAW.
- **MIDI into the FX build.** Some DAWs (Ableton Live in particular) make it awkward to send MIDI to an audio effect. Without it, MIDI learn and note-retriggered LFOs can't work in the FX build.
- **Large parameter list.** JUCE adds 2048 hidden parameters that carry MIDI CC (VST3 delivers CCs as parameters). Hosts normally hide them.

## Hosting

- **Hosted plug-in windows on Linux** are embedded with X11 (XEmbed), nested inside the DAW's window. This is the least predictable part of the system. If a plug-in's window misbehaves, use ModdMudd's **Parameters** view, which works for every plug-in.
- **Hosted sidechains and extra outputs** aren't used. ModdMudd connects each hosted plug-in's main bus only.
- **Mono plug-ins in a stereo slot** receive the average of left and right, and their output is copied to both channels.
- **Plug-ins that edit parameters without begin/end gestures** can't change the base value of a modulated parameter from their own window. Use ModdMudd's Parameters view.
- **Scanning happens inside the DAW process.** A plug-in that crashes while being scanned takes the DAW down. On the next scan it is skipped automatically.
- **Hosted plug-in state calls** must run on the message thread (JUCE's VST3 host requires it). DAWs normally save and restore on that thread. If one doesn't, ModdMudd defers restoring, and saving relies on JUCE's message-manager lock.
- **Up to 4 hosted plug-ins** in series. Parallel routing isn't implemented.

- **Narrow plug-ins.** ModdMudd's panel needs at least 660 px. A plug-in whose window is narrower and can't be resized is centred above the panel, rather than matching its width exactly.

## Modulation

- **Time resolution** is the sub-block size (32 samples by default), not per sample, because JUCE 9.0.6 has no sample-accurate path for hosted parameters. Smaller sub-blocks cost more CPU, and a few plug-ins handle very small blocks poorly. If a plug-in misbehaves, raise the setting.
- **Synced LFOs and randomizers stop locking to the bar grid** while their rate is cross-modulated. They free-run at the modulated rate instead.
- **Envelope follower latency.** It reads the audio of the sub-block it is in, so modulation from it lags by up to one sub-block. In the Instrument build there is no audio input: use the Output source.
- **Shapers play their whole curve on each trigger.** There's no sustain stage held while a note is down, and note-offs are ignored. In the FX build, audio triggering listens to the input. The Instrument build has no input, so it listens to its own output.
- **The preset blender stores base values**, i.e. what you set, not what modulation produced. Discrete parameters switch to the nearest snapshot. They don't interpolate.

## Not done yet

- Signed and notarized macOS builds, and an installer.
- Factory presets.
- Live modulation rings on hosted plug-ins' own windows. Assignment there is by touch or through the Parameters view.
- Testing in actual DAWs (see `COMPATIBILITY.md`).
