/* SPDX-License-Identifier: GPL-3.0-only */
/* The global GRID's lines (src/grid.c) and the tape's clock (src/seq.c tape_clock), through the same sources as the
 * firmware (hostsim.c): every grid's floor / next / window over two bars at SWING 0, 50 and 100; 1,000 bars at 120,
 * 133 and 240 BPM end exact; a tempo change mid-beat never moves the clock back; an external clock's pulses are 20
 * ticks each; Song Position 300; PLAY from a head snaps it to its bar; STOP and GLO + PLAY; (P2) the arm table: SEQ RUN
 * and MUTE land on the next step */
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint8_t unit_settings[32 * 16];                /* (the unit's settings, as ui.c's mset_io: GRID at [15][26]) */
static const uint8_t *const mset_io = unit_settings;

#define TWO_BARS (2u * TAPE_BAR)

static int check(const char *what, int ok)
{
    printf("grid: %-100s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static int test_lines(void)
{
    static const uint32_t SWINGS[3] = {0, 50, 100};
    uint32_t idx, sw, tick, bad = 0;
    for (idx = 1; idx < GRID_N; idx++)
        for (sw = 0; sw < 3u; sw++) {
            uint32_t ticks = grid_ticks(idx), swing = SWINGS[sw], line = 0;
            for (tick = 0; tick < TWO_BARS; tick++) {
                uint32_t floor_at, next_at;
                while (grid_line(idx, swing, line + 1u) <= tick)
                    line++;
                floor_at = grid_floor(idx, swing, tick);
                next_at = grid_next(idx, swing, tick);
                bad += floor_at != grid_line(idx, swing, line) || next_at != grid_line(idx, swing, line + 1u);
                bad += floor_at > tick || next_at <= tick;
            }
            bad += grid_line(idx, swing, 0) != 0 || grid_line(idx, swing, TAPE_BAR / ticks) != TAPE_BAR;
        }
    return check("floor / next: the line at or before, the first after, for the 9 grids at SWING 0 / 50 / 100", !bad);
}

static int test_swing(void)
{
    static const uint8_t SWUNG[GRID_N] = {0, 0, 0, 0, 1, 1, 1, 0, 0, 0};
    uint32_t idx, bad = 0;
    for (idx = 1; idx < GRID_N; idx++) {
        uint32_t ticks = grid_ticks(idx), shift = 100u * ticks / 250u;
        bad += grid_line(idx, 100, 1) != ticks + (SWUNG[idx] ? shift : 0u);
        bad += grid_line(idx, 100, 2) != 2u * ticks;
        bad += grid_line(idx, 0, 1) != ticks;
        bad += grid_line(idx, 1000, 1) != grid_line(idx, 100, 1);
    }
    bad += grid_ticks(4) != 240u || grid_line(4, 50, 1) != 240u + 48u;
    return check("swing: odd lines of 1/8, 1/16, 1/32 later by swing * ticks / 250, the others and even lines not", !bad);
}

static int test_off(void)
{
    uint32_t tick, bad = 0;
    for (tick = 0; tick < 3000u; tick += 7u)
        bad += grid_floor(0, 50, tick) != tick || grid_next(0, 50, tick) != tick;
    bad += grid_window(0, 120) != 0 || grid_ticks(0) != 0 || grid_ticks(GRID_N) != 0;
    return check("OFF: no line, a note stays where it was played, no window", !bad);
}

static int test_window(void)
{
    uint32_t bad = 0;
    bad += grid_window(1, 120) != 19u || grid_window(3, 120) != 19u || grid_window(5, 120) != 15u;
    bad += grid_window(6, 120) != 7u || grid_window(9, 120) != 10u || grid_window(8, 120) != 19u;
    bad += grid_window(1, 133) != 21u || grid_window(1, 240) != 38u || grid_window(6, 240) != 7u;
    bad += grid_window(1, 40) != 6u;
    return check("late window: min(grid / 8, 20 ms) in ticks at 40, 120, 133, 240 BPM", !bad);
}

static void fresh(uint32_t bpm)
{
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(&midi_clock, 0, sizeof midi_clock);
    host_tracks_init();
    song.g[G_BPM] = (int16_t)bpm;
    transport_req = 0;
    midi_beat_samples = 0;
    seq_stop();
    tape_head = song_ticks = tape_beat = tape_frac = tape_pass = 0;
    tape_len = 0;
}

static int test_bars(void)
{
    static const uint16_t BPM[3] = {120, 133, 240};
    uint32_t i, bad = 0;
    char what[160];
    for (i = 0; i < 3u; i++) {
        uint32_t blocks = 0, last = 0, wrong = 0;
        fresh(BPM[i]);
        transport_req = 1;
        while (tape_beat < 4000u || !blocks) {
            events_block(CTL);
            blocks++;
            wrong += song_ticks > tape_beat * TAPE_TPQ + TAPE_TPQ - 1u || song_ticks + TAPE_TPQ < tape_beat * TAPE_TPQ;
            wrong += song_ticks < last || tape_head != song_ticks % (4u * TAPE_BAR);
            wrong += tape_pass != song_ticks / (4u * TAPE_BAR);
            last = song_ticks;
        }
        events_block(CTL);
        snprintf(what, sizeof what, "%u BPM: 1,000 bars (%u blocks): head, passes and ticks exact, the beat's 4,000th starts the 1,920,000th tick",
                 (unsigned)BPM[i], (unsigned)blocks);
        bad += check(what, !wrong && song_ticks >= 1920000u && song_ticks < 1920000u + TAPE_TPQ && tape_beat == 4000u);
    }
    transport_req = 2;
    events_block(CTL);
    return bad;
}

static int test_tempo(void)
{
    uint32_t blk, last = 0, wrong = 0, beats0, moved = 0;
    fresh(120);
    transport_req = 1;
    for (blk = 0; blk < 60000u; blk++) {
        if (blk % 97u == 0)
            song.g[G_BPM] = (int16_t)(40u + (blk * 31u) % 200u);   /* mid-beat, up and down */
        events_block(CTL);
        wrong += song_ticks < last || tape_head != song_ticks % (4u * TAPE_BAR);
        wrong += song_ticks > tape_beat * TAPE_TPQ + TAPE_TPQ;
        moved += song_ticks > last;
        last = song_ticks;
    }
    beats0 = tape_beat;
    transport_req = 2;
    events_block(CTL);
    return check("tempo changes mid-beat (40..239 BPM every 97 blocks): ticks never go back, head = ticks mod length", !wrong && moved && beats0 > 100u);
}

static void ext_blocks(uint32_t pulses, uint32_t *wrong, uint32_t from)
{
    uint32_t pulse;
    for (pulse = from; pulse < pulses; pulse++) {
        fm1_ms = 1000u + pulse * 500u / 24u;                   /* 120 BPM */
        midi_enqueue(0x0Fu | 0xF8u << 8, 1);
        events_block(CTL);
        events_block(CTL);
        *wrong += song_ticks != 20u * (pulse - 1u);   /* (the first pulse only starts the count) */
    }
}

static int test_external(void)
{
    uint32_t wrong = 0;
    fresh(120);
    song.g[G_CLOCK] = 1;
    fm1_ms = 1000;
    events_block(CTL);
    midi_enqueue(0x0Fu | 0xFAu << 8, 1);
    events_block(CTL);
    ext_blocks(1000u, &wrong, 1u);
    return check("external clock: 24 PPQN pulses are 20 ticks each (1,000 pulses), head = ticks mod length",
                 !wrong && song.playing && tape_head == song_ticks % (4u * TAPE_BAR) && song_ticks == 20u * 998u);
}

static int test_transport(void)
{
    int bad = 0;
    fresh(120);
    tape_head = 5000;
    transport_req = 1;
    events_block(CTL);
    bad += check("PLAY from head 5000: the head snaps to its bar, 3840", song.playing && tape_head == 3840u && tape_beat == 8u);
    transport_req = 2;
    events_block(CTL);
    bad += check("STOP keeps the head (3840)", !song.playing && tape_head == 3840u);
    transport_req = 2;
    events_block(CTL);
    bad += check("STOP when stopped: the head to 0", !song.playing && tape_head == 0u);
    tape_head = 3000;
    transport_req = 1;
    events_block(CTL);
    transport_req = 3;
    events_block(CTL);
    bad += check("GLO + PLAY: from the top, head 0, beat 0", song.playing && tape_head == 0u && tape_beat == 0u);
    seq_locate(300);
    bad += check("SPP 300: 36,000 ticks, head 36,000 mod 7,680, beat 75, passes 4",
                 song_ticks == 36000u && tape_head == 36000u % (4u * TAPE_BAR) && tape_beat == 75u && tape_frac == 0u &&
                 tape_pass == 0u);
    seq_locate(301);
    bad += check("SPP 301: 120 ticks into the beat", song_ticks == 36120u && tape_beat == 75u && tape_frac == 120u);
    events_block(CTL);
    bad += check("the clock goes on from the locate", song_ticks >= 36120u && song_ticks < 36120u + 20u);
    seq_locate(0);
    bad += check("SPP 0: the top", song_ticks == 0u && tape_head == 0u && tape_beat == 0u);
    transport_req = 2;
    events_block(CTL);
    return bad;
}

/* while the tape runs its head and the shared clock are on the same place in the bar (the GRID reads song_ticks) */
static uint32_t phase_wrong;
static void phase_blocks(uint32_t blocks)
{
    uint32_t blk;
    for (blk = 0; blk < blocks; blk++) {
        events_block(CTL);
        phase_wrong += tape_go && song_ticks % TAPE_BAR != tape_head % TAPE_BAR;
    }
}
static int test_phase_invariant(void)
{
    uint32_t ran;
    fresh(120);
    phase_wrong = 0;
    tape_head = 5000;
    transport_req = 1;
    phase_blocks(3000);
    ran = tape_go;
    seq_locate(301);
    phase_wrong += song_ticks % TAPE_BAR != tape_head % TAPE_BAR;
    phase_blocks(3000);
    tape_set_len(3);
    phase_blocks(3000);
    transport_req = 2;
    events_block(CTL);
    ran &= tape_bar_step(1) && tape_bar_step(1);
    transport_req = 1;
    phase_blocks(3000);
    ran &= tape_go;
    transport_req = 2;
    events_block(CTL);
    tape_len = 0;
    return check("the tape running: head = clock mod a bar across PLAY, SPP, a length change, bar steps", ran && !phase_wrong);
}

/* the arm table (launch.c): an op armed on step k lands at the boundary into step k + 1 */
static uint32_t arm_land(track_t *t, uint32_t op, uint32_t *from)
{
    uint32_t k = trk_index(t), blk;
    *from = t->seq_idx;
    arm[k].at = song_ticks;
    arm[k].op = (uint8_t)op;
    for (blk = 0; blk < 4000u && arm[k].op; blk++)
        events_block(CTL);
    return arm[k].op == ARM_NONE;
}
static int test_arm(void)
{
    track_t *t = &trk[0];
    uint32_t i, blk, from, sounded = 0;
    int bad = 0, ok;
    fresh(120);
    for (i = 0; i < NSTEP; i++)
        t->step[i] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    t->p[P_SLEN] = 16;
    transport_req = 1;
    for (blk = 0; blk < 4000u && t->seq_idx != 5u; blk++)
        events_block(CTL);
    ok = arm_land(t, ARM_RUN_OFF, &from) && !seq_running(0) && t->seq_idx == from + 1u && !t->seq_n;
    for (blk = 0; blk < 2000u; blk++) {
        events_block(CTL);
        sounded += t->seq_n != 0u;
    }
    bad += check("SEQ RUN off armed on step k: lands on step k + 1, its notes end; no step plays while off", ok && !sounded);
    ok = arm_land(t, ARM_RUN_ON, &from) && seq_running(0) && t->seq_idx == (from + 1u) % 16u && t->seq_n;
    bad += check("SEQ RUN on armed: lands on the next step, which plays", ok);
    ok = !t->p[P_MUTE] && arm_land(t, ARM_MUTE, &from) && t->p[P_MUTE] == 1 && t->seq_idx == (from + 1u) % 16u;
    ok &= arm_land(t, ARM_UNMUTE, &from) && !t->p[P_MUTE] && t->seq_idx == (from + 1u) % 16u;
    bad += check("MUTE, UNMUTE armed: each lands on the next step", ok);
    transport_req = 2;
    events_block(CTL);
    return bad;
}

/* GRID: a key armed mid-bar lands on its line, the first step boundary on or after it (never before) */
static uint32_t grid_land(uint32_t grid, uint32_t op, uint32_t *line_ticks)
{
    uint32_t blk, line;
    *line_ticks = 0;
    arm[0].at = arm_when(grid);
    line = arm[0].at;
    arm[0].op = (uint8_t)op;
    for (blk = 0; blk < 4000u && arm[0].op; blk++)
        events_block(CTL);
    *line_ticks = song_ticks - line;
    return arm[0].op == ARM_NONE;
}
static int test_arm_grid(void)
{
    track_t *t = &trk[0];
    uint32_t i, blk, late, head0;
    int bad = 0, ok;
    fresh(120);
    for (i = 0; i < NSTEP; i++)
        t->step[i] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    t->p[P_SLEN] = 16;
    transport_req = 1;
    for (blk = 0; blk < 4000u && tape_head < 500u; blk++)
        events_block(CTL);
    head0 = tape_head;
    ok = head0 >= 500u && head0 < 700u && arm_when(0) == song_ticks;
    ok &= arm_when(1) == song_ticks + TAPE_BAR - head0 && arm_when(3) == song_ticks + 960u - head0 &&
          arm_when(5) == song_ticks + 600u - head0;
    bad += check("arm_when: OFF now, 1 BAR / 1/4 / 1/16 the next line after the head", ok);
    ok = grid_land(1, ARM_RUN_OFF, &late) && late < 200u && !seq_running(0) && tape_head % TAPE_BAR < 200u;
    bad += check("GRID 1 BAR: RUN OFF armed mid-bar lands on the bar line (the first step boundary from it)", ok);
    for (blk = 0; blk < 4000u && (tape_head % TAPE_BAR < 300u || tape_head % TAPE_BAR > 400u); blk++)
        events_block(CTL);
    ok = grid_land(3, ARM_RUN_ON, &late) && late < 200u && seq_running(0) && tape_head % 480u < 200u;
    bad += check("GRID 1/4: RUN ON armed lands on the next beat line", ok);
    transport_req = 2;
    events_block(CTL);
    return bad;
}

/* the ARP's first key on the GRID (seq.c arp_wait): past the line's window it waits for the next line, within it, or
 * with GRID OFF, it starts at once */
static uint32_t arp_press(uint32_t grid, uint32_t offset, uint32_t *waited, uint32_t *start)
{
    track_t *t = &trk[0];
    uint32_t blk, press, base;
    fresh(120);
    unit_settings[32 * 15 + 26] = (uint8_t)grid;
    t->p[P_AMODE] = AM_UP;
    t->p[P_APROB] = 127;
    transport_req = 1;
    for (blk = 0; blk < 4000u && (tape_head < 960u || tape_head % TAPE_TPQ < offset); blk++)
        events_block(CTL);
    press = song_ticks;
    base = tape_head - tape_head % TAPE_TPQ;
    arp_add(t, 60);
    events_block(CTL);
    *waited = t->arp_pos == ARP_WAIT;
    for (blk = 0; blk < 4000u && !t->arp_note; blk++)
        events_block(CTL);
    *start = song_ticks - press;
    press = tape_head - base;
    transport_req = 2;
    events_block(CTL);
    return t->arp_note != 0 ? press : 0u;
}
static int test_arp(void)
{
    uint32_t waited, start, at, bad = 0;
    at = arp_press(3, 100, &waited, &start);
    bad += check("ARP, GRID 1/4: a key 100 ticks after a beat waits, its first note on the next beat",
                 waited && at >= TAPE_TPQ && at < TAPE_TPQ + 40u && start >= 330u && start < 400u);
    at = arp_press(3, 8, &waited, &start);
    bad += check("ARP, GRID 1/4: a key 8 ticks after a beat (in the window) starts now", !waited && start < 12u);
    at = arp_press(0, 100, &waited, &start);
    bad += check("ARP, GRID OFF: a key 100 ticks after a beat: no wait, the ARP's own 1/16 line 20 ticks on, as before", !waited && start >= 15u && start < 30u);
    at = arp_press(1, 600, &waited, &start);
    bad += check("ARP, GRID 1 BAR: a key past the bar line waits for the next bar", waited && at >= 0u && start > 1000u);
    return bad;
}

/* the live-play delay (seq.c live_hold / live_tick): GRID 1/4 at 120 BPM, the late window 19 ticks */
static int sounds(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].gate && t->v[i].note == note)
            return 1;
    return 0;
}
static void live_begin(uint32_t grid, uint32_t offset)
{
    uint32_t blk;
    fresh(120);
    unit_settings[32 * 15 + 26] = (uint8_t)grid;
    transport_req = 1;
    for (blk = 0; blk < 4000u && (tape_head < 960u || tape_head % TAPE_TPQ < offset); blk++)
        events_block(CTL);
}
static uint32_t live_until(const track_t *t, uint32_t note, int on)
{
    uint32_t blk;
    for (blk = 0; blk < 4000u && sounds(t, note) != on; blk++)
        events_block(CTL);
    return song_ticks;
}
static void live_end(void)
{
    transport_req = 2;
    events_block(CTL);
}
static int test_live(void)
{
    track_t *t = &trk[0];
    uint32_t press, line, at, off, blk, i, bad = 0;
    int ok;

    live_begin(3, 100);
    press = song_ticks;
    line = press + TAPE_TPQ - tape_head % TAPE_TPQ;
    input_on(t, 60, 100);
    ok = live_n == 1u && !sounds(t, 60) && !t->hits;
    at = live_until(t, 60, 1);
    bad += check("live, GRID 1/4: a note 100 ticks after a beat waits, sounds on the next beat", ok && at >= line && at < line + 3u && !live_n);
    live_end();

    live_begin(3, 8);
    input_on(t, 60, 100);
    bad += check("live, GRID 1/4: a note 8 ticks after a beat (in the window) sounds now", !live_n && sounds(t, 60) && t->hits == 1u);
    live_end();

    live_begin(3, 100);
    press = song_ticks;
    input_on(t, 60, 100);
    for (blk = 0; blk < 4000u && song_ticks < press + 60u; blk++)
        events_block(CTL);
    input_off(t, 60);
    ok = live_n == 1u && !sounds(t, 60);
    at = live_until(t, 60, 1);
    off = live_until(t, 60, 0);
    bad += check("live: pressed and let go before the line keeps its length (60 ticks)", ok && off - at >= 57u && off - at <= 63u && !live_n);
    live_end();

    live_begin(3, 100);
    for (i = 0; i < 16u; i++)
        input_on(t, 40u + i, 100);
    ok = live_n == 16u && !t->hits;
    input_on(t, 80, 100);
    bad += check("live: queue full (16): the next note sounds now, the 16 stay", ok && live_n == 16u && sounds(t, 80));
    live_end();

    live_begin(3, 100);
    input_on(t, 60, 100);
    ok = live_n == 1u;
    live_end();
    for (blk = 0; blk < 800u; blk++)
        events_block(CTL);
    bad += check("live: STOP drops what waits, it never sounds", ok && !live_n && !t->hits);

    live_begin(3, 100);
    input_on(t, 60, 100);
    input_on(trk + 1, 62, 100);
    panic_req = 1u;
    events_block(CTL);
    ok = live_n == 1u && live_q[0].trk == 1u;
    bad += check("live: panic of a track drops its waiting notes, not another's", ok);
    live_end();

    live_begin(3, 100);
    input_on(t, 60, 100);
    input_on(t, 64, 100);
    ok = live_n == 2u && !t->hits;
    unit_settings[32 * 15 + 26] = 0;
    events_block(CTL);
    bad += check("live: GRID set OFF flushes what waits, in order played", ok && !live_n && t->hits == 2u && last_note == 64u &&
                 sounds(t, 60) && sounds(t, 64));
    live_end();

    live_begin(3, 100);
    trk[1].p[P_FOLLOW] = 1;
    input_on(t, 60, 100);
    ok = fol_src[0].held == 0u;
    live_until(t, 60, 1);
    bad += check("live: a chord's followers hear it when the note sounds, not before", ok && fol_src[0].held == 1u);
    live_end();

    live_begin(3, 100);
    t->p[P_AMODE] = AM_UP;
    input_on(t, 60, 100);
    bad += check("live: with the ARP on a note is the ARP's (waits in arp_wait), not queued", !live_n && t->nheld == 1u);
    live_end();

    live_begin(0, 100);
    input_on(t, 60, 100);
    bad += check("live, GRID OFF: a note sounds at once, nothing queued", !live_n && sounds(t, 60) && t->hits == 1u);
    live_end();
    return bad;
}

