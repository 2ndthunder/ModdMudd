# ModdMudd tutorial

This walks you through a first session: load a plug-in, make it move, and save the result. It takes about ten minutes. ModdMudd also has a guided tour inside the plug-in: click **Tutorial** at the top right of ModdMudd's panel (or Settings > Show tutorial). It opens by itself the first time you use ModdMudd.

## Before you start

- Install the plug-ins (see [BUILDING.md](BUILDING.md)). There are two:
  - **ModdMudd FX**: goes on an audio or effect track and hosts effects.
  - **ModdMudd Instrument**: goes on an instrument track and hosts an instrument, optionally followed by effects.
- Your DAW must load VST3 plug-ins. Logic Pro, GarageBand and Pro Tools can't.

## 1. Load a plug-in

1. Insert **ModdMudd FX** on a track that has audio playing, such as a drum loop or a pad.
2. Click the plug-in button at the left of ModdMudd's panel (it says "No plug-in loaded") and choose an effect. A filter, EQ, delay or saturator is a good first choice.
   - If the list is empty, choose **Settings > Scan for plug-ins** and wait for the scan to finish. A plug-in that crashes during the scan is skipped next time.
3. The effect's own window appears at the top, with ModdMudd's panel snapped underneath at the same width.

**Parameter list.** The **Parameters** button switches to ModdMudd's list of the effect's controls. The list works with every plug-in, has a search box, and is the easiest place to drag modulators onto. **Plug-in UI** switches back.

## 2. Make something move with an LFO

A new ModdMudd starts with one modulator, **LFO 1**, selected in the chain row.

1. Find the **LFO 1** handle at the bottom left of the LFO panel: the bar with a crosshair icon and a moving value line.
2. Connect it to a control in either of two ways:
   - **Drag** the handle onto a parameter in the Parameters list (for example the filter cutoff).
   - **Click** the handle (it says "Move a control..."), then move the control you want in the plug-in's own window.
3. Press play. The parameter now moves with the LFO.
   - In the Parameters list the row shows an **L1** tag, a band showing how far the LFO can move it, and a marker following the live value.
4. The handle now shows **1**, the number of targets. Click the number to change how strongly the LFO moves the parameter (depth), or to remove the connection.

**Change the movement:**
- **Rate** sets the speed. Turn on **Sync** to lock the LFO to your song's tempo and pick a note length instead.
- **Shape** changes the waveform. Choose **Custom** to draw your own: drag points, double-click to add or remove one, and Alt-drag between points to bend a segment.
- **Depth**, **Offset**, **Phase**, **Fade In** and **Retrigger** (free, on transport start, or on each note) shape it further.

## 3. Add more modulators

Click **+ Add** in the chain row:

| Modulator | What it does | Try it on |
|---|---|---|
| LFO | Repeating movement | Filter sweeps, tremolo, vibrato |
| Random | Seeded random movement, smooth or stepped | Subtle drift, sample-and-hold |
| Macros | Eight knobs you turn (or automate, or MIDI-learn) | One knob that changes many things |
| XY Pad | Two values from one pad | Performing two parameters at once |
| Preset Blender | Morph between four snapshots of the plug-in | Morphing between sounds |
| Follower | Follows the loudness of audio | Ducking, effects that react to the beat |
| Shaper | A drawn envelope played on a trigger | Plucks, swells, rhythmic gates |

Tabs can be dragged into any order. The x on a tab removes the modulator, along with its connections. Each modulator has its own colour, used on its tab, its handle, and the tags on the parameters it moves.

## 4. One knob for many things (Macros)

1. Add **Macros**.
2. In the Parameters list, right-click a parameter and choose **Assign to macro > Macro 1**. Do this for two or three parameters.
3. Turn **Macro 1**. Every assigned parameter moves together.
4. Right-click the knob and choose **MIDI Learn**, then move a knob on your controller to control it from hardware.

Macros are automatable in your DAW like any parameter.

## 5. Make it react to the music

**Follower** follows audio loudness:

1. Add **Follower 1**.
2. Set **Listen to** to **Input**. For the Instrument version, use **Output**. To use another track, choose **Sidechain** and route that track to ModdMudd's sidechain input in your DAW.
3. Connect its handle to a parameter.
4. Adjust **Threshold** and **Sensitivity** until the meter moves with the music. **Attack** and **Release** set how fast it follows.

**Shaper** plays a drawn envelope each time something happens:

1. Add **Shaper 1**.
2. Set **Play on** to:
   - **Note**: a MIDI note-on.
   - **Audio**: a hit above the threshold, such as a kick drum.
   - **Transport**: when playback starts.
   - **Loop**: repeats continuously.
3. Draw the curve and set **Time**, or turn on **Sync** and choose a note length.
4. Click **Play** to hear it without a trigger.

## 6. Morph between sounds (Preset Blender)

1. Add **Preset Blender**.
2. Set up the plug-in the way you like and click **A**. Change the plug-in's settings and click **B**. Do the same for C and D if you like.
   - Or right-click a letter and choose **Capture from plug-in preset** to use one of the plug-in's own factory presets.
3. Turn on **Blend on** and move the pad. A is the top left corner, B top right, C bottom left and D bottom right.
   - Continuous controls glide between snapshots. Switches and menus jump to the nearest one.
4. Connect an LFO or Macro to **Blend X** or **Blend Y** (drag its handle onto those knobs) for automatic morphing.

## 7. Modulate the modulators

Drag any handle onto a modulator's own knobs:
- Random 1 onto **LFO 1 Rate**, for a wandering speed.
- Follower 1 onto **LFO 1 Depth**, so the wobble gets deeper when the music gets louder.

The knob shows a blue ring for the amount. ModdMudd refuses connections that would create a loop.

## 8. See everything: Routing

**Routing** lists every connection. For each one you can set:
- **Depth**: how much, and in which direction.
- **Offset**: a fixed amount added.
- **Polarity**: the source's own range, 0..1, or -1..1.
- **Min / Max**: the range the parameter may move in.
- **Smooth**: how gently it moves.

Untick a connection to switch it off without deleting it. In a narrow window, Min, Max, Offset and Smooth are hidden; make the window wider to see them.

## 9. Save it

- Your DAW project saves everything automatically: the hosted plug-in, its settings, your modulators and connections.
- **Presets > Save preset...** saves the whole setup as a `.mmpreset` file to use in other projects. If a preset uses a plug-in that isn't installed, ModdMudd keeps its place and settings and passes audio through until it is.

## Tips

- **Hearing steps?** Increase **Smooth** for that connection in Routing.
- **Fast modulation sounds stepped?** **Settings > Modulation resolution** sets how often ModdMudd updates the plug-in (32 samples by default). Smaller is smoother but uses more CPU.
- **A control in the plug-in's window won't move when modulated?** Use the Parameters list. A few plug-ins don't report when you touch their controls.
- **Hard to read?** **Settings > Theme > High Contrast**.
