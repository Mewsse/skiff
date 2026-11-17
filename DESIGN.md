# Design

How this firmware looks and behaves. Every screen and feature follows this page. If something here is wrong, change this page first, then the code.

Each rule comes from what FM-1 owners said about the stock firmware, Felucca and the other community firmwares, in GitHub issues and Discussions, forums and reviews. The issue numbers in brackets are Felucca's unless marked.

## Rules

1. **The K1–K4 legend is one row of four, in knob order, at the top of the screen,** because the physical knobs sit top right. Never a 2×2 grid.
2. **Every screen labels every live control:** the four knobs on its cards, the page as n/m, and (U5) what every encoder, button and key does here on SHIFT's map (HOME held). An unused knob is an empty tile (U4: one empty look; "--" means nothing picked), a value only read has no gauge and is MID. (D#80)
3. **Encoders have one job everywhere:**

   | Encoder | Job |
   |---|---|
   | ALGORITHM | Track |
   | PRESETS | Sound, the step cursor on SEQ pages, or the list selection (DRUM-X's SOUND sheet: the drum sound; FM6's OPERATOR and ALL OPS: the operator) |
   | SELECT | The page before or after in the family |

   Tempo changes only while GLO is held, so it can't be bumped by accident. (#58, D#84, D#128)
   - A new unit starts with BPM LOCK on, and (U1) a unit coming from Felucca gets it turned on once at its first boot (`main.c`, `ui_set_rev`); the MENU can turn it off again.
   - **SELECT turns pages** (phase 8; `ui.c` `page_step`, `ui_input.c` `select_turn`): either way, a detent a page, in the order the family's button steps them, stopping at the first and the last (no wrap). GLO's MIXER, DELAY, REVERB, CHORUS and GLOBAL are one family (U3). The button still steps forward from wherever SELECT left you. On a sheet (DRUM-X's SOUND, FM6's OPERATOR and ALL OPS; `ui.c` `sheet_t`) SELECT moves the knobs down the sheet first, then on to the page beside it.
   - On HOME (no pages) SELECT still says BPM LOCKED; with a layer held it does nothing to the page under it. With BPM LOCK OFF, SELECT is the tempo everywhere, Felucca's way, except on the sheets, where nothing else reaches their rows (U1).