/* the tape's playback (tape.c through events_block): one bar long, notes at 100 (60, 100 ticks) and 1000 (62, 100) */
static uint32_t rise[2][8], rises[2];
static void tape_fresh(uint32_t bpm)
{
    fresh(bpm);
    tape_init();
    tape_set_len(1);
    tape_insert(0, 100u | 60u << 20, 100u | 100u << 20, TAPE_ANY_PASS);
    tape_insert(0, 1000u | 62u << 20, 100u | 100u << 20, TAPE_ANY_PASS);
    rises[0] = rises[1] = 0;
}
static void tape_run(uint32_t until_ticks, uint32_t bpm_at, uint32_t bpm_to)
{
    static const uint8_t NOTES[2] = {60, 62};
    uint32_t blk, k, was[2] = {0, 0};
    for (blk = 0; blk < 40000u && song_ticks < until_ticks; blk++) {
        events_block(CTL);
        if (bpm_to && song_ticks >= bpm_at)
            song.g[G_BPM] = (int16_t)bpm_to;
        for (k = 0; k < 2u; k++) {
            uint32_t now = sounds(&trk[0], NOTES[k]);
            if (now && !was[k] && rises[k] < 8u)
                rise[k][rises[k]++] = tape_head + tape_pass * tape_length();
            was[k] = now;
        }
    }
}
static int near_at(uint32_t got, uint32_t want, uint32_t slack) { return got + slack >= want && got <= want + slack; }
static int test_tape_play(void)
{
    uint32_t bad = 0, pass, ok;
    tape_fresh(120);
    transport_req = 1;
    tape_run(4u * TAPE_BAR, 0, 0);
    ok = rises[0] == 4u && rises[1] == 4u;
    for (pass = 0; ok && pass < 4u; pass++)
        ok = near_at(rise[0][pass], pass * TAPE_BAR + 100u, 4u) && near_at(rise[1][pass], pass * TAPE_BAR + 1000u, 4u);
    bad += check("TAPE: a 1 bar tape plays its notes at their ticks across 3 loop wraps", ok);
    transport_req = 2;
    events_block(CTL);
    tape_fresh(120);
    transport_req = 1;
    tape_run(3u * TAPE_BAR / 2u, 2000u, 240);
    tape_run(5u * TAPE_BAR, 0, 0);
    ok = rises[0] >= 4u && rises[1] >= 4u && near_at(rise[0][2], 2u * TAPE_BAR + 100u, 8u) && near_at(rise[1][2], 2u * TAPE_BAR + 1000u, 8u);
    bad += check("TAPE: a tempo change in the middle of a pass: the next passes land on their ticks (the head counts beats)", ok);
    transport_req = 2;
    events_block(CTL);
    tape_fresh(120);
    transport_req = 1;
    tape_run(2u * TAPE_TPQ, 0, 0);
    seq_locate(24);
    rises[0] = rises[1] = 0;
    tape_run(song_ticks + 1500u, 0, 0);
    ok = rises[1] >= 1u && near_at(rise[1][0] % TAPE_BAR, 1000u, 8u) && (!rises[0] || rise[0][0] >= TAPE_BAR);
    bad += check("TAPE: SPP to the 24th 16th (head 960): the note at 1000 sounds, the one at 100 waits for the next pass", ok);
    transport_req = 2;
    events_block(CTL);
    return bad;
}
static int test_tape_stop(void)
{
    uint32_t bad = 0, blk;
    tape_fresh(120);
    tape_insert(1, 500u | 64u << 20, 1500u | 100u << 20, TAPE_ANY_PASS);
    transport_req = 1;
    for (blk = 0; blk < 4000u && !sounds(&trk[0], 64); blk++)
        events_block(CTL);
    bad += check("TAPE: a long note sounds", sounds(&trk[0], 64));
    transport_req = 2;
    events_block(CTL);
    bad += check("TAPE: STOP ends every tape voice", !sounds(&trk[0], 64));
    return bad;
}

int main(void)
{
    uint32_t fails = 0;
    fails += test_lines();
    fails += test_swing();
    fails += test_off();
    fails += test_window();
    fails += test_transport();
    fails += test_phase_invariant();
    fails += test_bars();
    fails += test_tempo();
    fails += test_external();
    fails += test_arm();
    fails += test_arm_grid();
    fails += test_arp();
    fails += test_live();
    fails += test_tape_play();
    fails += test_tape_stop();
    printf(fails ? "grid: %u FAILED\n" : "grid: all passed\n", (unsigned)fails);
    return fails ? 1 : 0;
}
