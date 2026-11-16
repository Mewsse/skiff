/* SPDX-License-Identifier: GPL-3.0-only */
/* The global GRID's lines (src/grid.c) and the tape's clock (src/seq.c tape_clock), through the same sources as the
 * firmware (hostsim.c): every grid's floor / next / window over two bars at SWING 0, 50 and 100; 1,000 bars at 120,
 * 133 and 240 BPM end exact; a tempo change mid-beat never moves the clock back; an external clock's pulses are 20
 * ticks each; Song Position 300; PLAY from a head snaps it to its bar; STOP and GLO + PLAY; (P2) the arm table: SEQ RUN
 * and MUTE land on the next step */
#define main hostsim_main
#include "hostsim.c"
#undef main

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

int main(void)
{
    uint32_t fails = 0;
    fails += test_lines();
    fails += test_swing();
    fails += test_off();
    fails += test_window();
    fails += test_transport();
    fails += test_bars();
    fails += test_tempo();
    fails += test_external();
    fails += test_arm();
    fails += test_arm_grid();
    printf(fails ? "grid: %u FAILED\n" : "grid: all passed\n", (unsigned)fails);
    return fails ? 1 : 0;
}