4. **There is one modifier model: layers.** A button held for longer than the hold time (MENU > HOLD, every button's) shows its map, and SHIFT + the button keeps it open (U2: it was a double tap, which fast page flipping set off).
   - HOME held is **SHIFT**. Held alone a moment, its map covers the panel under the page's cards (U5, the review's A1; `ui_layer.c` `draw_shift`): what every control does on this page, 4 rows of 3 cells as the layer maps, this page's own jobs in TEXT (NOTES: PRESETS STEP, KEYS PICK, KEYS + REC WRITE, EDIT + REC CLEAR, + EDIT DELETE), the ones the same everywhere in MID, the SHIFT combos lit (+ SAVE TAKE, + REC REC SET, + BUTTON LOCK); the header's title says SHIFT, the footer LET GO: MENU. A combo ends it; HINTS OFF shows none.
   - HOME held alone and then let go opens the MENU, on release, so it never races a combo.
   - A tap never flashes a layer screen. (#40, #59, D#83; Sloop #1, #46)
5. **Knobs change only what is on the screen.** A turn made while a layer opens or closes is ignored. (#39, #102)
6. **LEDs tell the truth:**
   - The lit button is the current page.
   - Idle buttons glow (MENU > LEDS), so the black unit's labels stay readable.
   - LEDs breathe, never blink (U6: NAME's cycling key too).
   - Nothing is shown only on an LED. (#35, #119; Sloop #10, #33, #62)
7. **Destructive actions:** a dialog with CANCEL (OCT-) as the default, or a hold the screen names while it runs (the REC layer's CLEAR: HOLD: CLEAR, then held for the hold time). Undo where the firmware can (SAVE held), and say CAN'T BE UNDONE where it can't. Clearing still has to be quick. (D#91)
8. **Never lose a jam.** The session comes back at power-on (MENU > SYSTEM > RESTORE LAST, on by default). (D#130; Sloop #20) (2.0) A save or a project load brings it up to date within a second, so switching off right after one keeps it.
9. **Legibility floor:**
   - Values in large numerals; no hairline glyph details.
   - True-black and high-contrast palettes.
   - Colour is never the only cue: a track colour always comes with its number.
   - Animations are short and ANIM OFF turns them off. (D#80, #46, #50)
10. **The tape is home** (`go_tape`): the unit starts on SEQ › TAPE and HOME, and every way back, lands there; the all-tracks overview is one HOME tap on the tape (then the scope, then the tape). The overview: a lane per track with its steps in its colour, the playhead, and bar.beat in the header. The selected track is a band of its colour. (D#112, D#134)
11. **One line of hint** says what to do next, and it can be turned off (U4: MENU > DISPLAY > HINTS; an action page's OCT+ / OCT- line stays, it says what they do now). (D#88)

Things to avoid:
- a knob turn used as enter or confirm;
- a button whose meaning changes from screen to screen;
- jargon names (MOTION became AUTOMATION);
- text that only fits when ellipsised;
- SELECT dedicated to tempo.

## Controls

| Control | Tap | Hold | SHIFT + it |
|---|---|---|---|
| ENV, LFO | Open its page family; again: the next page | As a tap | — |
| ARP | ARP pages | PERFORM layer: each track's RUN on the white keys, mutes on the black, throws on K1–K4, GRID on SELECT | Lock |
| SAVE | Its page family (PRESETS, USER, PROJECT): the page it was left on, USER the first time (U3) | **UNDO**, one level back (6 levels kept) | — |
| FX | FX pages | FX layer: effects on the white keys; on the black the tracks' MUTE as GLO's ((2.0, D 1A): a tap latches it, held the track is cut; see One mute), macros on K1–K4 | Lock the layer open; locked, it latches (U2: MENU > FX LATCH retired) |
| GLO | MIXER, DELAY, REVERB, CHORUS, GLOBAL (U3: the buses, FX's before; SYSTEM gone) | GLO layer: mutes, solos, tap tempo; with SELECT: tempo | Lock |
| SEL | SCALE, CHORD (1.2.1, #8: the button is printed SEL; the page was SCL) | SEL layer: root on the keys | Lock |
| EDIT | EDIT pages, on every page (U2; on NOTES with REC armed it clears the cursor step, with a step held its locks) | EDIT layer: engine on the keys | Lock |
| SEQ | SEQ pages | SEQ TOOLS, on every page (U2) | Lock |
| REC | Arm, on the press (U2); on an armed track every track disarmed (1.4.1) | — | REC layer, locked: CLEAR (held), CLICK, COUNT-IN, GRID, FEEL |
| HOME | The tape (a locked layer closes); on the tape, the tracks, then the scope, then the tape; with a layer's button held: locks that layer (1.2.1, #13) | **SHIFT**. Let go alone: MENU (not over a dialog or NAME, U2) | — |
| PLAY | Start / stop | — | — |
| OCT− / OCT+ | Octave. In dialogs and on action pages: OCT− cancel (or the tape, as its footer says; on the tape, the track overview), OCT+ do it. While the undo box (or message) shows: OCT− undo further, OCT+ redo (on action pages too, U2) | Both for 5 s: UPDATE MODE | — |

**SHIFT combos:**

| Combo | Does | Arrives in |
|---|---|---|
| SHIFT + SAVE | **TAKE:** keep the last loop of notes and knob moves on the selected track (`take.c`, `ui_take.c`); (2.5) stopped, with notes played since the stop: **FREE TAKE** (below) | Phase 2 (done) |
| SHIFT + PRESETS (NOTES, FEEL) | **RANGE:** the selection stretched from where it was (`ui_input.c` `sel_shift`) | 2.0 (D) |
| SHIFT + OCT− / OCT+ (NOTES, FEEL) | **COPY / PASTE** the selection (`sel_copy`, `sel_paste`) | 2.0 (D) |
| SHIFT + EDIT (NOTES, FEEL) | **DELETE** the selection, REC armed or not: the picked notes, else every note starting in it, with their ties; one undo (`sel_delete`). Elsewhere SHIFT + EDIT locks EDIT's layer, as any layer's button; here EDIT held + HOME does | 2.0 |
| HOME held alone, then let go | MENU (settings). It opens on release, so it never fights a SHIFT combo. | Phase 1 |

**TAKE in detail:**
- **What it keeps:** the last *complete* pass, notes and knob moves together.
- **What is logged:** while the track plays with REC off, the keys and their chords, MIDI and the arpeggiator (everything REC would record) and the knob moves are logged.
- **How notes merge:** as REC merges them. A held note ties into empty steps only, so a take never wipes a note. (2.0, #25) On a POLY track playing forward a held note goes on by itself to its key's release, a per-note tie beside whatever each step holds (4 notes a step, no pitch twice; no room: it ends there), and a note played meanwhile doesn't end it; a note played into a long one goes in beside it.
- **Undo:** one undo (hold SAVE) puts back the steps and the automation.
- **What it says:**
  - **BLOCKS:** the TAKE box sits over the panel for about 2 s. It shows the track's number on its colour, TAKE KEPT, what went in (14 NOTES, 3 KNOB MOVES; AUTOMATION FULL when the moves found no room), and [SAVE] HOLD TO UNDO. A newer message, an undo for example, ends it.
  - **Views with no panel for the box** (a layer, LARGE's tall cards, the MIXER): TAKE T1 in the header, then HOLD [SAVE]: UNDO (U4: one wording for the undo hint).
  - **Partial:** TAKE PARTIAL (TAKE T1 PARTIAL in the header) when more than about 128 notes in the pass overflowed the log.
  - PLAY ONE LOOP FIRST, NOTHING PLAYED or DISARM REC FOR A TAKE when there is nothing to take (U4: the way out, not just the problem).
- **Hint:** while HOME is held, the header says [SAVE] TAKE.

**FREE TAKE in detail** (2.5; `take.c` `free_note`, `ui_take.c` `fr_*`; tests/freetake_test.c): a take of what was played
while stopped, its tempo, length and first beat found by Skiff.
- **The log:** while the transport is stopped, each note's start and end from take_note's callers (the keys and their
  chords, MIDI, the arp), 4 bytes each (the time in 2 ms, 15 bits; the note; the velocity, 0 an end; the track) in a
  ring of 512 in the pool. Only those since the last stop count (`seq_stop` → `free_stop`: `free_n0`); nothing is
  logged while the picker is open (`free_hold`), so its PLAY's stop moves nothing.
- **The phrase:** the last one after a silence of over 2.5 s, 30 s at most (`fr_phrase`); notes started within 30 ms
  of the one before, chained, are one onset (a chord, a strum).
- **The tempo:** every BPM from 40 to 240 scored by how the onsets sit on its 1/16 grid at the best phase, |the sum of
  their unit phases|² (`fr_fit`; the phase by CORDIC, `fr_atan2`; no 64-bit divide on the chip: `fr_inc` without
  one). The guesses are the score's peaks, so ×2 and ÷2 come side by side, the one between 80 and 160 first when it
  scores at least 80 % of the best (`fr_guess`).
- **The picker** (its own screen while `fr.on`: the header FREE TAKE, cards GUESS BARS FROM TEMPO, the panel
  PATTERN's steps, the footer OCT+ KEEP, OCT- BACK, PLAY LISTEN): KNOB 1 the guess, KNOB 2 the bars (1, 2, 4 at 1/16;
  8 at 1/8), KNOB 3 which onset is the 1 (a pickup before it wraps to the loop's end), KNOB 4 TEMPO SET or KEEP (KEEP
  when another track has notes or the clock is external). Each turn renders the take afresh from the track as it was
  (`fr_from`). PLAY loops it; OCT+ (or SHIFT + SAVE again) keeps it; OCT- puts back the steps, LEN, DIV and the BPM.
- **It replaces the pattern** (the user's pick after the first unit test: an overdub lost a chord's fourth note on
  steps that held three): every step cleared as on a new unit, then the notes put in as a played take would be
  (`rec_put`, per-note ties, drum lanes, FEEL's nudge); one undo, SAVE held.
- **The hint** (2.5, `ui_layer.c` `free_hint`): the first 8 notes played stopped (16 log entries since the stop) say
  SHIFT + SAVE: FREE TAKE (SMK: SHIFT + BT) once, bit 0 of `layer_seen` (no layer has it); not over a message, a
  dialog or the menu, never with HINTS OFF.

Add a combo here before adding it to the code. Every SHIFT combo has to appear on the SHIFT map.

**UNDO in detail** (`ui.c` `undo_ring`, `undo_step`):
- **What's kept:** the last 6 undoable acts, on any tracks. A copy is taken by a sound load, a take, a clear, a SEQ TOOLS action or an automation edit. Loads in a row while browsing count as one. RUN and MUTE keys take none.
- **How:** SAVE held goes one back. Then, while the undo box shows (2.7 s, `UNDO_WIN`), OCT− goes further back and OCT+ forward again. A new edit after undoing drops the redo.
- **A copy belongs to its pattern:** taken while the track was on another pattern, SAVE held says UNDO NEEDS PATTERN 07 (its number) and changes nothing.
- **What it says:** the box (BLOCKS) or the header names the act and its track (UNDO T1 TAKE) and how far back you are (2 OF 5 UNDONE). The keys dim when there is nothing more that way.
- **Not undoable:** project and user-sound saves, a project load, erasing a sound, clearing or merging a tape track. Their dialogs say CAN'T BE UNDONE. (U1) EDIT's step clear on NOTES and the grid takes a copy: a run of them (under 1.5 s apart, nothing else changed) is one. (U2) A recording pass is one: REC arming takes a copy (RECORDING), dropped again when it recorded nothing.

**FLAM** (`seq.c`): RATCHET's fifth value on SEQ › FEEL (2.0; KNOB 2: X1–X4, then FLAM; SEQ › CHANCE's knob 3 before). The step plays a grace hit at 9/16 of its velocity, then the full hit 15 ms later (at most half the step). Chords and drum hits flam too; the chance is rolled once for both.

**SEQ › NOTES and FEEL** (2.0, the user's D: design/mockups/v2_notes.png; STEP, ROLL, CHANCE and PLAY before). PRESETS moves the cursor on both, so no knob is the step any more; it jumps a long note's ties (back onto its first step, on past its end), and the header shows the cursor's step (NOTES 5). The footer says so, PRESETS: STEP (the SMK: K4: STEP), then on NOTES the keys' PICK or WRITE; SHIFT's map has the combos (RANGE, COPY, PASTE, DELETE).
- **The selection** (`ui.c` `nsel`): the cursor's step, or a range: SHIFT + PRESETS stretches it from where it was (a SHIFT combo; the header says NOTES 5-8). With REC off a key picks its note in the selected steps (again: dropped; its row lit on the roll's keyboard strip); none picked: every note. The cursor moved any other way drops it.
- **NOTES**, the piano roll (Felucca's STEP page): PITCH, START, LENGTH, VELOCITY of the selection as a unit; nothing selected (an empty step, a tie): the cards read --, and the knobs write nothing (notes go in with REC or a paste). PITCH walks the track's SCALE (CHROMATIC: semitones), each note with the steps it goes on; one note off the keyboard, or onto a note that stays there, and none moves. START moves whole steps (nothing picked, or whole chords): their notes, each as long as it is, with the step's values, locks and automation (with nothing else on their steps and an empty step beside them, the steps round as before 2.0); picked notes go into the chord a step on or onto a step where none starts, with their step's values. **Notes overlap** (2.0, #25, design/TIER_B.md): a note moved back past the end of the note before goes in beside it, and that note keeps its length; a step where another note starts stops whole steps. Something in the way: nothing moves, STEP n TAKEN. LENGTH is 1–99 % of the step (the GATE lock below: the step's, for the notes ending there), then 1 STEP (100 %; (2.0, the user's standard) where a new pattern's notes start: PATTERN's GATE is 100 % by default, 50 % before), then 2–128 STEPS ((2.0.2, #58) 2–16 before, though REC tied further), each note's own: the picked ones alone (a chord's notes can end on different steps), through steps where other notes start. A note goes on only where there is room: 4 notes a step, no pitch twice, no lane hits; else it ends before that step, STEP n TAKEN. VELOCITY moves every step by the same amount. Each knob's run of turns is one undo. The roll draws a note going on as one bar through the step lines.
- **How it is kept** (`ui_input.c` `ed_open` / `ed_close`): every edit works on a copy of the steps unfolded (each TIE as the notes it holds, tied), note by note, folded back (a step where the notes before all go on, and nothing else: a TIE again) and written at once with the audio IRQ off. A step not changed keeps its bytes; a pattern without overlaps folds back to TIEs, as 1.4 reads it.
- NOTES keeps STEP's ways: step entry with REC on (keys held together: a chord; (#31) PRESETS meanwhile its LENGTH; let go: the cursor past its ties; (2.0) on a POLY track a note entered on a long note's step goes in beside it, the long one going on; MONO replaces it as before), a held step's locks, [EDIT] clearing the cursor step with its notes' ties while REC is armed (the notes going on through it end before it; a tie left behind would hold nothing, unseen; PRESETS lands on such a tie as on an empty step). **SHIFT + EDIT** deletes the selection, REC armed or not: the picked notes, else every note starting in it (its hits too), each with the steps it goes on; a step left with nothing is cleared with its locks; one undo, DELETED 3 NOTES. A DRUM track's NOTES is its grid: LANE, HIT, ACC (the range's steps), VELOCITY.
- **FEEL**, the 16 tiles (the B mockup's STEP): CHANCE (OFF, a REST; 1–100 %; then COND's conditions FILL to 4:4, one knob as Elektron's), RATCHET (x1–x4, FLAM), NUDGE, TYPE (- ACC SLD A+S), each set on every selected step. A tile shows its step's value of the knob turned last; a range is framed. A DRUM track shows its grid.
- **SHIFT + OCT- / OCT+** copy and paste the selection: its notes (picked: those), each as long as it is, from the first one's step (two bars at most), with their steps' values; pasted at the cursor into what is there (the chords and the notes going on there: 4 a step, no pitch twice, no tie onto hits; a step with no start of its own takes the copied one's values); anything in the way: nothing, said. The paste is one undo and the selection after. **On a DRUM track's grid** (2.0, the user's ask) the copy is the LANE card's lane, its hits and their accents from its first hit (COPIED KICK 4 STEPS; none there: NO KICK TO COPY), and the paste puts them on the LANE shown then (PASTED ON CLAP: a figure moved from lane to lane); SHIFT + OCT- again on the same steps and lane copies every lane (COPIED ALL LANES), pasted where they were, and once more the lane. FEEL has no LANE card: every lane.
- A note's own length within a chord, and notes that overlap, are a tie bit a note (#25: core.h `step_ties`, the pattern record's version 3). Velocity, chance, ratchet, nudge and type are a step's.
- **SEQ TOOLS on notes that overlap** (2.0, #25, `ui_tools.c`): REVERSE goes note by note when a per-note tie is anywhere in LEN: each note starts where it ended, its ties after it, with its step's values; a step's locks and automation go where its first note starts now (lane hits and steps with no start of their own mirror). Without overlaps REVERSE is as before (whole steps, a note's ties after it). RANDOM and COOK change only the notes starting on a step, each with the steps it goes on (`tl_chord_to`; two meeting on a step: that step as it was). SHIFT, DOUBLE and HALF keep a note beside its ties as they are.
- **Takes and overlaps** (2.0, #25, `ui_take.c`): a TAKE on a POLY track going forward: a key held while another starts on a later step goes on as its own pitch, a per-note tie each step, beside what the step holds (4 notes a step; lane hits or no room: its hold ends there); let go before a step's middle, it is taken out of that step again. A note taken into a TIE goes in beside the notes the TIE holds. MONO, LEGATO and UNISON, and the other directions: a new note on another step ends the hold, held notes write TIEs. REC no longer writes steps: it writes the tape (below); step entry and TAKES are what put notes on the steps.
- **PATTERN**'s PRESETS turns the track's DIR (PLAY's knob 3 before), shown under its steps.

**NUDGE** (`seq.c` `seq_enter`): a NOTE step plays up to half a step early or late, in 64ths of its own length (−32 to +31; 0 on the grid).
- **Early** notes play in the step before, in the play direction. The first step after PLAY can't be early, so it plays on the grid; so does an early step under RAND.
- The step's automation and locks move with it. RATCH parts and a FLAM's full hit count from where the step plays.
- A trig condition on an early step counts the pass that step opens.
- TIE and REST steps keep to the grid.
- The piano roll and the DRUM grid draw a nudged note where it plays. The card reads +12 /64.
- **FEEL** (REC held, knob 4; OFF by default): takes keep where you played (the tape keeps its own ticks). A note going into an empty step sets that step's nudge, so it plays back as played. A step that already has notes keeps its own nudge. With FEEL OFF, a take lands on the grid as before.
- **Saved** with the track's sequencer (the tape store's chunk 3). Felucca's FUN9 projects show OLD FORMAT.
- User presets are sounds only since phase 5: SAVE › USER writes no pattern, and SEQ › PHRASES is gone with the factory phrases.

**A step's own note length** (GATE as a lock: `motion.c` `motion_param`, `ui_input.c` `len_set`): (2.0) NOTES' LENGTH up to 100 %, SEQ › PLAY knob 4 GATE before.
- It shows the cursor step's GATE: its own, or the track's (2.0: both live; SEQ › PLAY dimmed the track's). Turning it gives the step its own, starting from the track's; turned back to the track's value, the step follows the track again. SAVE held undoes it.
- The step's notes last that long, as a share of the step (1–100 %); the next step goes back to the track's GATE. RATCH parts take it too. For a note longer than a step, TIE steps still do it.
- It's a parameter lock, so it uses one of the 64 shared automation records (AUTOMATION FULL when they're used up), and the roll marks the step. CLEAR and [EDIT] on a held step remove it with the step's other locks. (2.0.2, #56) A step held no longer turns it with knob 4 after the PATTERN page: SEQ's own pages never give a held step their knobs (a held step kept PATTERN's LEN, DIV, SWING and GATE until a sound page was shown, the drum grid's KIT, PITCH, TONE and DECAY gone).
- GATE can be automated now too: knob moves recorded with REC, and takes. LEN, DIV and SWING still can't.
- The sequencer keeps it with its locks; its own GATE is always the track's, never a step's.

**PLAY DIRECTION** (`seq.c` `seq_next`): DIR, per track: (2.0) PATTERN's PRESETS (PRESETS: DIR), SEQ › PLAY knob 3 before.
- **FWD:** 1 to LEN, as always. **REV:** LEN down to 1. **PING:** up and down, each end played once (1 2 3 4 3 2 1 2 …). **RAND:** any step each time.
- **A pass is LEN steps** in every direction. Takes and trig conditions (1:2 …) count passes this way, so a 1:2 step in REV still plays every other time round.
- **REC and takes** put a note played late in a step on the next step in the direction. With RAND nobody knows the next step, so the note goes on the step playing.
- DIR stays when a sound is loaded; a pattern brings its own.

**The tape, GRID and the sequencers** (`tape.c`, `grid.c`, `launch.c`, `tape_store.c`, `seq.c`): the notes live in two places, each track's step sequencer and a 4-track note tape. The pattern pool, the lists, the song, the scenes and the LAUNCH modes are gone, and with them the keys' step live recording (REC writes to the tape; step entry, TAKE and knob recording stay).
- **The sequencer** is one a track: its 64 steps whole (chords, velocity, accent, slide, chance, RATCH or FLAM, trig condition, nudge, drum hits and their accents, each note's own tie), LEN, DIV, SWING, GATE and DIR, and the track's automation and locks. The sound is not part of it. All of it is saved with the project, and the track's RUN bit: the sequencer plays only while RUN is set and the transport plays (`launch.c` `seq_run`; all on in a new project). Every sequencer and ARP note goes through one function, `seq_out` (vel 0 an off), the one place a future sequencer TYPE would replace; the project keeps a type byte a track (0: this step sequencer).
- **Time:** 480 ticks to a quarter note (`TAPE_TPQ`), 4/4 only, a bar `TAPE_BAR` 1920; one 24-PPQN pulse is 20 ticks and an SPP sixteenth 120. `song_ticks` counts the ticks since PLAY or a locate (never wrapped: due times), `tape_head` the same wrapped at the tape's length, `tape_pass` the wraps. `tape_clock` (once an audio block, in `events_block`) takes them from the beat counters the ARP and the click use, so a tempo change or an external clock needs no rescale.
- **GRID** (`grid.c`), one setting for everything timed on the keys: a table of lines on the tape's ticks, the byte in the settings (`favorites.factory[15][26]`; MENU › TIMING, REC layer KNOB 3, PERFORM's SELECT; OFF by default):

  | GRID | ticks | swings |
  |---|---|---|
  | OFF | – | – |
  | 1 BAR, 1/2, 1/4 | 1920, 960, 480 | no |
  | 1/8, 1/16, 1/32 | 240, 120, 60 | yes |
  | 1/4T, 1/8T, 1/16T | 320, 160, 80 | no |

  - Every line divides a bar, so the lines repeat every bar and every loop, and fall where the click, the steps and the arpeggiator count (PLAY starts from a bar line: beat 0, step 0).
  - **Swing:** the odd lines of 1/8, 1/16 and 1/32 are later by `swing * ticks / 250`, GLOBAL's SWING only (the law of `swing_step_len`).
  - **The late window** is the smaller of an eighth of the grid and 20 ms (0.16 x BPM ticks).
  - **One-time reset:** a settings record from before the byte meant LAUNCH there (`ui_set_rev`, and `settings_import_per` for a restored one): GRID starts OFF.
- **Live playing** (`seq.c` `input_on` decides, `input_now` sounds): with GRID on and the transport playing, a key pressed within the window after a line sounds at once; later it waits for the next line (`live_q`, 16 entries, FIFO; `live_tick` sounds the due ones). A key released before its line keeps its length; the same key sounding again ends that note's pending end first (`live_settle`). A full queue sounds at once. Not delayed: the ARP's keys (it times its own start), the sequencer, the tape. GRID OFF, STOP, panic, MIDI IN overflow, an engine switch and the pedal's end flush or drop the entries (nothing hangs: a note that never sounded is dropped).
- **ARP start** (`arp_step`, `arp_wait`): the first note of an arpeggio waits for the next line when the key came after the window; GRID OFF is as before.
- **Recording** (`voice.c` `trk_note_go` and `trk_note_off`): while REC is armed on a track and the transport plays, every note the track sounds goes to the audio ISR's record queue (`tp_rq`, 128 events, never blocking: a lost event closes every open note) and the main loop's `tape_poll` puts it on the target track: keys, MIDI IN, the sequencer, the ARP, strummed chords, a follower's leader (the note before `fol_on`; playback applies it again). A note sounded by the tape itself is not recorded (`tape_playing`) and is not strummed again (it was recorded strummed: `strum_add`), nor is a muted track's recorded. With GRID on a live note is stamped on its line, never forward past it, so it can't pass the tape's end; the others at the head. Count-in notes land on the bar line PLAY started from (the head), held or a sixteenth long.
  - **Overdub** (the default, the project's): a note of the same track and pitch within 12 ticks of one on the target track is the same note (so a sequencer left running over passes leaves one). **Replace** (the editor's Tape tab): the ISR marks the target track's notes it passes as ghosts and skips them; the main loop sweeps them. Notes recorded this pass are behind the cursor, so a pass replaces the one before.
  - **A held note** is open (never played: the live note is sounding) until its off: its length is counted across the loop's end (1 to the tape's length); a full table closes the oldest at its length so far. STOP closes them.
- **The tape** (`tape.c`, all in the `.pool`): 1,440 nodes (`TAPE_N`), 10 bytes each, in 4 lists sorted by tick (a note: tick 20 bits, pitch, instrument, velocity, length 20 bits, a ghost and a held bit), merged without loss. One structural writer, the main loop, which walks with the interrupts on and commits each change in a few stores with them off; the ISR (`tape_block`, after `seq_tick`) only moves each track's cursor, sets ghosts, plays and fills the record queue. A node that leaves a list waits in limbo until the ISR has finished a block. Readers (the page, TAPE_GET, a save) walk bounded by `TAPE_N`.
  - **Playing** (`tape_block`): each track's due notes sound through `trk_note_on` of their instrument (at most 16 a track a block; the ones past it are skipped), up to 48 sounding ones in a voice table (one slot to an instrument and pitch: a retrigger takes the longer end). STOP, a locate, panic and an engine switch (that instrument's, as the switch begins) end the tape's voices. A muted track sounds nothing.
  - **Length** L: 1 to 256 bars; notes at or past L stay stored, unplayed, saved; a longer L shows them again. **Merge** moves each note of a track into another in tick order (the target's first on equal ticks), no allocation; **clear** moves a track's nodes to limbo.
  - **The head:** PLAY snaps it down to its bar line and starts there; GLO + PLAY from 0; STOP keeps it, STOP while stopped sends it to 0; an external Start goes to 0 and Continue keeps it; Song Position sets it (`pos * 120` ticks, wrapped at L). The SMK-37's `<` and `>` move it a bar while stopped (`tape_bar_step`; playing: STOP FIRST).
  - **Not in v1:** knob moves (the automation records them), audio (the unit has 1 MiB of flash).
- **Saved** (`tape_store.c`): each of the four project slots and the music now has a *set* of 4 chunks of 3,840 bytes in the 20 sectors the pattern pool had (project slots 4 x 4, the music's 4: five sets of four). Chunks 0 to 2 are the tape's records (8 bytes: tick, pitch, instrument, length, velocity; no ghost, no held note: a save is stopped only), chunk 3 the six sequencers (640 bytes a track: the type, 64 steps of 9 bytes, the nudges, 15 spare; 90 bytes spare in all). Chunk 0's commit record also keeps the tape's length, each track's count, the mutes, the target and OVERDUB. A write puts a chunk whole into a sector no set holds, its commit record last (magic `SKT1`, the set's generation, a hash); the slot moves to the new set only once all four are in, and at power-on each slot takes its newest complete set, else the one before: a torn write leaves the old set. A set is written only when one of its chunks changed; a save with fewer than four free sectors gives up the music's set for the next autosave. In the editor's full backup as objects 40–59 (the ids 16–31 of the pool are accepted by a restore and dropped).
- **A project** (SKF5, 2,560 bytes, 446 free; `project.c`) keeps the sounds, the mix, the motion store (the automation's records), the RUN bits, the FM6 patches and the name, no steps and no tape (their set is saved first). SKF2 to SKF4 (their steps were pool patterns) and older show **OLD FORMAT** in SAVE › PROJECT and keep their bytes: LOAD and NAME say OLD FORMAT, SAVE asks before writing over it, DELETE clears it, a backup takes it as it is; the unit converts nothing (the editor's Restore, `web/conv`, makes SKF5 and a set of FUN1..FUN9 and SKF1). A user sound of versions 1–3 or DIGITAL: LOAD says OLD FORMAT and SAVE asks first, naming it so (SAVE › USER's list shows "--").
- **AUTOMATION FULL:** the 64 automation records are the six tracks'; a track takes no new one past them.
- **TAPE FULL:** a recorded note finding no free node is dropped, with that message.

**SEQ › TAPE** (`ui_graph.c` `graph_tape`, `ui_input.c` `tape_knob`; after AUTOMATION): the page is a tape machine on the whole screen under the header (`ui.c` `tape_scene`: no cards, the panel from `Y_LABEL`), drawn in two canvases (the pool's canvas is 138 rows, the scene 186; `TPS_SEAM` splits them): a striped sun (NEON's `ne_sun`; the other looks a THEME and ACCENT blend) behind two large reels (flange, a tape pack whose radius follows the head, a hub with three spokes), the tape from each over a guide roller to a head block, a perspective floor under it, a large bar.beat counter, and the transport as a sign of shapes between the reels (play triangle, stop square, REC dot blinking, an armed ring). At the bottom the four tracks are thin bars (8 px) over the tape's length: a note a segment in its instrument's colour (a muted track's DIM), pitch not shown, touching notes of one instrument merged; the target's bar raised with an ACCENT tab, a merge's source framed, a loop bracket over the length with a tick a bar (every 2nd, 4th .. when they crowd), the head a TEXT line across them. The hubs turn a bar of tape a turn (`tape_reel_phase`, from `tape_head` and the SINE table), the packs follow `tape_head` and the marks on the tape pass a beat each: all from the head alone, so a stopped tape stands still. The notes held, the GRID and the length are a caption line; a knob turned shows a pill (`tape_pill`, the knob's name and value from `tape_words`) in its place until `ui.hot_t` runs out, and a layer's or SHIFT's map brings the four cards back over the page. The page is the main screen: MENU › LARGE leaves it its layout (its captions and pill in M).
- **Knobs:** 1 TRACK (1–4: the target, the one REC writes to), 2 LENGTH (1–256 BARS), 3 ACTION (CLEAR, or merge track 1–4 into the target; never itself), 4 MUTE (OFF, ON the target's). No knob is an action: **OCT+** does KNOB 3's, after a dialog (CLEAR TAPE TRACK 2?, MERGE TRACK 3 INTO 2?, with the notes that go; can't be undone). NOTHING TO CLEAR, PICK ANOTHER TRACK and THAT TRACK IS EMPTY are the refusals.
- **The head** is a line on the lanes; PLAY, STOP and the SMK's `<` `>` move it as above.

**PERFORM layer** (`ui_layer.c` `layer_perf`, `launch.c`): ARP held.
- **The map:** a row a track, its number in its colour, then its RUN cell (RUN in a tile of the track's colour, DIM while muted; OFF as text) and, at the row's end, M in REC's colour while the track is muted. The selected track's row (the keys' and knobs' track) is a band of its colour with everything on it in INK, as on HOME.
  - **Header** (BLOCKS): PERFORM, the lock before it while locked; a message (T2 RUN ON ON GRID, T4 MUTE NEXT STEP, GRID 1/8) takes its place for a while.
  - **Footer:** WHITE: RUN, BLACK: MUTE, OCT-: CANCEL. This is the one screen that doesn't label SELECT, PRESETS and OCT− (UX rule 2): the mockup's choice, kept on request.
- **White keys 1–6** turn that track's RUN: stopped (or counting in), at once; playing, **armed** (`launch.c` `arm`): the main loop writes its `at` (a `song_ticks` time: the next GRID line, GRID OFF the track's next step) and then its op, and the audio ISR applies it at the track's first step boundary from then on. A second press of the same kind before it lands takes it back (T2 RUN ON CANCELLED); the other kind replaces the first. The cell is framed while armed. RUN off ends the track's sounding sequencer notes (`seq_release`).
- **Black keys 1–6:** each tap turns the track's MUTE the other way, armed the same way ((2.0, D 1A) held past HOLD, the track is cut while it's held instead, nothing armed: One mute). The M frames while a mute is armed. The step starting at the line plays as the new MUTE says.
- **OCT−** cancels everything armed (CANCELLED; NOTHING ARMED when there was none). What is armed when the transport stops comes in at once (`arm_poll`), as while stopped; a project load drops it.
- **SELECT turns GRID** (the header says GRID 1/16), kept with the settings when the layer lets go.
- **Throws** (knobs 1–4; `throw.c` `throw_id`), the mockup's four: **CUTOFF** (the sound's tone: its first HOME parameter where that is one, CUTOFF on most engines; U3 V2 `THR_TONE` where it picks a sample or a source), then **REVERB**, **DELAY** and **DIST**, the track's sends on every engine (U4: were VERB, DLY, DRV). Each knob pushes its parameter away from its value for a moment. The card shows how far it's out, in the knob's steps (+0 at rest, +18), its gauge from the middle.
  - Once the knob rests (120 ms), the throw glides back in a line over one bar ((2.0, D 2A) over MENU › CONTROL › RETURN's time: One way back). STOP drops it at once. It never goes past the parameter's range. (2.0) OCT+ in PERFORM keeps every throw out: its value as it sounds then becomes the parameter's own (`throw_keep`; not recorded, no undo, as MOMENTARY's OCT+).
  - It's never saved and never edits the sound: the audio interrupt puts it on at the start of each block (so a note starting then reads it too, its attack for example) and takes it off after the mix. What the screen, the editor, projects and presets see is always the parameter's own value. Automation or a lock writing the parameter meanwhile wins.
- **Timing:** Felucca rounds each DIV's step down to whole samples, so 1/32 and triplet tracks run a few samples a beat ahead of 1/16's. Their step starting less than half a step before the bar line counts as the bar's.

**DUCK** (FX › DUCK, after SLICER; `duck.c`): a track dips under another track's hits, the classic sidechain pump.
- **Knob 1 BY:** the track it ducks under, OFF or T1–T6. The knob steps over the track itself.
- **Knob 2 DEPTH:** how far down at each hit (100 % is silent). The default is 76 %.
- **Knob 3 REL:** how long it takes to come all the way back, in a straight line. Times are the envelopes' (1 ms to 10 s); the default is 199 ms.
- **What counts as a hit:** every note the source starts: steps, ratchets, flams, keys, MIDI and the arpeggiator. A DRUM kit counts its KICK lane only, so its hats never pump anything. A muted source strikes nothing.
- **The sound:** the gain drops to DEPTH over 3 ms (no click) and comes back over REL. It sits on the track's own signal after DIST and the SLICER, before LEVEL, PAN and the sends, so its effect sends dip too, while delay and reverb tails already there ring on.
- **The graph:** the track's level over one bar of its source's steps: a dip at each hit, its recovery over REL, the hits marked underneath in the source's colour.
- The three belong to the track's mix: a sound load keeps them, and they're saved in the project. DEPTH and REL can be automated; BY can't. With BY OFF the mix is bit for bit as without ducking.

**The stereo delay and chorus** (2.5; `fx.c` `dly_mono`, `dly_stereo`, `cho_st`; tests/stdelay_test.c): GLO › DELAY's
MODE (`G_DMODE`, GSID 27): MONO, STEREO, PING.
- **MONO** is 2.2's line, bit for bit: one 65,536-sample int16 line at 44.1 kHz in `dly_buf` (128 KiB).
- **STEREO and PING** use the same 128 KiB as two 32,768-sample lines at half rate (22.05 kHz, 1.49 s each), written
  through HALL's 1-2-1 filter and read interpolated (the repeats lose the highs above about 10 kHz); their loop's
  products round towards 0 (`mulq15z`), so a tail dies to exact 0. PING feeds each line into the other.
- **Stereo sends:** a second sum (`mixsum_t` `e`, the right send) from each track's PAN in `mix_part`, `mix_spread`, the
  lane pans and `smx_render`; MONO's right send is skipped. The stereo wet goes out beside HALL's (`rev_side`).
- **The chorus** (its CHORUS and FLANGER; PHASER stays mono) reads its line twice, the LFO 180° apart, in STEREO
  and PING (`cho_st`).
- **Switching** fades a block and clears the lines over 32 blocks (`dly_switch`, `dly_clear`), as HALL's `rev_switch`.
- **The mono loops keep 2.2's shape;** the stereo work sits in noinline helpers with their own target_budget lines
  (`smx_right`, `dly_mono`, `dly_stereo`, `cho_st`, `perf_pre_st`): stereo branches inside the mono loops had cost
  +20..77 % on its estimate.
- **Default:** STEREO for a new project (`params.c`); a project saved before 2.5 loads MONO, as it played
  (`proj_g_read27`, `proj_g_from_v2`, and SKF4's reader when the stored globals end before `G_DMODE`).
- **PRESETS turns MODE** on GLO › DELAY and FX › DELAY (the PRESETS card, below).

**FX › DELAY and FX › REVERB** (2.5, the user's 1A; `params.c` `GR_FXDLY`, `GR_FXREV`): after FX's first page, the track's
send on KNOB 1 beside the bus's own on KNOB 2-4 (GLO › DELAY's TIME, FEEDBACK, COLOR; GLO › REVERB's TYPE, SIZE,
DAMP); the graphs as GLO's, scaled by the send. A knob's automation only for the track's own (a pointer into `t->p`):
the buses' values are global. TIME turns in `DTIME_ORDER`, the dotted values in their place.

**The PRESETS card** (2.5, the user's 5B; `ui_graph.c` `pr_knob`, `pr_card`): where PRESETS does something special, an
84 × 40 card top right: PRESETS with a small knob (the SMK's K4), the value in the graph's colour, the job dim. On
GLO › DELAY and FX › DELAY (MODE), PATTERN (DIR; a 64-step pattern: one line under the rows, `PR_LINE`), INSERT (MIX,
`ui_pgraph.c` `pr_mix_card`). NOTES keeps its footer hint.

**INSERT on one page, and FILTR** (2.5; the user's ask and 6B; `fx.c` `ins_filter`): INSERT 2 is gone, PRESETS turns
MIX on INSERT's page (FX has 7 pages). FILTR (`IT_FILTER`, after CHOR): KNOB 2 CUTOFF, KNOB 3 RES, KNOB 4 SHAPE from
LP through BP to HP (`F_SHAPE`: "30 BP" between). It is `dsp.c`'s `tsvf` (Q13 coefficients, Q12 damping): the BP
taken as k·v1 so it is as loud as the others, the three blended by SHAPE, the input clamped, the integrator's steps
rounded (floored, they left a DC of about 1,700 at low cutoffs). The graph (`ui_pgraph.c` `flt_curve`, shared with
ANALOG's FLT, which is unchanged): the larger of the two shapes at their shares (`flt_off`), the LP BP HP strip and
its mark. tests/insert_test.c: ±2 octaves, BP at the cutoff, no step over 1 dB across SHAPE.

**STRUM** (2.5, the user's 7B; `voice.c` `sq`, `strum_*`; tests/chord_test.c): SCL › CHORD's KNOB 3 STRUM (OFF, 1-100
ms) and KNOB 4 DIR (DOWN, UP, ALT; dim at OFF), `P_STRUM`, `P_SDIR` before `P_E0` (SIDs 146, 147; `P_COUNT` 148).
`trk_note_on` hands a note to the track's queue (`strum_add`, only inside `events_block`: `sq_in`), `strum_flush` at
its end sorts the chord and spreads it over STRUM (one divide a chord; ALT turns each chord round, from down at
PLAY), `strum_tick` at its start plays what is due. A note-off drops its queued note; `trk_all_off` clears the queue.
POLY only; kept by a sound load. With STRUM on, CHORD's graph draws the notes in time (`graph_strum`).

**KEEP SENDS** (2.5, MENU › CONTROL, OFF by default): bit 5 of the settings' MIDI byte [15][17] (`PREF_KEEP_SENDS`),
read by `params.c` `param_kept` (through `ks_io`, set in `ui.c`; a build without the UI: OFF), the one rule of every
load (PRESETS, a user sound, the editor's): ON, CHORUS DELAY REVERB stay the track's, as LEVEL and
PAN. `preset_fill` asks it before writing a preset's sends; DIGITAL's `fm4_load_preset` puts the kept ones back after
`fm4_preset_values` (`fm4_convert.c` comes before `params.c` in the host builds). The editor's MENU id 21.

**Six tracks** (phase 6; `core.h` `NTRK`, `NHEAVY`; mockups `design/mock_p6.py`, chosen 2026-10-10): T1–T6, T5 and T6 light.
- **The voices** were still 8, shared by every track (`voice.c`): two more tracks added each block's fixed work, not
  voices. (2.5: 16 voices, below.)
- **Light tracks** (`engines.c` `eng_allowed`): T5 and T6 play every engine but PHYS and GRAIN, whose state is the big one (T1–T4 keep a PHYS-sized area each; a light track has DRUM's 1.8 KB, `eng_lite`).
  - The ENG knob and EDIT's KNOB 1 step past them; the PRESETS list leaves them out; the EDIT layer shows them dimmed, their keys dark.
  - Asking for one anyway (an EDIT key, a user sound, the editor) is refused with PHYS: T1-T4 ONLY in the header; nothing changes.
  - A project holding one there (only a crafted file can) loads that track's power-on sound.
- **Power-on sounds:** T5 SAMPLE PIANO, T6 DRUM KIT.
- **The track screens show all six** (U3, the review's N1; `design/mock_u3.py`, the user's pick A. Phase 6 showed four with a scroll bar, `trk_bar`, still drawn by a four-track build):
  - HOME: the selected track's lane at its size (its name over its engine), the other five compact: 18 px, the number in M, the name in S, 12 px cells (`ui_home.c`).
  - SEQ › TAPE: four lanes (the tape's tracks), not the six.
  - PERFORM: six track rows, 17 px rows 19 apart, 13 px tiles.
  - MIXER: six 36 px strips at the layer maps' columns (`ui_graph.c` `mix_strip`): the number, its state (recording a square in REC, armed its outline in the accent, muted M), the sound's first word, LEVEL a fader with the output meter beside it and its dB under it, the PAN (L31, C, R37). REV is on the cards only (the selected track's KNOB 3). The selected track's strip is a band of its colour. LARGE: the number, the state, the name, LEVEL as a gauge and the meter under it.
  - GLO's cards: four, the ones around the selected track (T1–T4, or the four ending at T5 or T6; `ui.c` `trk_top`), KNOB 1–4 their LEVELs.
- **The key maps show all six:**
  - GLO: SOLO 1–6 on F G A B C D, then ALL on E4, FILL on F4, TAP on G4 (two columns wide; in this order since 1.2.1, #11), MUTE 1–6 on F# G# A# C# D# F#. 36 px cells.
  - FX: the effects as before; the mute row of six as GLO's.
  - PERFORM: white keys 1–6 turn each track's RUN, black keys 1–6 arm the mutes.
- **MIDI IN** (MENU > MIDI IN CH1-6, U3): channels 1–6 play T1–T6; 7–16 are free for other instruments. (1.3, from Felucca 1.5.5) MENU > MIDI > T1..T6 CHANNEL move a track to any channel (`seq.c` `trk_midi_ch`, settings bytes [15][6..11] as the distance up from the track's own, so 0 is CH1-6): MIDI IN hears the channels the tracks are on (the first track on a channel plays it), the keys play out on them, and a channel no track is on any more lets go of its notes at the next block (`midi_route_drop`). MIDI IN says TRACK once a track moved.
- **MIDI OUT** (2.2): one funnel. Every note a track sounds passes voice.c `trk_note_on` / `trk_note_off` (steps, ratchets, flams, NUDGE, the arp, chords, chord follow after `fol_on`, keys, MIDI IN, STEP PREVIEW), and there `seq.c` `mo_note` sends it as NOTES OUT says (ALL; KEYS: `key_on` / `key_off`'s instead; OFF; an EXT track always, and it returns before any voice). A bitmap per track (`mn.on`) keeps what went out and on which channel (`mn.ch`): a note-off only for a note sent, on its channel; a channel changed, NOTES OUT changed, `trk_all_off` (a panic, CC120/123, an engine switch) or STOP end what is owed; a note-off the ring had no room for is kept (`mn.owe`) and sent the next block. `mn.quiet` around `midi_event`: MIDI IN is never sent back. The ring (`usb.c` `mo`, 128 events) holds each event with the TIMER4 tick its block plays at (`audio.c` sets `mo.tick` per block); `ep1_tx` (TIMER5, 2 kHz, also nested in a render as `uac_service`) sends the head events that are due, so MIDI leaves with its sound (to half a millisecond), not in a burst a half buffer early. A packet the host leaves for 20 ms is flushed (no app reading: an app opening later gets the present, not a stale note-on). CLOCK OUT: six pulses to the sequencer's own 1/16 (`div_samples(2)`, as the steps, the click and the GRID), each at its sample in the block (`mo_block_begin` / `mo_block_end`), Start in `seq_start` (the first pulse with step 0), Stop in `seq_stop` after the notes' ends. The SMK-37's TRS jack reads the same ring (`mo_trs`, its own read index), realtime ahead of the bytes queued. CC OUT: `midi_control.c` `cc_out_of` (learned first, else the standard map, unless learned elsewhere) and `cc_value` (`midi_cc_set`'s inverse), from the knob paths (`midi_learn.c` `cc_knob` via `ml_knob`, the SMK faders) and, on an EXT track, from a step's locks and automation (`mo_motion` around `motion_step`, before the step's notes). The settings: [15][17] (NOTES OUT, CLOCK OUT, CC OUT, THRU), [15][5] bits 3..7 (REMOTE), [1][1] (AUTO CHANNEL), the T1..T6 CHANNEL bytes' bits 6 (EXT) and 7 (OFF); 0 is every default, and older firmware reads the channels as they were. REMOTE presses go through the input HAL's debounce (`fm1_in.virt` / `vtap`), so they act as the contacts do. (2.5) REMOTE's knobs: on its channel CC 116-119 KNOB 1-4, CC 85 ALGORITHM, 86 PRESETS, 87 SELECT, relative (1-63 right, 127-65 left, 0 and 64 nothing; Sloop maps no encoders, 120-127 are channel modes): `midi_control.c` `midi_event` adds the steps to `rmt_enc_q` (`ui.c`: `panel.c` `rmt_steps`, a static assert pins the encoders' order), `panel_enc` takes them with its own, the IRQs off. SPP: `midi_clock_spp` keeps it for Continue; `seq_locate` = `seq_start`, then each track's bookkeeping walked silently (whole cycles skipped); `seq_locate_at` the same (Song Position: it moves the tape's head too). `events_block` and `keyboard_block` are their own functions, each with its own line in tests/target_budget.txt: inlined into the audio ISR's render loop, cold stubs moved before its head with every small change and read as loops.
- **MIDI LEARN** (1.3, from Felucca 1.5): MENU > MIDI > MIDI LEARN (OCT+) closes the MENU onto the page; a knob turned picks its parameter on the track it edits, the next CC heard is learned to it (`midi_learn.c`), OCT- clears the pick's, OCT+ is done; the footer's one line says what is picked and its CC, OCT+ DONE at the right. Ten entries of 3 bytes (code, CC, track) in engine 1's favourites row, bytes 2..31 (DIGITAL, retired: its stars only ever byte 0, still migrated); an engine parameter's code is 0xF8 + k, so moving P_E0 keeps them. A learned CC leaves the standard map. LEARN CLEAR forgets them; RESET SETTINGS too.
- **The MENU scrolls** (1.3): a tab of more than six rows (MIDI's ten) shows six, the cursor's row the last shown once past them, a thin bar at the right for where they are. Rows with no value that OCT+ acts on (`menu_items.c` `menu_act`): CALIBRATION, RESET SETTINGS, ABOUT, MIDI LEARN and LEARN CLEAR (its count before the chevron); the key hint says START / STOP, CLEAR or OPEN.
- **Projects:** the six tracks (SKF5 now, 2,560 bytes: see The tape, GRID and the sequencers). Older formats show OLD FORMAT.
- **Splash:** six blocks.
- **Cost** (SMK-37 build, with the tape): image 581,428 B of 620,544; RAM .data+.bss 87,372 of 94,208 B; the pool 349,564 of 360,448 B (`build.py`'s checks; SMK37.md).

**16 voices** (2.5; `core.h` `NVOICE`, `VOICES_1CORE`, `VOICES_2CORE`; `voice.c` `voice_victim`): each track has 16
voice slots and the tracks share 16 on two cores (8 on one, `VOICE_BUDGET`). The engines that cost more keep their
caps (GRAIN and PHYS 3, VOICE 4, FM6 6), UNISON and SLICE 8 (`UNI_NV`, `SLC_NV`, so SLICE's windows stay `[8][64]`),
the drums 8. A new note takes the quietest released voice first (`v_quieter`: the oldest was the rule), then as
before; a steal within the track restarts the voice in place, counted by the console (`voices_restarted`). EDIT ›
VOICE 2 shows 16. A reviewer's case (an ANALOG pad, 4-note chords every 2 s, a 3 s release): 2.2 on one core cut 25
tails, on two 18; 2.5 on two cuts none. To pay for it:
- **Every FM-1 on 2 CORES once** (`ui.c` `CPU2_DEFAULT`: the FM-1's 1, the SMK's and the browser's 0; `main.c`
  `cpu2_move` before the second core starts, `unit_g_boot` marking it done): 1 CORE is 2.2 exactly. RESET SETTINGS
  puts the default back.
- **A track's block runs from RAM on both cores** (`AUDIO_RAM`: `mix_part`, `c1_parts` and the per-block helpers:
  the matrix, SPREAD, DUCK, DRIVE, ENV 2, ENV SYNC, the lane pans); INSERT, the SLICER's steps, a user sample's mix,
  `lfo_inc` and `mod_depth` stay in flash (`AUDIO_FAR`). 16 voices had made the slowest block 86 % of a half buffer
  from flash; on the unit, the busiest project plays at about 46 %, its slowest block 2,202 µs of 2,902.
- **RAM:** undo 8 → 6 levels (3,304 B), the 8 KiB gap after the RAM code given to it (`app.ld` RAMTEXT 0x7F00), the
  RAM / pool border moved into .bss's spare (`build.py`'s limits with it).

**Flash room** (2.5): the FM-1's dev image had to stay under its 581,564 B slot with the console in.
- `tools/size_fns.py` `giant_helpers`: a main-loop helper called once, from one of the giant functions (`draw_graph`,
  `ui_input`, `ed_handle`, `ui_draw_page`, `fm_draw`, `edit_param`), gets `noinline`. Inlined there, its locals grew
  the giant's stack frame (`ed_handle` 1,948 B, `draw_graph` 1,680 B, `ui_input` 1,624 B: past the short offsets) and
  cost more than its call. −2,960 B on the FM-1, measured on IR variants: 121 helpers tried one by one and together,
  five smaller inlined (`KEEP_INLINE`), `fm1_main`'s larger out of line (left out). New helpers in those functions
  are out of line by the same rule.
- The FM-1's dev console is short (`console.c` `FELUCCA_CON_FULL`, the SMK's: `status`'s core lines, `memr`, `crash`,
  `uboot` on the FM-1; `dbg`, `inp`, `params`, `flr`, `uac` and `usb` on the SMK): about 2.7 KB. Releases have none.

**Timing, levels and the clock** (2.5's fixes):
- **#107, tracks of different DIVs drifting apart:** each DIV's step was truncated on its own (at 120 BPM a 1/16
  5,512 samples, a 1/8 11,025: a 1/16 track gained 2 samples a beat). The quarter is now a whole multiple of 24
  samples (`core.h` `bpm_quarter`, `QUARTER24`), so every DIV's step is a whole share of it; the click, the
  GRID, CLOCK OUT and LFO SYNC count the same grid. tests/drift_test.c: 1/8, 1/16 and 1/8T over 30 simulated minutes
  within a sample of the beat (fails on 2.2).
- **#110, distortion in fading notes:** the mix floored 2 bits before LEVEL's and MASTER's products, and MASTER
  lowered the level before the shift to 24 bits. Now one rounded cut a voice (`dsp.c` `voice_amp`, `voice_amp2`),
  rounded level products (`fx.c` `lvl_mul`), the master on Q22 down to the DAC (`master_out`, `softclip22`; the DC
  blocker and SPEAKER EQ's low cut at full precision), and USB's 16 bits dithered only while there is sound
  (`usb.c` `uac_dith`), so silence stays 0. tests/tail_test.c (H3 against MASTER and SPEAKER EQ). A reverb's own tail
  can still grow harmonics far down: #110 stays open for it.
- **#111, an external clock's tempo jumping:** it was read from each 16th (6 pulses, whole milliseconds, the
  timestamps late by up to a render): a swung clock read two tempos, a lost pulse about 110 BPM. Now a beat at a time
  (`midi_clock.c` `midi_clock_tempo`), the mean of the last 8 beats, a beat half a pulse or more off the mean passed
  over and two in a row taken as a new tempo (after SLOOP 2.7.2). midi_control_test's clock_tempo_test (±3 ms
  jitter, a lost pulse, a ramp, 60:40 swing).
- **#101, noise from INSERT's FLANG and CHOR on loud chords:** their line is kept at an eighth of the level (18 of 150
  presets' chords had clipped it), the feedback's knee as before.
- **TAKE:** a pass of the pattern that played before a switch is refused, not put into the one that came in.
- **The installer** (`tools/fm1_install.py`): it hung closing the MIDI ports of a device that had just restarted into
  its loader; those are now left as they are (`MidoLink.close(going)`), and the run ends with `os._exit`
  (tests/install_test.py).

**DRUM-X** (phase 7; engine 14, `eng_drumx.c`, `drumx_voice.c`, `ui_dx.c`): the drum engine of [JIANT FM](https://github.com/juanjiant-bit/JIANT) by juanjiant-bit (GPL-3.0-only), after the idea of Sonic Charge's Microtonic. It sits beside DRUM, which is unchanged: old sounds and projects play as before.
- **A synthesized kit:** the same 8 lanes as DRUM (KICK SNARE CLAP CL HAT OP HAT TOM RIM BELL; BD SD CP CH OH TM RS CB on the grid). Each is a tuned oscillator (SIN, FM, MTL or BEL) with a pitch envelope, plus noise through a filter (LP, BP, HP), each with its envelope. Every sound has two patches, A and B, of five values: PITCH, P MOD, DECAY, NOISE, COLOR.
- **A drum track as DRUM's:** the same keys (the GM drum map), grid, lane LEVELs (EDIT › LANES), the closed hat choking the open one. Its KICK lane is DUCK's source, as DRUM's.
- **EDIT 1 and EDIT 2** (KIT, HIT): MORPH (the morph from every sound's A to its B; KNOB 1 on HOME), PITCH, COLOR, DECAY, NOISE (each moves every lane from its patch; 64 is the kit as it is), FM, FOLD (a wavefolder after the envelope), DRIVE (U4: the words spelled out). They move a hit while it rings, and they can be automated and locked as any engine's.
- **The kit is the project's:** one kit for every DRUM-X track, saved with the project (SKF3: six tracks) and kept by the autosave; a project without one (older, or SKF2) gets the factory kit. Two DRUM-X tracks share its sounds and differ by their knobs. A user sound keeps the knobs, not the kit.
- **EDIT › SOUND, the sound sheet** (phase 8, after EDIT 2, DRUM-X tracks only; `ui_dx.c`; the user's pick of three mockups, `design/mock_p8.py` direction 2): one sound of the kit, all of it on one page.
  - **The sound:** a key struck on the track, or PRESETS (BD .. CB; no sound is loaded). The header names it: SOUND SD.
  - **The knobs** are a window on the sheet that SELECT moves, A beside B so there is no side to choose: PITCH A, PITCH B, P MOD A, P MOD B; then DECAY A, DECAY B, NOISE A, NOISE B; then COLOR A, COLOR B, LEVEL (the sound's lane level: automation as any knob), DRIVE; then the switches WAVE (SIN FM MTL BEL), FILTER (LP BP HP), SNAP, P SHAPE (the pitch modulation: DECAY LONG NOISE SINE). Past its top SELECT goes on to EDIT 2, past its foot to (2.0) LANE MIX, the family's last page (U3: VOICE and VOICE 2 change nothing on the hits, LANES is DRUM's: the sheet has LEVEL).
  - **The panel:** the 8 sounds, the one edited in the track's colour; then a row a value, A's bar in the track's colour and B's in white with their numbers, LEVEL and DRIVE, the switches in words; the knobs' rows on a band with a mark at the left, a scroll bar at the right. The footer shows the page number, as every page does (SELECT: ROWS is on SHIFT's map).
- **A kit edit isn't undoable,** as a knob turn isn't. **INIT SOUND** on a DRUM-X track puts the factory kit back too (the dialog says INITIALIZE SOUND? and the DRUM-X kit); SAVE held undoes both at once (`ui.c` `sound_init`, UNDO_KIT).
- Left out of JIANT's DRUM-X, for now: its group mutes, X-MOD (a tempo-synced LFO on MORPH), PUNCH and the dice. Features like these would come as one considered choice, not piecemeal. The SOUND pages' look may be revisited.

**FM6 LAB** (2026-10-13; `ui_fm6.c`; the user's pick of four mockups, `design/mock_fm6.py` direction D): every value of an FM6 patch on the unit, so a sound can be built from the bare sine and saved, without the web editor.
- **EDIT › OPERATOR and EDIT › ALL OPS** (after EDIT 2, FM6 tracks only): two sheets. PRESETS picks the operator (OP1 .. OP6, no sound is loaded; the header says OPERATOR 3), SELECT moves the knobs through five windows each, then on to the page beside it.
  - OPERATOR: TONE (OUTPUT, RATIO, FINE, DETUNE), ENV 1 (RATE 1, LEVEL 1, RATE 2, LEVEL 2), ENV 2 (3 and 4), KEYS (L CURVE, L DEPTH, R DEPTH, R CURVE: the keys below BREAK on the left, above it on the right, as the chart), MORE (BREAK, KEY RATE, VELOCITY, LFO AMP).
  - ALL OPS: ALGO (ALGO, FEEDBACK, TRANSP), PITCH 1 and PITCH 2 (the pitch envelope's rates and levels), LFO (WAVE, RATE in Hz, FADE, KEY SYNC), DEPTH (VIBRATO, VIB SENS, TREMOLO). OKS isn't there: the engine never reads it.
  - **RATIO** is one knob over x0.5, x1 .. x31, then the fixed frequencies 1, 10, 100, 1K HZ (MODE); FINE shows the ratio or the Hz it makes (x14.00); DETUNE ±7; the curves -LIN -EXP +EXP +LIN; BREAK a note (MIDI 21 + it: C4 at 39); PITCH in semitones.
  - **What plays:** ALGO and FEEDBACK show the patch through EDIT 1's ALGO and FEEDBACK, and a turn puts that into the patch with the macro back to neutral (PAT, 0). The other macros (MOD LVL, RATIO, MOD ENV, VEL MOD, DETUNE) stay offsets over several operators.
  - A knob edits a copy of the patch, then `fm6_set_patch` and `fm6_adopt`: SLOT turns OWN (or the factory patch it now equals), so PATCH never loads over an edit. Some values sound from the next note (OUTPUT, the ratios, the scaling); the rates and levels from the next stage. A patch edit isn't undoable, as a knob turn isn't.
- **The panel** (after an idea in a video by Floyd Steinberg, drawn our own way): every operator's envelope as the engine runs it (`fm6_op_env_init`; the macros through `fm6_macro`, as the audio's copy), a note of MIDI 60 at velocity 100, straight between its breakpoints on a log time axis (the held note, a dotted key up, the release), so a 1 ms attack and a 4 s decay both show.
  - **Brightness:** each carrier is a thick line, shaded along its length in 12 steps from cool blue (a sine) to REC's red (FM's bite): the strongest modulator feeding it at that moment, its level over its top 60 dB, square-rooted (a little FM is heard at once), the feedback counted as FB / 7 of its operator. (Steinberg averages the modulators; with six operators and several carriers an average dulls a bright pair.) The caption says where it bites most (BRIGHTEST 12 MS, a mark under it) or PURE SINE, the length, and the key: SINE, the ramp, FM. Colour is never the only cue.
  - The modulators are thin lines in their own colours (the palette's six), the one picked brighter; the picked operator's breakpoints are rings, the one the knob just turned filled. Tabs OP1 .. OP6 above, the picked one outlined; under each its line's colour (a carrier's the ramp); on TONE each with its ratio.
  - KEYS and MORE: the picked operator's level across the keyboard (MIDI 21 .. 108, 0 to -48 dB), BREAK a dotted line with its note, C4 a dot. ALGO: the algorithm chart with the picked operator lit, and what it does (OP4 MODULATES OP3, GOES TO THE OUTPUT, IS SILENT). PITCH: the pitch envelope over the note, the sound's outline behind. LFO and DEPTH: two seconds of vibrato from the key, FADE bringing it in, the tremolo under it.
  - The simulation runs only when what it draws changes (`fm_sig`), at most a few ms.
  - **In the themes of their own** (FM6 LAB T, 2026-10-13): CRYPT's fire (the ramp ash, blood, ember, gold, flame; square chunky strokes; skulls for handles, the one turned aflame; a flame where it bites; stone tabs, the picked one gold-framed; the ASCII shade under the sound; KEYS' BREAK a dagger; the vibrato a snake), NEON's night to sunset (cyan, violet, pink, orange, yellow; tubes and their glow; sparkles; a laser where it bites; the striped sun and the grid behind; glass tabs, a laser frame; the sun's bands under KEYS, PITCH and LFO; BREAK a laser), QUARK's energy (dim cyan, cyan, white, gold; the modulators a bubble chamber's dotted tracks in the colour charges; nuclei; a collision where it bites; a Monte Carlo fill under the sound; a cell's brackets on the tabs). ALGO's picked operator: a gold frame, a laser, brackets. SKIFF, HI-CON and PAPER as drawn above.
- **INIT SOUND** on an FM6 track is the bare sine to build from: `FM6_INIT` (OP1 alone, ALG 1), SLOT OWN, the macros at 0; SAVE held undoes it. An FM6 track on its own patch shows the patch's name (INIT VOICE) where the sound's name goes.
- **Saved as a sound** (SAVE › USER): the user sound keeps the whole patch, as before (`up_fm6.c`); a name typed for it goes into the patch's own name too (its first 10 letters), so a `.syx` from the web editor carries it. The automatic name and a factory patch (F n) leave it alone.

**Factory presets for the themes** (2026-10-11): ten sounds each for CRYPT (dungeon synth), NEON (synthwave) and QUARK (particles), added at the end of their engines' lists, so every earlier preset keeps its number.
- CRYPT: CRYPT CHOIR, MONK CHANT, CATHEDRAL, HARPSICHORD, BONE FLUTE, WAR HORN, GRAVE BELL, DARK STRINGS, WAR DRUMS, CAVE WIND.
- NEON: NIGHT BASS, NEON LEAD, SUNSET PAD, HERO BRASS, GLASS KEYS, TURBO SYNC, NEON ARP, DREAM CHOIR, NEON KIT, SYN TOM.
- QUARK: GEIGER, QUARK CLOUD, PHOTON, ISOTOPE, COLLIDER, QUANTA, NEUTRINO, PLASMA, HADRON KIT, DARK MATTER.
- **A preset's other values** (`core.h` `preset_t` `more`): besides its engine values, envelope, sends and voice mode, a factory preset may set up to four more of the sound's parameters: the LFO, GLIDE, ENV › PITCH, (phase 9) DRIVE's TYPE and TONE. Twelve of the thirty do (vibrato, PWM, pitch drops, S&H bleeps). Like every other sound parameter, they go back to their defaults when a preset without them is loaded. The track's own settings are never among them (`editor_test` checks every preset).

**Sounds and their types** (phase 9, 2026-10-15):
- **148 factory sounds**, at least 8 an engine. A table only grows at its end: a preset's number is in projects, favourites, the power-on sounds and the goldens. Names have 12 letters at most, each once among all the engines (a type's list mixes them; phase 9 renamed the older ones that weren't: ANALOG's PLUCK BRASS WIND, FM6's BELL BRASS MARIMBA ORGAN PLUCK, PHASE's BRASS ORGAN STRING BELL and DRUM-X's DRUM KIT, now OWN KIT; `ui_test` checks it), and no drum machine's or synth's trademark. Each new sound passed the audition page (the user's KEEP / CHANGE and a note) and regress's HEALTH, at the loudness of the sounds of its kind there were before.
- **A type each** (`core.h` `preset_t.tag`, `TAG(PT_*)`): BASS LEAD PAD KEYS PLUCK DRUM FX. SAVE › PRESETS' KNOB 4 is **LIST**: ALL, FAV, then the types. A type lists every engine's sounds of it, in the PRESETS list's order (`ENGINE_ORDER`); KNOB 1 and the PRESETS knob step through it wherever they browse; KNOB 2 (ENG) still steps the engines. User sounds are on ALL and FAV only. A light track leaves PHYS and GRAIN out of every list, as of ALL. The footer names the LIST beside the engine; an empty one says NO SOUNDS (a type) or NO FAVORITES.
- **The LIST is the unit's** (not the track's, not the project's): the settings byte `favorites.factory[15][21]` (0 none, else the type), kept below 8 at boot. `favorites.filter` stays 0 or 1, because older firmware refuses a backup whose filter is above 1; the LIST is the type + 1, else the filter.
- **DRUM's kits:** presets for its five model kits (80 10 66 55 77) and variations of the standard one, never on the retired KIT values 1..3 (a preset's values load as they are).
- **DRUM-X's kits** (`eng_drumx.c` `DX_KITS[]`, the factory kit first; `DRUMX_PKIT[]`, a kit + 1 per preset): such a preset loads its kit as the project's one kit, with the sound, as one undo (`UNDO_KIT`: SAVE held brings the kit back). Presets 0 and 1 keep the kit, because every engine switch loads preset 0, which would otherwise wipe the project's kit. `track_sig` hashes the kit, so an edit of it between two loads is an undo of its own.
- **SAMPLE's variations** (`tools/gen_samples.py` `SET_VARIANTS`, `MORE` too) follow PERC's slot; the placeholder there is a PIANO alias, hidden from browsing.
- **SLICE's loops** (phase 9, `assets/breaks/`): BREAK (one bar at 120 BPM, its 16ths on the grid) and FUNK (SOURCE 5, after PIANO: two beats at 86.33 BPM), cut from drum takes made with the YuE2 music model (`ATTRIBUTION.txt` there), mono at 22.05 kHz like every sample. Their AUTO slices are what the firmware's own detector finds (`gen_samples.py` `slc_onsets` runs `slc_scan`'s integer steps; `slice_test` checks them to the sample). FUNK BREAK plays FUNK's sixteenths. Flash: FUNK costs 15.3 KB.

**One mute** (2.0, D 1A, the user's pick on design/mockups/v2_mutes.png; `ui_layer.c` `mute_keys`, `perform.c` `perf_mute`): the black keys 1–6 in GLO, FX and PERFORM are the six tracks' MUTE and do the same in each. A tap (let go within MENU › HOLD) toggles it, latched and saved (MIXER's card, HOME and every map agree): GLO's and FX's at once, PERFORM's armed on the GRID (stopped: at once). Held, the track is cut while the key is: FX's and GLO's from the press, PERFORM's once it is a hold (so a tap there cuts nothing). Muted means silent now: a MUTE ramps the track to 0 in 2.9 ms in the mix, as a cut does (`fx.c` `mix_part`), and still refuses new notes; before 2.0 a MUTE only refused new notes and let the sounding ones ring out. FX LATCH latches the effects only. FX's map draws the black keys as GLO's (the speaker, the note, the number; lit while the track sounds), its footer WHITE: EFFECTS, BLACK: MUTE, K1-4: MACROS (latched: WHITE: ON / OFF, BLACK: MUTE, OCT-: ALL OFF).

**One way back** (2.0, D 2A, the user's pick on design/mockups/v2_mutes.png; `ui.c` `mom_*`, `throw.c`): MOMENTARY's values (LFO held + a knob), the FX layer's macros and PERFORM's throws come back the same way, set once in MENU › CONTROL › RETURN (after STEP PREVIEW; the editor's id 20): SNAP (at once, MOMENTARY's before 2.0), 1 BEAT, 1 BAR (the default, the throws' before 2.0), in a line. What starts it stays: LFO let go (or the menu, a dialog, NAME, a layer), FX let go (a locked FX layer: closed) and the keys pressed with it ((2.0.3, #69) while one is held its effect plays and the FX map shows: the macros stay), a PERFORM knob at rest (120 ms). OCT+ keeps them in all three while held: LFO held, FX held (or a key pressed with it), PERFORM open (OCT+ breathes then). A list's value (a TYPE, a WAVE, MIXER's MUTE) comes back at once, not through the values between; a value something else writes while it comes back (an edit, automation, a load) stays as written. FX's macros used to snap back when FX was let go; FX + OCT- (latched) and the menu still turn them off at once. The footer says it while something would come back (MOMENTARY's page, FX's map, PERFORM's): LET GO: BACK OVER 1 BAR (BACK OVER 1 BEAT, BACK), OCT+: KEEP, side by side. MOMENTARY's values and FX's macros share one list in the main loop (12 values: 8 LFO's, FX's 4); the throws keep their offsets in the audio interrupt, over what sounds (the sheet had them in the list too: there a lock or automation would wipe a throw at its next step), and only take RETURN's time (`thr_beats`, set each pass).

**The FX layer's map** (phase 9): fourteen effects, seven a row, an octave each (F3 .. E4 over F4 .. E5), in 30 px cells; the REPEATs' rate written 8, 16, 32 (the "1/" doesn't fit beside the key's letter). The four new ones, in the white keys after OCT DN (`perform.c`): GATE (the mix chopped on the sixteenths, open their first half; DEPTH how deep), DUB (the dry mix into the delay, its feedback up to 0.94 and its return at MIX's default while held; the echoes ring out after), FLANGE and VINYL (buffer effects: a jet sweep 0.3 .. 6 ms, a bar each way, fed back; a 0.555 Hz wow of ±0.4 %, a 4.5 kHz low-pass and crackle). Their icons, Fukiai's: a pulse, echoes trailing off, a comb, a record.

**QUANTIZE SOLO** (2.1, #74, kovacsarnold's request; `seq.c` `kb_pitch`, `kb_solo`): the white keys play as WHITE (C4 the root, a scale degree a key, CHRD's chords), the black keys single notes of the same scale, five to an octave: C# D# F# G# A# are 0..4, plus (note / 12 − 5) × 5, so C#4 is the root, F#3 G#3 A#3 the three degrees below it, F#5 seven above, and the SMK-37's C#3 D#3 G#5 A#5 carry the run on. One walk for both (`kb_degree`); the black keys an octave above the white ones' is a constant (`SOLO_OCT`, 0: the same octave). A black key is no chord's: it plays its note alone (`key_on`, step entry's `chord_make`); KBD HOLD doesn't hold it (the SMK-37's pedal does, and its release ends it), pressing one doesn't start a new chord, and one down doesn't make a white key join the held chord; with the ARP on it sounds directly (`input_on`, recorded as with the ARP off), its notes and the ARP's kept apart (`midi_local_held`): a note both play ends with the black key, not with the ARP's step or a white key let go (`kb_sol_held`). Each key keeps what it was pressed as (`kb_sol`), so QUANTIZE turned while it is held leaves nothing hanging. SCALE LEDS light the black keys too, the roots brighter. The stored value 5 is appended; firmware before 2.1 clamps it to SEQ+T (`param_fit`): the keys snapped.

**Chord follow** (2.1, SCL > FOLLOW, `follow.c`): three track settings, `P_FOLLOW` (OFF, T1..T6: the track whose chords it follows; never itself, as DUCK's BY), `P_FMODE` (KEY, SCALE, FIT) and `P_FNOTE` (ROOT, LOW, HIGH, LAST), kept by sound loads (`param_kept`), never automated (`motion_param`), stored ids 143..145 (`sid_p`: a parameter after the lane pans takes its P_* + 8). Every track keeps the chord it leads with (`fol_src`): its latest step's notes as they sound (`seq_step` → `fol_step`), or the keys and MIDI notes it holds (`input_on` / `input_off` → `fol_key`: a note with none held starts a new chord; QNT SOLO's black keys are none of it); a muted track leads, a kit doesn't; STOP and a panic clear it. Its root: the note with a fifth above it, then a third, then a flat fifth, a tie to the first from the lowest note up (`fol_root`). A following track's notes move as they start, at the voice (`trk_note_on` → `fol_on`): KEY by the chord's note's distance from its ROOT the nearest way (−5..+6), SCALE by as many steps of its own scale (`fol_shift`, `fol_walk` over SOLO's `kb_degree`), FIT to the chord's nearest note, a tie down. `fol_map` keeps the sounding note of each note played, so a note-off ends exactly it (`fol_off`; 255 while another note of the track sounds it too: FIT's two onto one), a retrigger where the chord has moved ends the old pitch first, and tied notes keep theirs; recording, TAKES and step entry keep the notes as played. The followed tracks tick first (`fol_order`, `events_block`), so on one step the chord comes before the note that follows it. The helpers are not inlined (`fm1_alnk0_irq` 7,997 of 8,441). User presets past 144 parameters are version 6 only: `UP_PUT` stages one directly and passes its pattern over (`up_parse`).

## LEDs

| LED | Meaning |
|---|---|
| Page button | Lit: the current page family. Breathing: its layer map is open. Glow: idle. |
| PLAY (green) | Playing |
| REC | Lit: a track is armed |
| OCT± | The octave. In a dialog, OCT+ breathes when it would act. |
| Keys | Notes sounding or held; the sequencer's and MIDI-in notes; the current scale (SCALE LEDS); a layer's choices; the step grid on drum pages |

## Visual tokens: BLOCKS (direction B, chosen 2026-10-07)

**Type:** Barlow Condensed, SIL OFL 1.1. Labels are lighter than the bold values.

| Face | Font | Use |
|---|---|---|
| S | Medium 13 px | Labels, units, lists |
| M | Bold 16 px | Names, the header, values too wide for V |
| V | Bold 24 px | Knob card values (BLOCKS): three figures and a unit fit a card |
| L | Bold 28 px | Big numbers |

**Palette SKIFF**: neutral, so the colour belongs to the tracks.

| Role | Colour |
|---|---|
| BG | #0C0C0C |
| SURF (tiles) | #1E1E1E |
| TEXT, THEME, ACCENT | white |
| REC | red |
| MID, DIM, LINE | derived greys |

**Track colours:**

| Track | Colour |
|---|---|
| 1 | vermilion #FF5A36 |
| 2 | teal #22C3B4 |
| 3 | amber #FFBF00 |
| 4 | violet #9580FF |
| 5 | pink #FF6FAE |
| 6 | lime #A3E635 |

The colours differ in lightness as well as hue, and always appear with the track's number. Other palettes give every track their THEME colour.

**BLOCKS**, the look (`firmware/src/ui_blocks.c`, `ui_home.c`). U2: the only one; FLAT and LINE and the MENU's STYLE are retired (a saved STYLE byte is left as it was):
- Square tiles on BG, with no dividers.
- **Header:** the transport on the left: the play state, which becomes the REC mark while a track is armed; the selected track's bar.beat (grey 1.1 when stopped); then "124 BPM". At the right, as the mockup's, the page's title in MID (HOME: the project's name; a layer its own: GLO SET, PERFORM BAR). The octave sits before the title only while shifted; when the title leaves too little room before the BPM (FM6's OPERATOR n), drop the "OCT" word and keep the signed number, or hide it rather than cover the BPM (#70). The battery after it at every level (charging on USB). A message starts right of the BPM, at x 106 or later, and takes the title's place for a while.
- **Knob cards:**
  - The label (S) sits top left.
  - The value is big (V), with its unit right-aligned on the same baseline. The value drops to M, then S, when it is too wide.
  - The gauge is a 3 px bar of the track's colour along the bottom edge.
  - The knob just turned is a whole accent tile, with everything on it in INK.
  - Card icons are dropped, except MOTION's mark. Values snap: no rolling digits.
- **Graphs** are drawn in the selected track's colour.
- **Dialogs** (`ui_blocks.c`):
  - A SURF tile, square, over the page dimmed, as the mockup's: the page drawn again with every colour 70 % of the way to BG (`confirm_backdrop`; the header stays itself, so REC shows).
  - The question, then what it is about: a name, or a sequence's steps and notes.
  - The detail and undo lines in the mockup's sentence case: 16 steps, 9 notes; hold SAVE: undo, or can't be undone in REC's colour (U4: the undo hint's words, in the explanations' sentence case). The second covers project and user-sound overwrites, a project load (U1: LOAD PROJECT B?, the project's name under it), erasing a user sound, and clearing an entry or a pattern. Names keep their capitals.
  - CANCEL (OCT−) is the filled button. The action (OCT+) is outlined in REC's colour and named by its verb: CLEAR, OVERWRITE, INITIALIZE, ERASE. Each key sits under its button.
- **TAKE box:** see TAKE in detail.
- **Footer:** one line, as the mockup's, with no step strip: the selected track's number, big in its colour, then its engine and sound; the page's title and n/m at the right. On HOME it says HOLD HOME: SHIFT; on an action page or a drum grid, the hints (OCT+: LOAD  OCT-: BACK; PAGE 1/2  KEYS: STEPS); on NOTES, PRESETS: STEP  KEYS: PICK (KEYS: WRITE with REC armed); on FEEL, PRESETS: STEP; on PATTERN, PRESETS: DIR.
- **Key names are words**, as the mockup writes them (`gfx.c` `kc_plain`): no keycaps. A hint reads KEY: WHAT (PLAY: RESTART, HOLD HOME: SHIFT); a key under a dialog's button is its name (OCT-); a state is a coloured word (the mixer's REC in red, MUTE in MID).
- **ENV:** three LINE guides behind the curve (its top, middle and foot) and its points as 5 px blocks (the peak, the decay's end, the release's start); the knob just turned has its point white.
- **One level of tiles:** SURF tiles on the black ground, the selection a white tile with INK. The layer maps' cells and the NAME screen's keys sit on the ground with no panel under them. Every shape is square, the "rounded in every style" pills included: the menu's tabs, its page dots, the COLOR swatches.
- **Menu:** ON and OFF are words like its other values (OFF in DIM), and there are no rules between its rows.
- **A footer short of room** for its hints keeps only the page's n/m at the right (the header names the page), so every hint keeps its word. An action card's value is its key's name, in M (LOAD: OCT+).
- **A page's own actions are cards** (U2, the review's G3): EDIT always means the EDIT pages. SAVE › USER's KNOB 3 picks ERASE and, one step further, NAME; SAVE › PROJECT's KNOB 2 picks DELETE and, one step further, NAME (1.2.1, #6), and its KNOB 3 is NEW on an empty slot (a blank project, the power-on one); AUTOMATION's VALUE turned past its lowest shows DELETE. OCT+ does the picked card, OCT− drops it.
- **The header names the page, the footer where it is** (U4, the review's L5): the header the page's name (EDIT: the engine's own, OSC, KIT), the footer at the right only its place in the family (1/4; with the page's icon), so a name is said once. NOTES' scale (C MIN, the footer's before) sits under the roll at its right.
- **No silent dead ends** (U4, F7): SELECT at a family's first or last page flashes the footer's n/m; OCT+ on an action page with nothing picked says PICK WITH A KNOB FIRST; a second layer's button says ONE LAYER AT A TIME; GLO's TAP counts the taps before the tempo (TAP 2 MORE); PRESETS on SLICES picks the slice (KNOB 1's).
- **MENU > SYSTEM > RESET SETTINGS** (U4, A3): its dialog on the page under the MENU, then every MENU setting as a new unit has it (Skiff's palette, BPM LOCK ON, CLOCK INT, MIDI IN CH1-6, TUNE 0, HINTS ON ..); the calibration, the favourites and the music stay.
- **One word for one thing** (U4, the review's L1–L3; `params.c` TP / GP, the engines' rows, the cards' own words): spelled out where the card has room (Barlow Condensed fits 8 letters), the vocabulary of the review's appendix: LEVEL, ATTACK DECAY SUSTAIN RELEASE, CUTOFF PITCH SHAPE (the ENV's and LFO's routings; (2.0) MOD's grid: CUT), PHASE, OCTAVE, SWING, CHANCE, ORDER, SCALE, QUANTIZE, TRANSP, LENGTH, DIST CHORUS DELAY REVERB, VOICE GLIDE GL MODE PRIORITY DETUNE, SLICER PATTERN, CHORD VOICING, CLOCK FEEDBACK COLOR ROUTE ENGINE; on the engines DETUNE, NOISE, CUTOFF, DRIVE, KEYTRK, CRUSH, SWEEP, VIBRATO, VOWEL, BREATH, RANDOM, PITCH (every semitone pitch: SAMPLE's and the drums' TUNE, VOICE's SHIFT, TRIO's PITCH2 PITCH3), SOURCE, DENSITY, SPREAD, STRUCT, BRIGHT, ACCENT, EXCITE, DRIFT, ALGO, MOD LVL, RATIO, MOD ENV, VEL MOD, PATCH (FM6's SLOT), MORPH, COLOR (DRUM-X's TONE, its sheet's COLR); the cards VELOCITY, CHANCE, RATCHET, SOURCE DEST AMOUNT (MOD). One name, one meaning (L2): DEST for a destination (DST was distortion too), RVRS for reverse (REV is reverb: SLICE's, the play direction's), PLAYED for the arp's order as played. The lanes CL HAT and OP HAT in full, BD SD CH OH on the grid. AUTOMATION's PARAM card shows ENV or LFO on its label's line (ENV ATTACK). The web editor's library files keyed by the labels of before (LVL ATK ..) still load: `legacyKeys` takes them to today's by id.
- **Messages say the way out** (U4, the review's F3, L6): a refusal names what to do, on the header's second line where it needs one (`ui.c` `ui_way`): AUTOMATION FULL, then AUTOMATION: DELETE ONE; BPM LOCKED, then GLO + SELECT: TEMPO; PLAY ONE LOOP FIRST, DISARM REC FOR A TAKE, NOTHING BEFORE TO JOIN, STOP TO ERASE / RENAME / SAVE as it is. Musicians' words for developers': CLOCK IS EXTERNAL, SLICES NOW MANUAL, PICK A USR SAMPLE FIRST, SAVED UNTIL POWER OFF (no flash), STORAGE ERROR, UPDATE MODE, KEYS RECORD LANES, HOLD [GLO]: SHORTCUTS, UNDONE: SEQUENCE / MIX / SCALE / REC, TAPE 2 CLEARED, SOUND INITIALIZED. After a crash the session is not restored on purpose, and the boot says SESSION NOT RESTORED. MIDI IN into a track not selected marks its number on HOME for 0.4 s (was a header message every 4 s, covering the title). Values show up to 7 letters (SPRING, MELLOW; were cut at 5), the card falling to M, then S, where V is too wide. AUTOMATION names a lane as its kit plays it (CONGA). The REC layer says CLICK, COUNT-IN, GRID, FEEL (the click's level stays MENU > AUDIO's CLICK LEVEL); GLOBAL's swing is SWING+ (it adds to PATTERN's SWG).
- **Every page has a graph of its own** (U5, the review's V1; `ui_pgraph.c`, design/mock_u5.py): (2.0, D 1A, design/mockups/v2_mod.png) MOD the seven fixed routings as a grid, the LFO and the ENV a row each, PITCH CUT SHAPE AMP a column (the ENV's AMP the envelope itself: -), the route picked white, over the matrix's four slots as routes 8 to 11 (ENV DEST's and LFO DEST's own graphs went with their pages); DELAY the dry hit and an echo every TIME, the first MIX high, each next FEEDBACK lower (`fx.c`), over the bar's beats; REVERB the first reflections and the tail SIZE long, its top dimmed by DAMP, SPRING's coil; CHORUS the dry line and its drifting copy (one modulated delay), (phase 9) FEEDBACK its copy's copy fainter behind, FLANGER and PHASER their response across the spectrum (the comb, the two notches) drawn as FLT's curve; REVERB's HALL the tail twice ROOM's; FX › DRIVE (phase 9) the TYPE's transfer curve at the DIST, the straight line behind it, and one sine cycle through it as `fx.c` runs it (CRUSH's hold, TONE's low-pass in it); GLOBAL a bar's 16 steps, the off steps late by SWING+ over where they would be, the long:short share; VOICE what the mode does and the GLIDE between two notes; VOICE 2 the engine's voices, those sounding lit, the next one outlined (POLY, ROT), DETUNE's UNI spread; ANALOG's OSC the wave and its detuned second, FLT the low pass's curve, its peak at CUTOFF as high as RES; ARP and ARP 2 one graph, the arp's notes over 16 steps from the notes held (else C E G, said), GATE a tile's length, SWING, the steps CHANCE skips outlined. Of a page's curves the knob just turned (else the biggest amount) is the track's colour, the others MID, a zero one DIM. The other engines' EDIT pages keep their charts (FM6, WHEEL, SAMPLE) or the output's scope; FM6's OPERATOR and ALL OPS draw the note as the engine plays it (FM6 LAB, below).
- **The page families** (U3, the review's structure; `params.c` `PAGES`, `ui.c` `page_visible`):
  - FX holds the track's effects (FX, (phase 9) DRIVE: DIST TYPE TONE, SLICER, DUCK). (2.0, D 2A, the user's pick on design/mockups/v2_mod.png) DRIVE's TYPEs are INSERT's shapers (`fx.c` `ins_shape`, one curve for both, in DIST's own low cut, tone and make-up): SOFT (with DIST's bias, as before), HARD, FOLD, CRUSH (BITS by its old name, the same sound), FUZZ (new, last: a saved TYPE stays). HARD and FOLD sound a little different from 1.x's: the ceiling 24000 (was 24576), the folds x 1.5 (were x 2). The buses every track shares are GLO's, after MIXER: DELAY, REVERB (TYPE SIZE DAMP; TYPE ROOM SPRING, phase 9 HALL), CHORUS (RATE DEPTH, were CRT CDP; phase 9 MODE FEEDBACK), then GLOBAL (BPM, SWING). Its footer counts the five as one (MIXER 1/5).
  - (Phase 9) ENV's third page, ENV 2 (ATTACK DECAY SUSTAIN RELEASE, the ENV page's graph), is a second envelope for the MOD matrix only (source ENV2; `voice.c` `env2_tick`, ticked only while a slot uses it).
  - SEQ goes by task: (2.0) NOTES, FEEL, PATTERN, AUTOMATION, TAPE last (STEP, ROLL, CHANCE, PLAY before). On a drum track NOTES and FEEL are the grid.
  - AUTOMATION is 1.1.5's AUTO LIST: the records, and a first row of its own (ROW ALL) with PLAY on KNOB 2 and CLEAR on KNOB 4. The AUTOMATION page of before, its counts and scope, is gone.
  - EDIT > LANES (DRUM) is one page: KNOB 1 the lane (the grid's too), KNOB 2 its LEVEL, (phase 9) KNOB 3 its TUNE (±12 semitones, 0.0 as the kit has it), KNOB 4 its DECAY (a signed offset, 0 as the kit has it), the eight as faders; a held step's KNOB 2 3 4 lock that lane's. DRUM-X has its sheet's LEVEL instead (and tunes its sounds there).
  - (2.0, #36, the user's pick 1A of design/mockups/v2_feats.png) EDIT > LANE MIX, DRUM's and DRUM-X's (after LANES, after SOUND): KNOB 1 the lane (DRUM: LANES' and the grid's; DRUM-X: the sheet's; a key struck picks its lane, the same key again too: `ui.c` `lane_follow`, `seq.c` `last_hits`), KNOB 2 its LEVEL, KNOB 3 its PAN (-64 .. 63 as the track's, the card -31 %); a held step's KNOB 2 3 lock them. The panel (`ui_graph.c` `graph_lmix`): LANES' faders 20 px shorter (`lane_fader`, its themes' too), under each its pan, a row 20 px wide, C its middle, the pan a 5 x 7 mark (CRYPT a skull on a dotted leader, NEON a spark on a glass line, QUARK a nucleus on its energy levels) and written as the MIXER writes it (C, L31, R100: `pan_txt`), then the name's tile 2 px lower than LANES'.
  - (#21) EDIT > SAMPLES (a SAMPLE track on a USR set with samples, after EDIT 2): KNOB 1 the sample (its number; a key played picks its sample), KNOB 2 its LEVEL (square law as LANES', 100 % the sample as uploaded), KNOB 3 and 4 its REVERB and DELAY, added to the track's sends (127 at most in all; `fx.c` `smx_render`: before DRIVE and the INSERT, after the track's LEVEL, mute, SLICER and DUCK); the panel its waveform, as SET's. Kept with the slot (`slice_store.c`: after SLICE's record, for that slot header only) once the page is left with the music stopped (MIX SAVED); never a lock or automation. The editor's Samples tab and `fm1_sample_upload.py mix` set the same values (cmd 81 SMP_MIX).
  - (2.0, #35, the user's pick A of design/mockups/v2_kits.png) DRUM's KIT goes on to **USR1–USR3**, a user sample slot as the kit (`eng_drum.c` `drum_user`, `drum_smp`): each lane plays the slot's narrowest zone holding its key (`drum_user_zone`, the lanes' GM notes), the sample as recorded whatever the zone's root; the kit's and LANES' TUNE and DECAY (shorter only), LANES' LEVEL, LANE MIX's PAN, the open hat cut by the closed one, the grid, DUCK's KICK and DRIVE as on a synthesized kit; TONE under its middle a low-pass. A lane without a zone is silent, its fader and name dim (`ui_graph.c` `lane_fader_of`); the sound's name is the slot's (MY KIT) unless a user sound is loaded. The editor's Samples tab builds such a slot (Drum kit: a WAV a lane, cut to where the sound is, each with a level; one gain for the kit, so the files keep their levels to each other: `kitZones`).
  - A page that changes nothing on the track's engine is not there: ENV, ENV 2, VOICE, VOICE 2 and SCALE on DRUM and DRUM-X (`engine_t.oneshot`), ENV and ENV 2 on FM6 (`ownenv`); VOICE 2 is ALLOC, DTUNE and (1.3) SPREAD (PAN, MUTE: MIXER's). A card that does nothing there is dimmed: (2.0) MOD's routes the LFO to PITCH CUT SHAPE on the hits, the ENV's there and on FM6 (`mod_dead`; ENV DEST was hidden there), DRUM's KICK on a model kit, (1.3) LFO's RATE while LFO 2 SYNC is on, SPREAD on MONO and LEGATO. A button with no page says so (NO ENV ON DRUM); a page a track no longer has goes to its family's first (or HOME) and says so too.
  - Every family reopens the page it was left on (SAVE and GLO too), and the MENU closes back onto the page it was opened over.
  - SAVE › TOOLS is gone (CLEAR is the REC and SEQ TOOLS layers', INIT the EDIT layer's), and so is GLO › SYSTEM: CLOCK and MIDI IN are MENU › CONTROL's ((1.3) MENU › MIDI's), TUNE MENU › AUDIO's, CPU (read only) MENU › SYSTEM's. (Skiff 1.3) CPU picks 1 CORE or 2 CORES (BETA until 2.5, the FM-1's default since: 16 voices below; from the next power-on, RESTART until then), then the load: the second core renders tracks (fx.c mix_block: the split by the last block's cost, the sums per core, bit for bit as one core; tests/dualcore_test.c), and the voice budget is 12 instead of 8 while it runs ((2.5) 16, core.h VOICE_BUDGET). A core that misreads or stops answering is held: one core, CORE 2 FAILED, the row back to 1 CORE. (Skiff) CPU CLOCK, after CPU (SYSTEM's seven rows scroll): 360 MHZ (the boot stage's, the default) or 320 MHZ, JieLi's rated clock, switched at boot from RAM with the IRQs off, the cache and the flash controller retimed around the PLL change (curist's hal/fm1_clk320.h, hal/fm1_clk.h, main.c), the screen up, before the second core, the audio and USB; from the next power-on (RESTART, STOP while playing, as CPU). Clock registers not as the boot stage leaves them: not switched, the header says NO 320 MHZ and why, the row REFUSED; a reset during the switch is no failed boot, and no switch is tried again until power-off; a soft reset keeps the PLL, and a kept 320 is not switched again. ABOUT shows the clock (CPU 320 MHZ), the console's `status` cpu_mhz, clk320 and the loop-measured clk320_loop_mhz.
  - CLOCK, MIDI IN and TUNE are the unit's (`ui.c` `unit_g_*`, settings bytes [15][18..20]): a project still stores them (for older firmware) but a load leaves them; the first boot with U3 takes the restored session's.
- **Sentence case** for the lines that explain, as the mockup's dialog and TAKE box: recording starts after 1, list ALL to add sounds, teach each button and knob. Titles, labels, values and key hints stay in capitals.
- **Track numbers** (`icons.c` `cv_trk`): no cushions. The number stands alone in its track's colour, as on the HOME lanes: V where Felucca's 16 px cushion went (footer, MIXER, the GLO layer's SOLO keys), M where its 12 px one went (the GLO cards, the layers' mute keys). It's INK on a light tile or a held key, and DIM for a silent track. The TAKE box's chip, the number on its colour, is the one square, as in the mockup.

**Defaults:** a new unit gets SKIFF. **THEME** (the review's R1; MENU > THEME, was COLOR; `tools/gen_ui_palettes.py`): SKIFF, HI-CON, PAPER, CRYPT, NEON, QUARK. A unit on HI-CON or PAPER keeps it; on any other palette of before (GREY, GREEN, AMBER, ICE, VIOLET, ROSE, NIGHT, MONO, Felucca's twenty) it starts on SKIFF. CRYPT (`gfx.c` `ux.crypt`) brings its own font and widgets too (the theming phases C2-C4); NEON (`ux.neon`) CRYPT's
font and its own widgets (N1-N8); QUARK (`ux.quark`) Skiff's Barlow faces and its own widgets (Q1-Q5).

**THEME CUSTOM** (#27, @tradiuz; after Felucca 1.6's CUSTOM palette; design/mock_c27.py, mockups/c27_sheet.png): the
user's eleven colours from the web editor (BG SURF TEXT THEME ACCENT and the six tracks'), in a look they pick: a
built-in THEME's faces and widgets (`gfx.c` `ux_cpal` over `palette_set`, `ui.c` `theme_apply`). THEME's last value
while a set is stored. The other tokens come from the five as for every palette, and each look's motifs are drawn in
them (CRYPT's skulls, stone and pixel letters, NEON's chrome, bands and high score table, QUARK's brackets, fills and
collisions); a motif's own art keeps its colours (the torch's flame, the sun's bands). The SMK-37's pads take the
tracks' colours as the screen does. **PLAYHEAD** (#27; MENU › DISPLAY, under THEME): THEME, the figure at HOME's
playhead on the selected lane (CRYPT's torch, NEON's car, QUARK's collision), or LINE, the playhead's line only.

**CRYPT** (THEME; design/mock_crypt.py B TORCHLIT with A's ASCII graphs and the motifs the user asked for: skulls, snakes, torches, swords; `ui_crypt.c`):
- **Letters:** our own 5×7 pixel glyphs (`tools/gen_pixel_font.py`), every pixel full or empty: labels 1×, names a 1× bold, card values 2×, MENU > LARGE's 3×; capitals only (lower case folds to them). Each face keeps its Barlow face's line height and centres its capitals on Barlow's, so every layout holds; `gfx.c`'s AF_S AF_M AF_L AF_V and CAP_IN follow the theme.
- **Stone and frames:** stone (a speckled tile, `gfx.c` `cr_stone`) wherever BLOCKS lays a SURF tile (`cv_rrect`, so every cell, list and box follows): the graph panel, cards, HOME's lanes, the MIXER's strips, the layer maps' and SHIFT's cells, the MENU's rows, the dialog (a tablet); the ASCII double line round the panel, the MENU and the dialog. The hot card is lit, framed in amber; the MENU's selected row and tabs are square.
- **The motifs:** a card's gauge is a sword (the hilt, the blade as long as the value, its edge gold; MENU › LARGE's tall cards too); REC is a blood drop (another track armed: its outline); the header ends in a castle wall's crenels; HOME's playhead lines are gold with a torch (twice the size, in a dark outline, after the hardware test) on the selected track's; the MIXER's levels are torches (ember at the foot, gold at the top, a flame on it; muted: out, its mark a crossed skull); the splash a skull between two torches.
- **ASCII steps:** HOME's step cells and the drum grid's hits are characters: # a note or a hit (the drum grid's accents bright), - a tie, . nothing.
- **The MENU as an old book's contents:** a dotted leader from each name to its value (AUTOMATION's rows too).
- **The SEQ pages:** PATTERN's steps ASCII (# - .); the piano roll's notes swords (a gold crossguard where the note
  starts, the blade as long as it lasts, its edge bright), its cursor and playhead gold; the step tiles plaques, their
  border engraved (the edited one's gold); the tape's lanes stone.
- **The graphs as ASCII art** (`cr_art`: a level a 6 px character column, joined by 2 px strokes, _ / \ _| as the curve moves): ENV with skulls on its three points over a ':' shade; LFO a snake along its path up to where the LFO is now; DELAY # columns under skulls, the dry hit under a torch; REVERB's tail in shade characters # * + : ., its top blood (DAMP), SPRING's coil ~; CHORUS's copy a snake; GLOBAL # columns, ^ where an off step would be; VOICE's glide timed by a sword; VOICE 2's voices skulls, the next one in a gold double frame; OSC's wave with blood in its teeth; FLT's curve with a skull on its peak and a dagger at the cutoff; ARP's notes #, a skipped one a crossed skull; (phase 9) DRIVE's curve a snake, its head at the end, the straight line faint behind it, its sine as OSC's wave, blood in its teeth; FLANGER's and PHASER's response as FLT's curve over its shade, a skull on the middle peak; CHORUS's FEEDBACK copy MID art behind the snake; HALL as ROOM's tail. FX's sends and DRUM LANES' levels are the MIXER's torches; SLICER's steps # columns (a GATE step ':' as high as it stays open, STUT '='); WHEEL's drawbars # columns as far out as they are; DUCK the level as ASCII art over the shade, the source's hits ^ under it. The ':' shade is DIM (LINE vanished into the stone's specks).

**The LFO's graphs** (every theme; LFO, LFO 2; `ui_graph.c` `lfo_view_at`): the LFO over 2 s from its retrigger (a phrase's first note), as it runs: RATE's cycles in it, PHASE where it starts, FADE rising from nothing; a dot (CRYPT: the snake's head) where it is now, crossing the whole graph in the 2 s; past them the window scrolls, the last 2 s. (The hardware test: RATE and FADE did nothing to the old two fixed cycles.)

**NEON** (THEME, synthwave; design/mock_neon.py A SUNSET, the user's pick, with all four motifs: the car, the sun and
the grid, VHS snow, the high score table; `ui_neon.c`):
- **Letters:** CRYPT's pixel faces. The values in chrome, slanted (`gfx.c` `cv_fx_on`: a px right every 3 rows above the
  baseline, a ramp down the capitals: white, sky blue, violet at the horizon, orange to gold); the header's BPM and the
  track numbers slanted. Next to a unit the value's top passes over the unit's (its gap at the unit's height).
- **Glass and dusk:** every SURF cell glass (its top edge lit violet); the hot card a dusk (gold, orange, pink) in a
  white frame; a card's gauge an 80s dash bargraph (slanted segments, cyan, violet, pink as they climb), MENU › LARGE's
  too. A neon rule (pink, violet, cyan) under the header and over the footer; REC a red light, glowing.
- **HOME:** a highway: the selected lane a neon sign (its colour into violet), the others asphalt with the lane's paint
  between them; a step a lit tube, nothing a dim dash; the playhead a cyan laser and on the selected lane the car (24 x 8,
  after the hardware test: 2x hid too many steps) in a dark outline, its nose at the playhead, its light trails back over
  the steps just played.
- **MIXER, PERFORM:** VU columns (cyan, violet, pink, gold), LEVEL's cap white; a muted track no signal: VHS snow (MUTE;
  selected: dark snow on its band).
- **The scope** (HOME tapped on HOME; an engine's EDIT without a chart): the sound as the skyline: the wave a ridge of
  vector mountains over the horizon, the striped sun behind it swelling with how loud it is, the grid floor running
  toward the viewer. CRYPT's (2.5, after the user's Ossuary synth): six creatures in a row, a track's each, always the
  same (1 a skull, 2 a ghost, 3 a candle, 4 a witch, 5 a hooded monk, 6 a demon), bone in tiny ASCII ramp glyphs
  (`ui_crypt.c` `cr_creature`, their art in `tools/gen_sprites.py`), the track's number under each: a track playing
  surges out of the dark (its glyphs up the ramp), its eyes burning in its colour, the candle's flame flickering, fuller
  the louder; loud, its rows slip sideways and its glyphs break; silent, a faint outline; muted, its eyes crossed out
  (MENU › LARGE's strip: the fire of before, the wave burning up from the foot). SKIFF's (2.5, `ux.skiff`): the logo's
  sea (design/logo/make_logo.py): 18 columns of rounded cells, the tracks' colours in bands (track 1 at the left), each
  band's swell as high as its track is loud and rolling on, a muted or silent track's band grey; the logo's skiff
  sailing across over and over, on the highest crest under it.
- **MENU:** an arcade's high score table: ranks 1ST..6TH, slanted, in the tracks' colours; dotted leaders; values cyan;
  the selected row and tab a sunset bar. The dialog: night glass in a double neon frame, the page under it paused like a
  tape (every other row darker); CANCEL lit cyan, the action red neon. The splash: the striped sun between palms, the
  grid floor, the car.
- **The graphs:** every curve glows (`cv_line_t`: a ring out of it on the background only). ENV over the sun's bands, its
  points sparkles; LFO a ridge of vector mountains, a white-hot trail into a sparkle where it is now; DELAY's echoes on the
  grid toward a dim sun, ghosting wider; FLT and DUCK over the sun's bands, and (phase 9) DRIVE's curve, FLANGER's and PHASER's
  response and REVERB's tail (its reflections lit tubes, DAMP's share violet), sparkles at DRIVE's ends and at the
  chorus copy's, a ghost behind it its FEEDBACK; GLOBAL, FX's sends, LANES and WHEEL VU columns;
  SLICER, the piano roll's notes, the drum grid's hits and the step tiles lit tubes; the cursor a cyan laser.
- **The SEQ pages:** PATTERN's steps lit tubes, the cursor a laser under them; the roll's playhead a laser; a list's
  selected row a sunset bar (AUTOMATION, the presets), AUTOMATION's values cyan on dotted leaders; the tape's lanes glass,
  the target's lane a dusk.

**QUARK** (THEME, particle physics; design/mock_quark.py A COLLIDER, the user's pick, with all four motifs: beams and
collisions, dark matter, the hazard sign, the polar scope; `ui_quark.c`, its circles on `sine_i`):
- **The colour charges:** the six tracks red, green, blue and their anticolours cyan, magenta, yellow; values and curves
  cyan, the one active thing gold. Skiff's Barlow faces.
- **The cells:** a card is the periodic table's cell: bracket corners, the knob's number top right where the label leaves
  room, the gauge energy levels (a dot every 4 px); the hot cell cyan. The panel in an instrument's bracket corners;
  dotted rules under the header and over the footer; REC a red nucleus.
- **HOME:** the lanes beam pipes (the selected one a beam, its colour faint, its walls lit; the others two rails); a note
  a bunch of particles, spent (a ring) once played this bar, a tie its trail; the playhead the beam line, dotted gold, and
  on the selected lane a collision whose tracks fly out a new way each step.
- **MIXER, PERFORM:** the level a calorimeter tower (its cells the energy deposited, a gold cap); a muted track dark
  matter: a dotted, empty tower (DARK), a dotted outline round its row.
- **MENU:** an instrument's settings: the values readouts in brackets on dotted leaders, the row a cyan tint in corners,
  its value gold. The dialog: gold corners, what can't be undone under the radiation sign, CANCEL cyan, the action in
  red brackets. The splash: an atom in a collision's tracks.
- **The graphs:** ENV a Monte Carlo fill under the curve, its points nuclei, DECAY's and RELEASE's half-lives written
  on it; LFO the orbit and its projection (the electron's height drawn out as the wave, dashed to where it is now);
  DELAY a decay chain; FLT and DUCK Monte Carlo fills, and (phase 9) DRIVE's curve (a nucleus on its top, the electron at
  its sine's end), FLANGER's and PHASER's response and REVERB's tail (its reflections towers, DAMP's share sparse);
  CHORUS's copy ends in the electron, its FEEDBACK copy dotted; GLOBAL, FX's sends, LANES, WHEEL and SLICER calorimeter towers;
  the roll's notes bubble tracks from their vertex (its cursor in gold corners); the drum grid's hits and PATTERN's steps
  bunches. The scope (HOME tapped on HOME; 1.3.3, Q6): the 1.2 tour video's event display, live: each note a track
  plays a collision at the vertex, three particles flying out in the track's colour and curling in the field; (2.5)
  they live as long as the note sounds: lit while it plays, fading with its release (a hit's as it rings out), gone
  with its voice (a sounding voice an event; UNISON's and MONO's one); round the beam pipe's rings the calorimeter,
  eight towers a track, as loud as the track is; the vertex gold, brighter just after a hit. (It replaced Q5's wave
  wrapped round the detector.)

**The web editor looks like the unit** (1.0; `design/editor_looks.png` A BLOCKS, the user's pick over a softer app
look): the night, each section a tile, white values, Barlow Condensed for headings, labels and values, each track its
colour (the track buttons, the mixer's strips, the Tape tab's lanes), the sliders and the steps in the selected track's.
Once connected it follows the unit's THEME: SKIFF and HI-CON dark, PAPER light (cream tiles, ink), CRYPT (stone, bone,
amber headings), NEON (glass, pink headings glowing) or QUARK (a detector's dark, cyan headings on a dotted rule), each
with its tracks' colours; before that, the system's light or dark (`web/editor.html`, `data-theme`; colours only as
tokens). (#27) In THEME CUSTOM the editor's tokens are the set's colours and its look's faces stay (`customTokens`);
Settings › Device display edits the set: eleven colour wells, Look, Start from… (a THEME's own colours), shown on the
device as they change (RAM only), Save, Revert, Remove, and a warning for colours hard to read (nothing is refused).

**The web editor reaches what the device has** (W, the review's W1–W7; `web/editor.html`, `web/EDITOR_PROTOCOL.md`):
a step's FLAM, COND and NUDGE and its locks; the MENU settings (Settings); the mixer's REV; the DRUM-X kit (a DRUM-X
track's Sound tab); PLAY, STOP and REC in the header; the projects' names (SAVE AS, RENAME); the Tape tab: the four
lanes of notes, the target, MUTE, CLEAR and MERGE, the length, OVERDUB or REPLACE, the head, each track's RUN. Each acts through the device's own code and the device says so on its screen; what can't be undone is
asked first, as on the device. Nothing of these is pushed: the editor reads what it shows again about once a second.
**Screen grid:** Felucca's, so every page inherits the restyle (`firmware/src/ui.c`); BLOCKS' footer is one line, so its panel is 16 px taller (`foot_less`, `graph_h`):

| Region | Size |
|---|---|
| Header | 24 px |
| Cards | 4 × 57×44 at y 28 |
| Panel | y 76–198 (BLOCKS: 76–214: ADSR and LFO grow into it, the fixed layouts sit centred) |
| Footer | y 202–240 (BLOCKS: 218–240, its line where the names' line was) |

**HOME overview** (`firmware/src/ui_home.c`):

| Element | Spec |
|---|---|
| Lanes | 31 px tiles with 3 px between them (the mockup's) |
| Number | V, in the track's colour; INK on the selected track's band |
| Name | The sound (M) over its engine (S), from x 28 |
| Step cells | 16 cells of 6×16 px from x 100, a 2 px gap, 2 px more between beats; an empty step is a LINE block |
| Playhead | A 1 px line before the step playing |
| MENU › LARGE (2.5, #97, the user's 8C) | Under the tall cards the strip names the six sounds, two columns of three, the selected one a band of its colour, each theme's lanes as HOME's (`ui_graph.c` `graph_names`); HOME tapped: the scope, as before. Redrawn on a change (`home_sig`), not every other frame as the scope. Before, LARGE showed the scope there and the footer named the selected sound only: a reporter lost the instrument list |

**Still open:**
- the 20 MHz LCD clock, `LCD_BAUD=2`, to try on hardware.
