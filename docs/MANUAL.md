# Indexed Quad Sequencer & Sequencer Controller — User Manual

Two VCV Rack 2 modules by Jin Ng:

- **Indexed Quad Sequencer** (26HP) — a four-track, clock-driven step sequencer in which every step stores *indices* into voltage tables rather than voltages. Inspired by the Orthogonal Devices ER-101.
- **Sequencer Controller** (14HP) — an expander that adds parts, groups with a CV modulation bus, recording, and extended storage. Inspired by the Orthogonal Devices ER-102.

These modules are independent re-creations of the hardware's published behaviour. They are not affiliated with or endorsed by Orthogonal Devices.

---

## Contents

1. [Basics](#1-basics)
2. [Quick start](#2-quick-start)
3. [The sequencer panel](#3-the-sequencer-panel)
4. [Editing](#4-editing)
5. [Timing: duration, gate, ratchet, smoothing, triggers, clock division](#5-timing)
6. [Voltage tables](#6-voltage-tables)
7. [Modes: EDIT, FOLLOW, HOLD](#7-modes-edit-follow-hold)
8. [MATH](#8-math)
9. [Snapshots](#9-snapshots)
10. [Reset](#10-reset)
11. [The Sequencer Controller expander](#11-the-sequencer-controller-expander)
12. [Parts](#12-parts)
13. [Groups and the modulation bus](#13-groups-and-the-modulation-bus)
14. [Recording](#14-recording)
15. [Storage and MIDI import](#15-storage-and-midi-import)
16. [Context menu, undo, saving](#16-context-menu-undo-saving)
17. [Differences from the hardware](#17-differences-from-the-hardware)

---

## 1. Basics

- **Four tracks**, each with three outputs: **CV-A**, **CV-B** (0–8.192V) and **GATE** (0/10V).
- A track is a list of **steps** divided into **patterns** (up to 100 patterns per track, 100 steps per pattern, 2000 steps in total across all tracks).
- Each step has four values from 0 to 99:
  - **CV-A**, **CV-B** — indices into the track's voltage tables A and B;
  - **DURATION** — the step's length in clock pulses (0 skips the step);
  - **GATE** — how many of those pulses the gate stays high (0 is a rest, ≥ DURATION is legato).
- Each track has its own two **voltage tables** of 100 entries. By default they are 12-tone equal temperament: index 12 = 1V, 24 = 2V, and so on.
- The sequencer has no internal clock. Patch a clock into **CLOCK** (rising edges above about 2V); 4 pulses per beat is a good start.

The panel has two encoders. The **left** one changes whatever is focused in the left display column (INDEX, TRACK, PATTERN, STEP, SNAPSHOT). The **right** one changes the right column (VOLTAGE, CV-A, CV-B, DURATION, GATE). Press a display's grey **focus button** to focus it; its LED lights. Drag an encoder up/down (or scroll over it) to turn it.

A **focus press** means pressing the focus button of a display that is already focused. It acts when the button is released, and only if nothing else happened while it was held, so focus buttons can also be held as modifiers (for example holding STEP while pressing RESET).

## 2. Quick start

1. Patch a clock into CLOCK, and TRACK 1 **CV-A** into an oscillator's V/Oct and **GATE** into an envelope.
2. Make sure MODE is on **edit** and TABLE is on **A**. If PAUSE is lit, press PAUSE.
3. Focus **STEP** and press **INSERT**: a step appears (CV-A 12, DURATION 0).
4. Focus **DURATION** and turn the right encoder to 4; focus **GATE** and turn it to 2. You now hear a note on every beat.
5. Press **INSERT** again: a copy of the step is added after it. Focus **CV-A** and turn it to 24: the two notes are now an octave apart.
6. Right-click the module and choose **Load example sequence** for a four-track example.

## 3. The sequencer panel

| Control | Purpose |
|---|---|
| TABLE switch (A / ref / B) | Which table INDEX and VOLTAGE show and edit; *ref* browses the reference tables. Also picks which output (A or B) SMOOTH acts on. |
| SMOOTH | Toggles smoothing (glide) on the focused step, pattern or track. |
| INDEX / VOLTAGE | Voltage table entry and its voltage (see §6). VOLTAGE also shows messages. |
| TRACK / PATTERN / STEP | Navigation. PATTERN jumps to a pattern's first step going forward, its last going backward. |
| SNAPSHOT | Selects a snapshot slot (see §9). |
| CV-A / CV-B / DURATION / GATE | The focused step's values. |
| INSERT, DELETE, COPY | Editing (see §4). |
| MATH | Transforms (see §8). |
| LOAD, SAVE | Snapshots (see §9). |
| LOOP START / END | Loop points (see §4). |
| PAUSE | Stops the clock from advancing the tracks; gates close. |
| RESET button and jack | Back to the start (see §10). |
| COMMIT | HOLD mode (see §7). |
| MODE switch (hold / edit / follow) | See §7. |

Messages on the VOLTAGE display: `AFtr` / `SPLt` / `bEFr` (insert mode), `CLr` (confirm clearing a track), `Abrt` (confirm load/save; also "aborted"), `FULL` (step limit reached), `TILt` (edit refused in FOLLOW), `PIN` / `dOnE` (MATH screen), `PLUG` (part selection is under CV), `Err` (built-in tables are read-only).

## 4. Editing

**Moving around.** Focus TRACK and turn left to pick a track; focus STEP to move through steps (the PATTERN display follows); focus PATTERN to jump by pattern.

**Changing a step.** With the cursor on a step, focus CV-A, CV-B, DURATION or GATE and turn the right encoder.

**Swing.** Hold the DURATION button while turning the right encoder: pulses move between this step and the next, so their total length (and everything after them) stays in time.

**INSERT.**
- With STEP focused, INSERT adds a copy of the current step after it (a step with default values if the pattern is empty). With PATTERN focused it adds an empty pattern.
- Hold INSERT and turn the right encoder counter-clockwise to choose **AFtr** (after, the default), **SPLt** (split the step in two, keeping its total duration; or split the pattern at the cursor) or **bEFr** (before). The insertion happens when you release INSERT.

**DELETE.** Deletes the current step (STEP focused) or pattern (PATTERN focused). With TRACK focused, DELETE shows `CLr`; press DELETE again to clear the whole track, or any other button to abort.

**COPY and paste.**
- COPY copies the current step, pattern or (TRACK focused) whole track; the COPY LED lights and the matching focus LED blinks. INSERT then pastes instead of inserting (after, or before with `bEFr`). Press COPY again to empty the clipboard.
- To copy a range, hold COPY and move the cursor with the left encoder, then release COPY.
- With TRACK focused, INSERT pastes a copied track over the selected one.

**Loop points.** LOOP START / LOOP END set the loop at the cursor's step; pressing again on the same step clears it (so pressing twice anywhere clears it). With PATTERN focused, the buttons use the pattern's first/last step, so pressing both loops the whole pattern. The LEDs show whether the cursor's step is a loop point (with PATTERN focused: lit for the pattern's boundary, blinking for a point inside it).

**Ratchet.** A focus press on GATE toggles ratchet on the current step (both decimal points on the GATE display light).

**Track options.** A focus press on TRACK opens the track's options; another focus press closes it:

| Display | Option |
|---|---|
| CV-A, CV-B | `Nt` show voltages as notes, `Nr` as numbers (press the focus button to switch) |
| DURATION | clock divider, 1–99 (right encoder) |
| STEP | clock multiplier, 1–99 (left encoder) |
| GATE | `Gt` gate outputs, `tr` trigger outputs (press the focus button) |

## 5. Timing

- A step lasts DURATION pulses; its gate is high for the first GATE of them. Steps with DURATION 0 are skipped.
- **Ratchet**: the gate repeats GATE pulses high, GATE pulses low until the step ends.
- **Trigger mode** (track option `tr`): the gate output fires a trigger of GATE × 0.1ms at each step. With ratchet on, GATE is instead the number of 0.5ms triggers, spread evenly over the step.
- **Smoothing**: once the gate falls, the CV glides linearly to the next step's value, arriving as that step begins (in trigger mode, over the second half of the step). The glide follows the clock's tempo. Smoothing can be set per step, per pattern or per track, separately for A and B: set TABLE to A or B, focus STEP, PATTERN or TRACK and press SMOOTH. The SMOOTH LED shows whether the cursor's step is smoothed by any of the three.
  *Tip*: two steps, CV 0 and 96, DURATION 16, GATE 0, both smoothed, make a triangle LFO.
- **Clock division and multiplication** per track (track options). The multiplier is phase-locked to the incoming clock and follows its tempo one period behind.

## 6. Voltage tables

- **Browsing**: focus INDEX and turn left; VOLTAGE shows that entry's voltage. When INDEX is not focused, INDEX/VOLTAGE show the entry used by the cursor's step.
- **Editing**: focus VOLTAGE and turn right. This changes the table entry, so every step using that index follows (handy for re-tuning a note throughout a track, or for "global" controls such as accent levels). A focus press on VOLTAGE (or holding VOLTAGE while turning) cycles the step size; the lower VOLTAGE LED shows it:

  | LED | Number display | Note display |
  |---|---|---|
  | off: fine | 2mV | 1% of a whole tone |
  | on: coarse | 100mV | to the next semitone |
  | blinking: super coarse | 1V | an octave |

- **Note display** reads `octave.note.percent-of-a-whole-tone`, e.g. `2.C.00` = 2V, `3.A.50` = A♯3.
- **Reference tables** (TABLE on *ref*; choose with the right encoder while INDEX or VOLTAGE is focused): `12Et`, `24Et`, `22Jt` (shrutis), `bLUE`, `PEnt` (major then minor pentatonic), `L-8` (linear 0–8V), `E-8` (exponential), `LE-8`, and eight user tables `USr1`–`USr8`. The user tables are shared by every instance and saved in Rack's settings.
- **Copying a table**: focus INDEX, set TABLE to the source and press COPY; set TABLE (or the reference table) to the destination and press INSERT; press COPY to finish. Built-in tables can't be overwritten.

## 7. Modes: EDIT, FOLLOW, HOLD

- **EDIT**: the displays follow the edit cursor; edits are heard the next time playback reaches them.
- **FOLLOW**: the displays follow the play cursor of the selected track; turning the left encoder on STEP/PATTERN moves (scrubs) playback. Edits that change steps are refused (`TILt`) unless PAUSE is on; loops, smoothing, tables and MATH still work.
- **HOLD**: you edit a copy; playback carries on with the original. **COMMIT** writes the copy back:
  - with TRACK, PATTERN or STEP focused, at the end of the selected track's current track/pattern/step (the COMMIT LED blinks meanwhile);
  - at once if you press COMMIT twice, or with INDEX or SNAPSHOT focused.
  Leaving HOLD without committing discards the copy. Loading a snapshot in HOLD loads it into the copy, ready to be cued with COMMIT.

## 8. MATH

Each track keeps a prepared transform with one operation per step parameter. Hold **MATH** to see and edit it: the left column shows each parameter's operation (TRACK row = CV-A, PATTERN = CV-B, STEP = DURATION, SNAPSHOT = GATE), the right column its operand.

- Press a left focus button to choose that row and cycle its operation: `A` add (`-A` subtract), `G` multiply (`-G` divide), `S` set, `rd` random 0..N, `Jt` jitter ±N. Turn the right encoder to set the focused parameter's operand.
- Release MATH to apply it to the focused step, pattern or track. A result outside 0–99 leaves that value unchanged.
- While holding MATH, press VOLTAGE to pin the screen (`PIN`); then MATH applies and VOLTAGE (`dOnE`) leaves. DELETE resets the transform.

With the Sequencer Controller attached, MATH becomes the five-operation transform of §11.

## 9. Snapshots

A snapshot holds everything about all four tracks (steps, patterns, loops, tables, options, transforms, parts and groups). Focus SNAPSHOT and turn left to choose a slot (`--` is the empty snapshot). Press **SAVE** or **LOAD**; `Abrt` blinks; press the same button again to confirm, or any other button to cancel. Loading rewinds every cursor. Snapshots are saved in the patch.

Without the expander there are 16 slots; with it, 127 (§15).

## 10. Reset

- A rising edge on **RESET** (jack or button) sends every track back to its first step, ignoring loops. While RESET is held high, the clock is ignored.
- By default the next clock then plays the first step. *Reset behaviour* in the context menu can make the reset itself start the first step instead. Clock and reset edges within 1ms of each other count as one event, so patches that derive both from one clock start cleanly on step 1.
- **Quantized reset**: hold TRACK, PATTERN or STEP and press the RESET button; the reset happens when the selected track reaches the end of its current track, pattern or step.

## 11. The Sequencer Controller expander

Place the Sequencer Controller directly to the **right** of the sequencer. It has no knobs of its own: it uses the sequencer's encoders and its INDEX/VOLTAGE displays. On its own, its STORAGE *error* LED lights.

**Five-operation MATH.** With the expander attached, each parameter's transform has five operations applied together:
`result = Qt( G × (Rd > 0 ? random(0..Rd) : value) + jitter(±Jt) + A )`

| Code | Operation | Range |
|---|---|---|
| `A` / `S` | add (set, when G = 0) | −99..99 |
| `G` | multiply; `-G` divide | ÷99 .. ×99, and 0 |
| `Jt` | jitter | 0..99 |
| `rd` | random | 0..99 |
| `Qt` | round to a multiple of | 1..99 |

In the MATH screen the right focus buttons (or the left rows) pick the parameter, the **left** encoder picks the operation, and the right encoder sets its value. **INVERT** in the screen inverts the transform (A and G); holding INVERT while pressing and releasing MATH applies the inverse.

**Rotoinversion.** With PATTERN or TRACK focused, **INVERT** reverses the order of the focused right-hand parameter (CV-A, CV-B, DURATION or GATE) across those steps, **ROTATE** shifts it one step later, and INVERT+ROTATE shifts it earlier. The other parameters stay put, so you can rotate a rhythm under a melody.

## 12. Parts

A part stores, for each track, a **RESET TO** step and a **loop**. There are 99 parts per snapshot plus the built-in **STOP** part 0 (silence).

- Focus **PART** and turn left to choose the focused part. With the expander attached, LOOP START/END and the **RESET TO** button edit the focused part's points on the selected track. Editing the playing part is heard at once. The first time you attach the expander, the loops you already had become part 1.
- **TRANSITION** (or a rising edge at **ACTIVATE**) makes the focused part *pending* (the PART LED blinks; with PART focused, INDEX shows it). The TRANSITION switch decides when it takes over:
  - **first**: as soon as any track finishes its loop;
  - **last**: once every track has finished at least one loop;
  - **user**: immediately, without resetting.
  Tracks with a RESET TO step jump to it; others play on from where they are. Leaving STOP is always immediate.
- **SELECT** picks the focused part by CV (0.1V per part). While ACTIVATE is held high, the pending part follows SELECT, so you can play parts live.
- With parts in use, **RESET** goes to the playing part's RESET TO steps.
- The PART display shows a dot when the focused part is the one playing. With PART focused, VOLTAGE shows an overview (per track: top bar RESET TO, middle LOOP START, bottom LOOP END). Hold PART and turn left to jump between first step, RESET TO, LOOP START, LOOP END and last step. COPY/INSERT/DELETE copy, paste and clear parts.

## 13. Groups and the modulation bus

A group is any selection of steps; there are 16 per snapshot.

**Selecting.** Focus **GROUP** and turn left to choose a group. **(DE)SELECT** adds or removes the cursor's step (the red LED left of GROUP shows membership; the GROUP display's dot means the group has members).

**Euclidean selection.** With PATTERN or TRACK focused, (DE)SELECT starts choosing a mask E(N, M): INDEX shows N, VOLTAGE shows `Eu.M`. Turn left for N, right for M (up to 99). DELETE sets N to 0; INDEX flips between none and all. Press (DE)SELECT again to apply: the mask repeats over the pattern (or track); steps on a 1 join the group, steps on a 0 leave it.

**With GROUP focused** (selection operations act on the selected track only): COPY/INSERT copy a selection and add it to another group, DELETE empties the group, INVERT inverts the selection, ROTATE shifts it (INVERT+ROTATE the other way). INDEX shows `nS` and VOLTAGE the number of members on this track.

**Group MATH.** With GROUP focused, MATH edits and applies the group's own transform to its members on every track.

**The modulation bus.** The expander's three channels **X, Y, Z** each have a CV and a gate input. For every group and channel:
- a **slope** per parameter adds `slope × CV` to member steps (to CV-A/CV-B in volts, continuously; to DURATION/GATE in pulses, when the step starts);
- a **HIGH** and a **LOW** transform (non-destructive, five operations) are applied each time a member step plays, HIGH while that channel's gate is above 1.5V, LOW otherwise (and when unpatched). Random and jitter are drawn anew every time. A step modulated to DURATION 0 is skipped.

To edit them, set the GROUP MODIFIERS switches (high / slope / low, and X / Y / Z) and press the **GROUP MODIFIERS** button (press again to leave). The right focus buttons pick the parameter.
- *slope*: the left encoder picks the group, the right encoder the slope (in 0.01 steps below 1, 0.1 to 10, then 1). VOLTAGE shows the focused slope exactly; the right column shows short forms.
- *high/low*: the transform screen, as in MATH (left encoder: operation; right: value).

## 14. Recording

**ARM** arms the selected track; **PUNCH IN/OUT** (button, or a gate at the PUNCH jack) starts and stops; the **REC** LED shows recording. Only patched inputs are used. RESET, PAUSE and changing the RECORD MODE end a recording, and a recording can be undone with Ctrl+Z.

- **alter**: as each step of an armed track starts playing, it is rewritten from the inputs: **A-1** → CV-A, **A-2** → CV-B (quantized to the track's tables), **AD-1** → GATE, **AD-2** → DURATION (volts × 20, up to 99). A single looped step follows the inputs continuously.
- **step**: a trigger at **D-1** inserts a step after the edit cursor of every armed track, taking the inputs' values (and following them while D-1 stays high); **D-2** deletes the step at the cursor.
- **real-time**: arming opens a configuration screen (press ARM again to close it):

  | Display | Setting |
  |---|---|
  | CV-A, CV-B | `tr`: a change of this CV (after quantizing) starts a new step, for legato playing; `--`: it doesn't |
  | DURATION, GATE | quantization grid in pulses (focus, then right encoder) |
  | TRACK / PATTERN / STEP (blinking) | where takes go: a new pattern at the end of the track, a new pattern after the playing one, or after the playing step |

  Punch in, then play: each note on **AD-1** (with pitch on **A-1**, velocity or anything else on **A-2**) becomes a step, timed against the track's own clock and rounded to the grids. Recording waits for the first note. While armed, the patched inputs pass straight to the track's outputs, both while recording and when you play before punching in, so you can rehearse.

## 15. Storage and MIDI import

**127 snapshot slots** with the expander attached, in this order: templates `t1`–`t9`, the empty `--`, and regular slots `A1`–`q9` (letters A b C d E F G H J L n P q, digits 1–9). The sequencer's own slots 1–16 are the same as A1–b7, so nothing saved without the expander is lost. The STORAGE switch and its LEDs are decorative.

**Import MIDI file…** (context menu, with or without the expander) reads Standard MIDI Files of type 0 and 1. MIDI channels 1–4 go to tracks 1–4 and replace the whole sequence (undoable).
- Each channel is played monophonically: a new note ends the previous one.
- Time is counted at 4 pulses per quarter note. Each note becomes a step lasting until the next note, with the note's length as GATE. Gaps become rests, and very long notes are split into tied steps.
- CV-A is the note number minus 36 (C4 → 24 → 2V), CV-B the velocity scaled to 0–8V. A new pattern starts every 16 steps.

## 16. Context menu, undo, saving

- **Reset behaviour**: whether the clock after a reset plays the first step (default) or the reset starts it (§10).
- **Import MIDI file…** (§15) and **Load example sequence**.
- Rack's **Initialize** clears everything, snapshots included.
- **Ctrl+Z / Ctrl+Shift+Z** undo and redo edits. A burst of activity (for example one knob gesture) becomes one undo step; moving the cursor or changing focus records nothing. In HOLD mode undo applies to the copy being edited. Snapshot saves and user reference tables are not part of undo.
- Everything except the user reference tables is saved in the patch.

## 17. Differences from the hardware

Where the manuals were silent or ambiguous, these modules make a choice. The full list, with reasons, is in [SPEC.md](SPEC.md) and [SPEC-ER102.md](SPEC-ER102.md). The ones you are most likely to notice:

- A new step copies the step under the cursor (the manual's Quick Start does this; its reference section says new steps have DURATION 0).
- Deleting a step that carries a loop or part point clears that point.
- Smoothing glides to the literal next step, including a zero-length one (needed for the manual's sawtooth LFO).
- The table of built-in reference scales 22JT, BLUE, PEnt, E-8 and LE-8 is reconstructed from their descriptions; the exact hardware values were not published.
- Two of the manual's nine Euclidean examples come out as rotations of the printed pattern (same rhythm, different starting step; use ROTATE).
- On the slope screen VOLTAGE shows the slope itself rather than slope × input voltage.
- Recording uses only patched inputs; an unpatched input leaves its parameter alone.
- Snapshot revisions and descriptions, the configuration file, the advanced MIDI import and firmware functions are not implemented. Saving a snapshot does not pause the module.
