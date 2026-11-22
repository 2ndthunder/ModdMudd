# ModdMudd architecture

ModdMudd is a VST3 plug-in that hosts other VST3 plug-ins and modulates their parameters. This document describes how it is put together and why.

## Builds

One codebase produces two VST3 plug-ins (`CMakeLists.txt`):

| Target | VST3 category | Buses | Hosts |
|---|---|---|---|
| ModdMudd FX | `Fx` | stereo in, stereo out, optional stereo sidechain | VST3 effects |
| ModdMudd Instrument | `Instrument Synth` | stereo out | a VST3 instrument in slot 1, effects after it |

DAWs decide where a plug-in can be inserted from its declared category, so one binary can't be both. `MODDMUDD_IS_INSTRUMENT` selects the build at compile time.

Only VST3 is built (`FORMATS VST3`), and only VST3 is hosted (`JUCE_PLUGINHOST_VST3=1`, all other host formats off).

## Source layout

```
Source/
  Core/      Constants (sizes, source and target IDs), RtSwap (lock-free state hand-off)
  Engine/    Modulators and routing. Depends only on juce_core, juce_audio_basics and
             juce_data_structures, so it is unit-tested without any plug-in code.
  Host/      PluginCatalog (scanning, plug-in list), HostedSlot (one hosted plug-in)
  Plugin/    The AudioProcessor, parameters, state save/restore
  UI/        Editor, look-and-feel, modulator panels, matrix, hosted view
tests/       Engine unit tests, two tiny test VST3s, the end-to-end hosting test,
             and a development tool that renders the editor to PNG
```

## Threads and data flow

```
 message thread                                   audio thread
 ──────────────                                   ────────────
 UI edits / DAW state load                        processBlock()
        │                                               │
        ▼                                               │
 model: slots, ModMatrix, snapshots,                    │
        custom waves, slot map, seeds                   │
        │ rebuild()                                     │
        ▼                                               ▼
 RtState (immutable) ──── RtSwap::publish ───▶  RtSwap::acquire()
                                                        │
 timer: collectGarbage(), delete retired       sub-block loop:
        plug-ins once acknowledged              engine → hosted param writes → hosted process
```

- **Model** (`ModdMuddProcessor`, message thread). Hosted slots, the modulation matrix, blender snapshots, custom LFO waveforms, random seeds and the DAW-slot map. `modelLock` guards it against a DAW calling `getStateInformation` from another thread; the audio thread never takes this lock.
- **RtState**. Everything the audio thread needs, resolved to indices: slot pointers, the routing plan (connections grouped per destination, cross-modulation evaluation order), blender entries, DAW-slot mappings, LFO tables. It is rebuilt and published whenever the model changes.
- **RtSwap** (`Source/Core/RtSwap.h`). The audio thread takes the newest state with one atomic exchange per block. Old states go to a fixed-size retire queue that the message thread empties. Nothing on the audio side allocates, locks or deletes.
- **Removing a hosted plug-in.** The slot is taken out of the model, a new state without it is published, and the slot goes to a graveyard tagged with that generation. It is destroyed on the message thread once the audio thread has acknowledged a state at least that new. Editors are told first (`Listener::slotsAboutToChange`) so the hosted plug-in's window is deleted before the plug-in.

## Audio processing

For each host block (`ModdMuddProcessor::processBlock`):

1. Take the newest `RtState`. If it is new, restore parameters that are no longer modulated to their base values.
2. Read ModdMudd's own parameters into `EngineSettings`.
3. MIDI: learned CCs are removed from the stream and applied to ModdMudd parameters (via a FIFO to the message thread). Everything else passes to the hosted plug-ins.
4. DAW automation of the remappable slots sets base values.
5. Split the block into **sub-blocks** (default 32 samples). For each:
   - feed envelope followers listening to the input or sidechain
   - run every modulator once, in dependency order
   - compute each modulated destination, smooth it, and write it to the hosted parameter if it changed
   - process each hosted plug-in for the sub-block, with a play head offset to the sub-block's position
   - feed envelope followers listening to the output
6. Apply the output gain.

### Why sub-blocks

JUCE's VST3 host passes parameter changes to a hosted plug-in once per `process()` call, at sample offset 0 (`juce_VST3PluginFormatImpl.h`: `inputParameterChanges->set (id, value, 0)`). JUCE 9.0.6 has no sample-accurate automation path for hosted plug-ins. Calling the hosted plug-in in short sub-blocks gives modulation a time resolution of the sub-block size instead of the host's buffer size. The hosting test checks this: 16 parameter steps inside each 512-sample host block.

