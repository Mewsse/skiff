/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff P11) the note tape for the editor (EDITOR_PROTOCOL.md "Tape"); INFO 5A 01 caps (1). Included by editor.c.
 * Every edit is the unit's own (tape.c, the main loop), as SEQ > TAPE does it.
 *   TAPE: [op, ...] -> op, rc (0 done, 1 refused), then the state. No op: the state. Ops: 1 MERGE [into, from] (a track
 *     with notes, never itself), 2 CLEAR [track], 3 MUTE [track, 0/1], 4 LENGTH [bars lo, hi] (1..256), 5 LOCATE [bar
 *     lo, hi] (the head to a bar line, stopped only), 6 TARGET [track], 7 RUN [instrument, 0/1] (the step sequencer's
 *     RUN, armed as PERFORM's key while playing), 8 OVERDUB [0/1] (0: replace)
 *   state: bars lo, hi, the head's ticks (3 bytes), target, overdub, mutes (a bit a track), run (a bit an instrument),
 *     the notes the tape holds at most (lo, hi), then each track's notes (lo, hi)
 *   TAPE_GET: [track, first lo, hi, count 1..32] -> track, first lo, hi, notes in the track (lo, hi), n, then n x
 *     (at 3 bytes, length 3 bytes, note, instrument, velocity): the notes in tick order, a note being recorded not
 *     among them; the ones past the tape's length are (they stay, unplayed) */
enum { ED_TAPE = 84, ED_TAPE_GET };
enum { EDT_MERGE = 1, EDT_CLEAR, EDT_MUTE, EDT_LENGTH, EDT_LOCATE, EDT_TARGET, EDT_RUN, EDT_OVERDUB };
#define ED_TAPE_PAGE 32u

static int ed_tape_args_ok(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if (cmd == ED_TAPE_GET)
        return n == 4u && a[0] < TAPE_TRACKS && a[3] >= 1u && a[3] <= ED_TAPE_PAGE;
    if (!n)
        return 1;
    switch (a[0]) {
    case EDT_MERGE:    return n == 3u && a[1] < TAPE_TRACKS && a[2] < TAPE_TRACKS && a[1] != a[2];
    case EDT_CLEAR:
    case EDT_TARGET:   return n == 2u && a[1] < TAPE_TRACKS;
    case EDT_MUTE:     return n == 3u && a[1] < TAPE_TRACKS && a[2] <= 1u;
    case EDT_LENGTH:   return n == 3u && (uint32_t)(a[1] | a[2] << 7) >= 1u && (uint32_t)(a[1] | a[2] << 7) <= TAPE_BARS_MAX;
    case EDT_LOCATE:   return n == 3u;
    case EDT_RUN:      return n == 3u && a[1] < NTRK && a[2] <= 1u;
    case EDT_OVERDUB:  return n == 2u && a[1] <= 1u;
    default:           return 0;
    }
}

/* the RUN bit asked for, as PERFORM's key arms it (ui_layer.c arm_set): stopped at once, playing at the next GRID line */
static void ed_tape_run(uint32_t part, uint32_t want)
{
    arm_t *slot = &arm[part];
    uint32_t op = want ? ARM_RUN_ON : ARM_RUN_OFF, now = slot->op == ARM_RUN_ON ? 1u : slot->op == ARM_RUN_OFF ? 0u : (uint32_t)seq_running(part);
    if (now == want)
        return;
    if (slot->op != ARM_NONE && slot->op <= ARM_RUN_OFF) {
        slot->op = ARM_NONE;
        return;
    }
    if (!song.playing) {
        fm1_irq_off();
        arm_do(&trk[part], op);
        fm1_irq_on();
        return;
    }
    slot->op = ARM_NONE;
    slot->at = arm_when(ui_grid_idx());
    RING_PUBLISH();
    slot->op = (uint8_t)op;
}

static int ed_tape_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t rc = 0, bars, mutes = 0, run = 0, track, notes, at;
    if (cmd == ED_TAPE_GET) {
        uint32_t first = a[1] | a[2] << 7, total = 0, want, sent = 0, guard;
        uint16_t node;
        track = a[0];
        tape_sweep();
        for (guard = 0, node = tp_trk[track].first; node < TAPE_N && guard < TAPE_N; guard++, node = tp_nx[node])
            total += tp_saved(node);
        ed_b(track); ed_b(first); ed_b(first >> 7); ed_b(total); ed_b(total >> 7);
        want = total > first ? total - first : 0u;
        if (want > a[3])
            want = a[3];
        ed_b(want);
        for (guard = 0, node = tp_trk[track].first, notes = 0; node < TAPE_N && guard < TAPE_N && sent < want;
             guard++, node = tp_nx[node]) {
            uint32_t w0 = tp_w0[node], w1 = tp_w1[node];
            if (!tp_saved(node) || notes++ < first)
                continue;
            at = TP_AT(w0);
            ed_b(at); ed_b(at >> 7); ed_b(at >> 14);
            ed_b(TP_LEN(w1)); ed_b(TP_LEN(w1) >> 7); ed_b(TP_LEN(w1) >> 14);
            ed_b(TP_NOTE(w0)); ed_b(TP_INST(w0)); ed_b(TP_VEL(w1));
            sent++;
        }
        return 1;
    }
    if (cmd != ED_TAPE)
        return 0;
    if (n) {
        uint32_t op = a[0];
        track = a[1];
        if (op == EDT_MERGE) {
            if (tp_trk[a[2]].count) { tape_merge(track, a[2]); ui.force = 1; } else rc = 1;
        } else if (op == EDT_CLEAR) {
            tape_clear(track);
        } else if (op == EDT_MUTE) {
            tp_trk[track].mute = (uint8_t)a[2];
        } else if (op == EDT_LENGTH) {
            tape_set_len(a[1] | a[2] << 7);
        } else if (op == EDT_LOCATE) {
            bars = tape_length() / TAPE_BAR;
            if (song.playing || seq_counting() || (a[1] | a[2] << 7) >= bars)
                rc = 1;
            else
                tape_head = (a[1] | a[2] << 7) * TAPE_BAR;
        } else if (op == EDT_TARGET) {
            tp_target = (uint8_t)track;
            if (ui.tsrc && (uint32_t)ui.tsrc - 1u == track)
                ui.tsrc = 0;
        } else if (op == EDT_RUN) {
            ed_tape_run(track, a[2]);
        } else {
            tp_overdub = (uint8_t)a[1];
        }
        ui.force = 1;
    }
    ed_b(n ? a[0] : 0u);
    ed_b(rc);
    bars = tape_length() / TAPE_BAR;
    ed_b(bars); ed_b(bars >> 7);
    ed_b(tape_head); ed_b(tape_head >> 7); ed_b(tape_head >> 14);
    ed_b(tp_target);
    ed_b(tp_overdub != 0u);
    for (track = 0; track < TAPE_TRACKS; track++)
        mutes |= (uint32_t)(tp_trk[track].mute != 0u) << track;
    for (track = 0; track < NTRK; track++)
        run |= (uint32_t)seq_running(track) << track;
    ed_b(mutes);
    ed_b(run);
    ed_b(TAPE_N); ed_b(TAPE_N >> 7);
    for (track = 0; track < TAPE_TRACKS; track++) {
        ed_b(tp_trk[track].count);
        ed_b(tp_trk[track].count >> 7);
    }
    return 1;
}