The cost is more `process()` calls per block. The resolution is a user setting (16, 32, 64, 128 samples, or once per host block).

## Parameters: base value and modulated value

Each hosted parameter has a `ParamState` (`Source/Engine/ModTypes.h`):

- `base`: the user's value, without modulation
- `display`: the last value ModdMudd wrote
- `gesture`: the user is holding the control
- `driven`: the parameter is currently modulated or blended

The destination value is `clamp (base + Σ (depth × source + offset), rangeMin, rangeMax)`, then smoothed per destination.

**Echo filtering.** When ModdMudd writes a modulated value, the hosted plug-in may report it back. If that report were taken as a user edit, the base value would drift towards the modulation. The rule (`HostedSlot::parameterValueChanged`): while a parameter is driven, value reports that are not part of a user gesture (begin/end edit) are ignored. While the user holds a control, modulation of that parameter pauses and the control follows the user's hand.

**Consequence:** a plug-in whose editor changes values without sending begin/end gestures can't change the base value of a *modulated* parameter from its own window. ModdMudd's parameter list always can.

## DAW-visible parameters

VST3 expects a plug-in's parameter list to be fixed when it loads, but hosted plug-ins come and go. ModdMudd exposes:

- its own controls: macros, XY, LFO, randomizer, envelope and blender settings, output gain
- 256 **remappable slots** (`RemappableParameter`). When a plug-in is loaded, its automatable parameters are assigned to free slots. The slot names follow the hosted parameters, and ModdMudd tells the host to re-read parameter info (`updateHostDisplay (withParameterInfoChanged)`).

Whether the new names appear depends on the DAW re-reading parameter info. JUCE's own VST3 host does (verified by the hosting test). Each DAW still needs checking; see `COMPATIBILITY.md`.

JUCE also adds 2048 hidden parameters to plug-ins that take MIDI input. They emulate MIDI CC, because VST3 hosts deliver CCs to plug-ins as parameters. They are what make MIDI learn and CC pass-through (mod wheel, sustain) work.

## Modulators

All produce one value per sub-block. All of them always run on the audio thread (they are cheap). The chain in the editor only decides which ones the user sees and can connect. Bipolar sources give -1..1, unipolar 0..1. Connections can remap the range (natural, unipolar or bipolar polarity).

| Source | Output | Notes |
|---|---|---|
| LFO ×4 | bipolar | sine, triangle, square, saw, reverse saw, custom (up to 64 points with curved segments); free Hz or tempo divisions; phase, invert, depth, offset, fade-in; retrigger free, on transport start or on note-on. Synced LFOs lock to the song position. |
| Random ×2 | bipolar | smooth (cosine) or stepped with glide; probability; seeded. Values are a pure function of seed, cycle and lane, so a synced randomizer repeats exactly with the song position. 8 independent lanes give each destination its own stream ("Independent"). |
| Macro ×8 | unipolar | DAW-automatable, MIDI-learnable, lightly smoothed. |
| XY pad | X and Y | glide; configurable centre; output relative to the centre (bipolar) or 0..1. |
| Envelope ×2 | unipolar | input, output or sidechain; peak or RMS; attack, release, gain, threshold, sensitivity (6–60 dB range), response curve, smoothing, invert. |

The **preset blender** stores up to four snapshots (A–D) of every modulatable hosted parameter's base value. The blend position (X/Y) weights the corners bilinearly. Continuous parameters interpolate. Discrete parameters (switches, menus) switch to the nearest snapshot that holds them. Only parameters whose captured values differ are driven.

## Cross-modulation

Modulator settings are destinations too: LFO rate, depth and phase; random rate and depth; envelope gain and depth; macro amount; XY X/Y; blend X/Y. `ModMatrix` treats modulators as nodes in a graph. It refuses any connection that would create a loop (including self-modulation), and it computes an evaluation order (Kahn's algorithm) so each modulator runs after the ones that drive it. There is no feedback, so no instability.

Rate cross-modulation works in the knob's normalised range in free mode, and as ±4 octaves of the tempo rate in sync mode. While a synced LFO's rate is cross-modulated it free-runs, because it can't be locked to the grid.

## Plug-in scanning

`PluginCatalog` is shared by all ModdMudd instances in a process (`SharedResourcePointer`). Scanning runs on the message thread, one file per timer tick, so the DAW stays responsive between plug-ins. JUCE's dead-man's-pedal file (`scan-in-progress.txt`) records the plug-in being scanned. If it crashes the DAW, it is skipped on the next scan. ModdMudd never lists itself, so it can't recurse.

Data files:

| Platform | Location |
|---|---|
| macOS | `~/Library/Application Support/ModdMudd/plugins.xml` |
| Linux | `~/.config/ModdMudd/plugins.xml` |

## State

ModdMudd's state is a ValueTree saved as XML (`createStateTree` / `restoreStateTree`). It holds:

- ModdMudd's parameters
- the chain: for each slot, the plug-in description, the plug-in's own state (base64), and base values of modulated parameters
- the matrix and per-destination settings
- blender snapshots, custom waveforms, random seeds, the DAW-slot map, MIDI CC assignments and settings

Hosted plug-ins are matched by VST3 class ID first, then name and manufacturer, then file path. This lets sessions and presets move between machines and between macOS and Linux, if the vendor uses the same class ID on both. A plug-in that isn't installed becomes a **missing** slot: audio passes through, and its saved data is kept so re-saving doesn't lose it.

Modulated parameters store their base values separately, because the hosted plug-in's own saved state holds whatever value ModdMudd last wrote.

Hosted plug-ins must be created on the message thread. If the DAW restores state on another thread, ModdMudd defers the restore to the message thread.

Presets are the same XML in `.mmpreset` files (`~/Documents/ModdMudd/Presets`).

## Editor layout

The modulation panel snaps onto the hosted plug-in, so the two read as one window:

- The hosted plug-in's own window sits flush at the top, with no frame or top bar.
- ModdMudd's panel sits directly underneath at the same width. Its header strip holds the plug-in menu (switch, load, replace, reorder, remove), the view toggle, presets and settings. Below that are the modulation chain, the selected modulator's controls, and a status line.
- The window is exactly `plug-in height + panelHeight` tall and as wide as the plug-in. The exception is a plug-in narrower than the panel's minimum (660 px): it is centred above the panel instead.
- If the plug-in's window can be resized, resizing ModdMudd's window resizes the plug-in (within its own limits), and the panel follows. If it can't, ModdMudd's window takes the plug-in's fixed size.
- With ModdMudd's parameter list showing instead, the window resizes freely.
- **The modulation chain.** You add only the modulators you want ("+ Add"), drag their tabs to reorder them, and remove them with the x on the tab (or right-click, or Delete). Removing a modulator also removes its connections. Connecting a modulator that isn't in the chain, such as "Assign to macro" from the parameter list, adds it automatically. The chain and its order are saved with the session. Older sessions show every modulator that is in use.
- **Routing.** The matrix, with every connection in one list, sits behind the Routing button, so the main view is just the plug-in and the chain.
- Panels adapt to the width. Rows shrink towards minimum sizes, labels drop out before controls do, and the matrix hides its offset, range and smoothing columns below 820 px.

**One UI timer.** The editor runs a single 30 Hz timer. It updates the status line and calls `refresh()` on every visible `Refreshable` (source chips, waveform, history and meter displays, pads, the parameter list). Hidden panels do no work. This replaced about twenty per-widget timers. Besides the CPU saving, it fixed an intermittent crash on Linux: with many timers, events were still in flight when the editor closed, and the host's VST3 run loop could call ModdMudd's already-removed event handler. Running pluginval with editor tests reproduced this in about 1 run in 5, and it was gone in 28 runs after the change.

`ModdMuddUISnapshot` checks the snapping: the window width and height against the hosted plug-in's window.

## Testing

- `ModdMuddTests`: engine unit tests (LFO shapes, sync and fade, custom tables, randomizer determinism and limits, envelope timing and mapping, matrix cycle detection and serialisation, polarity and range maths, blender interpolation, RtSwap).
- `ModdMuddHostTest`: a headless VST3 host loads **ModdMudd FX and Instrument as real VST3s**. ModdMudd then loads two small test VST3s, and the test checks:
  - pass-through and hosted processing
  - LFO modulation across the full range, with sub-block resolution
  - state round trip into a fresh instance
  - parameter renaming
  - base-value restore when modulation is removed
  - missing-plug-in handling
  - MIDI to a hosted instrument
- pluginval in CI (strictness 8) on both builds.
- `ModdMuddUISnapshot`: development tool that renders the editor to PNG.
