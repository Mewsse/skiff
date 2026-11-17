/* SPDX-License-Identifier: GPL-3.0-only */
/* REC onto the note tape (voice.c's hooks, seq.c tape_rec, tape.c tape_poll) through the same sources as the firmware
 * (hostsim.c): live notes stamped at the head with the GRID OFF, on its lines with the GRID on; the sequencer's and the
 * ARP's notes recorded once over overdub passes; replace mode; a note held over the loop's end; the record queue's
 * overflow; the count-in's kept notes at the start; a MUTE instrument records nothing; no note left open or sounding
 * after STOP, a panic or an engine switch while recording */
#define main hostsim_main
#include "hostsim.c"
#undef main
#undef inst

static uint8_t unit_settings[32 * 16];                /* (the unit's settings, as ui.c's mset_io: GRID at [15][26]) */
static const uint8_t *const mset_io = unit_settings;

static int check(const char *what, int ok)
{
    printf("taperec: %-104s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void fresh(uint32_t bars, uint32_t grid)
{
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(&midi_clock, 0, sizeof midi_clock);
    host_tracks_init();
    song.g[G_BPM] = 120;
    transport_req = tape_req = tape_arm = 0;
    midi_beat_samples = 0;
    cin_bars = 0;
    seq_stop();
    tape_head = song_ticks = tape_beat = tape_frac = tape_pass = 0;
    tape_len = 0;
    tape_init();
    tape_set_len(bars);
    unit_settings[32 * 15 + 26] = (uint8_t)grid;
    tape_poll();
}

static void block(void)
{
    events_block(CTL);
    tape_poll();
}

/* blocks until song_ticks reaches ticks */
static void run_to(uint32_t ticks)
{
    uint32_t blk;
    for (blk = 0; blk < 400000u && song_ticks < ticks; blk++)
        block();
}

static void stop(void)
{
    transport_req = 4;
    block();
}

typedef struct { uint32_t at, note, inst, len, held, ghost; } node_t;
static node_t nodes[TAPE_N];
static uint32_t listed;
static uint32_t list(uint32_t track)
{
    uint16_t node;
    listed = 0;
    for (node = tp_trk[track].first; node != TAPE_END && listed < TAPE_N; node = tp_nx[node]) {
        node_t *nd = &nodes[listed++];
        nd->at = TP_AT(tp_w0[node]);
        nd->note = TP_NOTE(tp_w0[node]);
        nd->inst = TP_INST(tp_w0[node]);
        nd->len = TP_LEN(tp_w1[node]);
        nd->held = (tp_w1[node] & TP_HELD) != 0u;
        nd->ghost = (tp_w0[node] & TP_GHOST) != 0u;
    }
    return listed;
}

static int sounding(void)
{
    uint32_t k, i;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++)
            if (trk[k].v[i].gate)
                return 1;
    for (k = 0; k < TAPE_VOICES; k++)
        if (tp_voice[k])
            return 1;
    return 0;
}

static int near_at(uint32_t got, uint32_t want, uint32_t slack) { return got + slack >= want && got <= want + slack; }

static int test_live_off(void)
{
    track_t *t = &trk[0];
    uint32_t at, at2, bad = 0;
    fresh(4, 0);
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    run_to(1001);
    at = tape_head;
    input_on(t, 60, 90);
    run_to(1301);
    input_off(t, 60);
    run_to(2501);
    at2 = tape_head;
    input_on(t, 62, 70);
    input_off(t, 62);
    block();
    list(0);
    bad += check("live, GRID OFF: stamped at the head, its held length, its velocity, held cleared",
                 listed == 2u && nodes[0].at == at && near_at(nodes[0].len, 300, 2) && !nodes[0].held &&
                 nodes[0].note == 60u && nodes[0].inst == 0u && TP_VEL(tp_w1[tp_trk[0].first]) == 90u &&
                 nodes[1].at == at2 && nodes[1].len == 1u && !tp_open_n);
    stop();
    return bad;
}

/* GRID 1/16: notes pressed at offsets after a line; in the window (here 15 ticks) on the line, later on the next */
static int test_live_grid(void)
{
    static const uint16_t OFFSETS[6] = {0, 5, 14, 30, 70, 110};
    track_t *t = &trk[0];
    uint32_t k, bad = 0, wrong = 0, want[6];
    fresh(4, 5);
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    for (k = 0; k < 6u; k++) {
        uint32_t line = 960u + 480u * k;
        run_to(line + OFFSETS[k]);
        want[k] = tape_head - tape_head % 120u + (tape_head % 120u > grid_window(5, 120) ? 120u : 0u);
        input_on(t, 60u + k, 100);
        run_to(line + 200u);
        input_off(t, 60u + k);
    }
    block();
    list(0);
    for (k = 0; k < listed; k++)
        wrong += nodes[k].at % 120u != 0 || nodes[k].at != want[k] || nodes[k].note != 60u + k;
    bad += check("live, GRID 1/16: every note on a line (in the window: its line; later: the next one, where it sounded)",
                 listed == 6u && !wrong);
    stop();
    fresh(4, 5);
    song.g[G_SWING] = 50;
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    run_to(1920u + 120u + 24u + 4u);                      /* 4 ticks after the swung odd line (144 into the beat) */
    input_on(t, 72, 100);
    input_off(t, 72);
    block();
    list(0);
    bad += check("live, GRID 1/16 SWING 50: a note just after a swung line lands on it (2064)", listed == 1u && nodes[0].at == 2064u);
    stop();
    return bad;
}

/* the step sequencer on track 1: 4 notes a bar, armed, over 4 passes of a 1 bar tape */
static int test_seq_passes(void)
{
    static const uint8_t NOTES[4] = {48, 52, 55, 59};
    track_t *t = &trk[0];
    uint32_t k, bad = 0, wrong = 0, first;
    fresh(1, 0);
    t->p[P_SLEN] = 16;
    for (k = 0; k < 4u; k++)
        put_step(t, 4u * k, 1, &NOTES[k], ST_NOTE, 0);
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    run_to(TAPE_BAR - 10u);
    first = list(0);
    run_to(4u * TAPE_BAR + 200u);                       /* (pass 5's first note over: none open) */
    list(0);
    for (k = 0; k < listed; k++)
        wrong += nodes[k].note != NOTES[k] || !near_at(nodes[k].at, 480u * k, 2) || nodes[k].held || nodes[k].len < 10u;
    bad += check("sequencer, overdub: its 4 notes recorded in the first pass, the next 3 passes add none (one node a note)",
                 first == 4u && listed == 4u && !wrong);
    stop();
    return bad;
}

static int test_arp_passes(void)
{
    track_t *t = &trk[0];
    uint32_t bad = 0, after1, after3;
    fresh(1, 0);
    t->p[P_AMODE] = AM_UP;
    t->p[P_ARATE] = 2;
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    block();
    input_on(t, 60, 100);
    input_on(t, 64, 100);
    run_to(TAPE_BAR + 10u);
    after1 = list(0);
    run_to(3u * TAPE_BAR + 10u);
    after3 = list(0);
    input_off(t, 60);
    input_off(t, 64);
    bad += check("ARP, overdub: what it plays recorded in the first pass, the next passes add none", after1 >= 4u && after3 == after1);
    stop();
    return bad;
}

/* replace mode: pass 1 records A, pass 2 (still armed) records B and passes A (ghosted), disarmed after: B alone */
static int test_replace(void)
{
    track_t *t = &trk[0];
    uint32_t bad = 0;
    fresh(1, 0);
    tp_overdub = 0;
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    run_to(500);
    input_on(t, 60, 100);
    run_to(600);
    input_off(t, 60);
    run_to(TAPE_BAR + 900u);
    input_on(t, 64, 100);
    run_to(TAPE_BAR + 1000u);
    input_off(t, 64);
    run_to(2u * TAPE_BAR - 10u);
    tape_req = TQ_REC;                                  /* (punch out) */
    run_to(3u * TAPE_BAR);
    stop();
    bad += check("replace mode: STOP sweeps the ghosts (the main loop's tape_poll): the count is the notes",
                 tp_trk[0].count == 1u && list(0) == 1u && !tp_ghosts);
    tape_sweep();
    list(0);
    bad += check("replace mode: the second pass replaces the first (A ghosted as it passed, swept), B stays",
                 listed == 1u && nodes[0].note == 64u && near_at(nodes[0].at, 900, 1) && near_at(nodes[0].len, 100, 2));
    return bad;
}

static int test_held_wrap(void)
{
    track_t *t = &trk[0];
    uint32_t bad = 0, at;
    fresh(1, 0);
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    run_to(1700);
    at = tape_head;
    input_on(t, 60, 100);
    run_to(TAPE_BAR + 300u);
    list(0);
    bad += check("held over the loop's end: still open (held) after the wrap", listed == 1u && nodes[0].held);
    input_off(t, 60);
    block();
    list(0);
    bad += check("held over the loop's end: its length across the wrap (1920 - 1700 + 300)",
                 listed == 1u && nodes[0].at == at && near_at(nodes[0].len, TAPE_BAR - at + 300u, 2) && !nodes[0].held);
    stop();
    return bad;
}

/* the record queue filled before the main loop drains it: everything open is closed */
static int test_overflow(void)
{
    track_t *t = &trk[0];
    uint32_t bad = 0, k;
    fresh(4, 0);
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    run_to(800);
    input_on(t, 60, 100);
    block();
    events_block(CTL);
    for (k = 0; k < 2u * TAPE_RQ; k++)
        trk_note_off(t, 30);                            /* (an off for a note never recorded, many times) */
    events_block(CTL);
    bad += check("record queue: overflow flagged", tp_rq_lost != 0u);
    tape_poll();
    list(0);
    bad += check("record queue overflow: the open note closed at the head, nothing left open",
                 listed == 1u && !nodes[0].held && nodes[0].len >= 1u && !tp_open_n && !tp_rq_lost);
    input_off(t, 60);
    stop();
    return bad;
}

/* the count-in's last eighth: a note kept onto the start (tick 0), held over it or let go before it */
static int test_count_in(void)
{
    track_t *t = &trk[0];
    uint32_t bad = 0, blk;
    fresh(4, 0);
    cin_bars = 1;
    tape_arm = 1;
    tape_req = TQ_REC;
    for (blk = 0; blk < 10000u && !(cin_left == 1u && cin_pos > click_beat_len() * 3u / 4u); blk++)
        block();
    input_on(t, 60, 100);
    for (blk = 0; blk < 10000u && !tape_go; blk++)
        block();
    block();
    list(0);
    bad += check("count-in: a note held from its last eighth is on the tape at 0, open", listed == 1u && nodes[0].at == 0u &&
                 nodes[0].held && nodes[0].note == 60u);
    run_to(600);
    input_off(t, 60);
    block();
    list(0);
    bad += check("count-in: let go after the start, its length from 0", listed == 1u && near_at(nodes[0].len, 600, 3) && !nodes[0].held);
    stop();
    fresh(4, 0);
    cin_bars = 1;
    tape_arm = 1;
    tape_req = TQ_REC;
    for (blk = 0; blk < 10000u && !(cin_left == 1u && cin_pos > click_beat_len() * 3u / 4u); blk++)
        block();
    input_on(t, 62, 100);
    block();
    input_off(t, 62);
    for (blk = 0; blk < 10000u && !tape_go; blk++)
        block();
    run_to(300);
    list(0);
    bad += check("count-in: let go before the start, at 0, a sixteenth long", listed == 1u && nodes[0].at == 0u &&
                 nodes[0].len == TAPE_TPQ / 4u && !nodes[0].held);
    stop();
    fresh(4, 0);                                        /* (PLAY from bar 3: the head's bar line, not 0) */
    cin_bars = 1;
    tape_arm = 1;
    tape_head = 2u * TAPE_BAR;
    tape_req = TQ_REC;
    for (blk = 0; blk < 10000u && !(cin_left == 1u && cin_pos > click_beat_len() * 3u / 4u); blk++)
        block();
    input_on(t, 64, 100);
    for (blk = 0; blk < 10000u && !tape_go; blk++)
        block();
    block();
    list(0);
    bad += check("count-in from bar 3: a note held from its last eighth lands on the head's bar line (3840), open",
                 listed == 1u && nodes[0].at == 2u * TAPE_BAR && nodes[0].held && nodes[0].note == 64u);
    input_off(t, 64);
    stop();
    return bad;
}

/* an engine switch while the tape sounds a long note of that instrument: the tape's voice ends as the switch begins
 * (nothing left sounding on the old engine's note), another instrument's keeps sounding */
static int test_engine_switch_voices(void)
{
    track_t *t = &trk[0];
    uint32_t bad = 0, blk, k, inst0 = 0, inst1 = 0;
    fresh(1, 0);
    host_preset(t, 0, 1);
    (void)tape_insert(0, 100u | 60u << 20 | 0u << 27, 1500u | 100u << 20, TAPE_ANY_PASS);
    (void)tape_insert(1, 100u | 62u << 20 | 1u << 27, 1500u | 100u << 20, TAPE_ANY_PASS);
    tape_req = TQ_PLAY;
    run_to(400);
    t->eng_req = (uint8_t)(t->engine ? 0u : 1u);
    for (blk = 0; blk < 4u; blk++)
        block();
    for (k = 0; k < TAPE_VOICES; k++) {
        inst0 += tp_voice[k] && TP_INST(tp_voice[k]) == 0u;
        inst1 += tp_voice[k] && TP_INST(tp_voice[k]) == 1u;
    }
    bad += check("engine switch while the tape sounds: that instrument's tape voice ended, another's kept",
                 !inst0 && inst1 == 1u && !tp_open_n);
    stop();
    bad += check("engine switch under the tape: nothing sounding after STOP", !sounding());
    return bad;
}

static int test_mute(void)
{
    static const uint8_t NOTE = 50;
    track_t *t = &trk[0];
    uint32_t bad = 0;
    fresh(1, 0);
    t->p[P_MUTE] = 1;
    t->p[P_SLEN] = 16;
    put_step(t, 0, 1, &NOTE, ST_NOTE, 0);
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    run_to(300);
    input_on(t, 60, 100);
    run_to(500);
    input_off(t, 60);
    run_to(2u * TAPE_BAR);
    bad += check("REC with the instrument on MUTE: nothing recorded", !list(0) && !tp_open_n);
    stop();
    fresh(1, 0);
    trk[1].p[P_SLEN] = 16;
    put_step(&trk[1], 0, 1, &NOTE, ST_NOTE, 0);
    input_on(t, 60, 100);
    transport_req = 1;
    tape_req = TQ_PLAY;
    run_to(2u * TAPE_BAR);
    input_off(t, 60);
    block();
    bad += check("REC never armed, the tape playing: nothing recorded (live notes, the sequencer)", !list(0) && !tp_open_n);
    stop();
    return bad;
}

static int test_ends(void)
{
    track_t *t = &trk[0];
    uint32_t bad = 0, blk;
    fresh(1, 0);
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    run_to(300);
    input_on(t, 60, 100);
    input_on(t, 64, 100);
    run_to(700);
    stop();
    list(0);
    bad += check("STOP while recording held notes: both closed at the stop", listed == 2u && !nodes[0].held && !nodes[1].held &&
                 near_at(nodes[0].len, 400, 3) && !tp_open_n);
    input_off(t, 60);
    input_off(t, 64);
    tape_poll();
    bad += check("STOP: nothing sounding", !sounding());

    fresh(1, 0);
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    run_to(300);
    input_on(t, 60, 100);
    run_to(700);
    panic_req = 1;
    block();
    list(0);
    bad += check("panic while recording: the open note closed, nothing sounding", listed == 1u && !nodes[0].held && !tp_open_n &&
                 !sounding());
    input_off(t, 60);
    run_to(2u * TAPE_BAR + 100u);
    stop();
    bad += check("panic: the recorded note plays back and ends", !sounding());

    fresh(1, 0);
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    run_to(300);
    input_on(t, 60, 100);
    run_to(500);
    tape_req = TQ_REC;                                  /* (punch out) */
    run_to(700);
    input_off(t, 60);
    block();
    list(0);
    bad += check("REC let go while a note is held: its end still goes in (400 long), nothing left open",
                 listed == 1u && !nodes[0].held && near_at(nodes[0].len, 400, 3) && !tp_open_n);
    stop();

    fresh(1, 0);
    host_preset(t, 0, 1);
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    run_to(300);
    input_on(t, 60, 100);
    run_to(400);
    t->eng_req = (uint8_t)(t->engine ? 0u : 1u);
    for (blk = 0; blk < 200u; blk++)
        block();
    input_off(t, 60);
    run_to(TAPE_BAR + 1000u);
    list(0);
    bad += check("engine switch while recording: the note closed with its key, played back on the new engine, ends",
                 listed == 1u && !nodes[0].held && !tp_open_n && !sounding());
    stop();
    bad += check("engine switch: nothing sounding after STOP", !sounding());
    return bad;
}

/* STRUM on a POLY track: a chord recorded strummed in pass 1, then each pass plays it back; the replay is never
 * recorded again (its nodes keep their ticks and lengths, none left held, nothing sounding after STOP) */
static int test_strum_passes(void)
{
    static const uint8_t CHORD[3] = {60, 64, 67};
    track_t *t = &trk[0];
    node_t first[3];
    uint32_t bad = 0, k, wrong = 0, count1;
    fresh(1, 0);
    t->p[P_STRUM] = 50;
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    run_to(100);
    for (k = 0; k < 3u; k++)
        input_on(t, CHORD[k], 100);
    run_to(500);
    for (k = 0; k < 3u; k++)
        input_off(t, CHORD[k]);
    run_to(TAPE_BAR - 10u);
    count1 = list(0);
    memcpy(first, nodes, sizeof first);
    run_to(5u * TAPE_BAR - 10u);
    list(0);
    for (k = 0; k < listed && k < 3u; k++)
        wrong += nodes[k].at != first[k].at || nodes[k].len != first[k].len || nodes[k].held || first[k].len < 100u;
    bad += check("STRUM 50, overdub: the chord recorded strummed once, 4 passes of its replay leave it as it was",
                 count1 == 3u && listed == 3u && !wrong && !tp_open_n);
    stop();
    bad += check("STRUM: nothing sounding after STOP", !sounding());
    fresh(1, 0);                                        /* (a step's chord: recorded as it sounded, strummed) */
    t->p[P_STRUM] = 50;
    t->p[P_SLEN] = 16;
    put_step(t, 0, 3, CHORD, ST_NOTE, 0);
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    run_to(TAPE_BAR - 10u);
    count1 = list(0);
    memcpy(first, nodes, sizeof first);
    run_to(4u * TAPE_BAR - 10u);
    list(0);
    for (k = wrong = 0; k < listed && k < 3u; k++)
        wrong += nodes[k].at != first[k].at || nodes[k].held;
    bad += check("STRUM 50, a step's chord: recorded spread as it sounded (low to high), the passes after add none",
                 count1 == 3u && listed == 3u && !wrong && first[0].at < first[1].at && first[1].at < first[2].at &&
                 first[0].note == 60u && first[2].note == 67u);
    stop();
    return bad;
}

/* GRID 1/16: a late note waits for its line and is let go before its end (SOUNDED, RELEASED, its off still due); the
 * same key pressed again in the next line's window sounds at once: the old one's pending off never ends it */
static int test_live_retrigger(void)
{
    track_t *t = &trk[0];
    uint32_t bad = 0, k, cut = 0, at2;
    fresh(4, 5);
    tape_arm = 1;
    transport_req = 1;
    tape_req = TQ_REC;
    run_to(1920u + 120u + 60u);                         /* (60 ticks after a line: late, sounds on the next) */
    input_on(t, 60, 100);
    run_to(song_ticks + 20u);
    input_off(t, 60);
    run_to(1920u + 240u + 6u);                          /* (in the next line's window: at once) */
    at2 = tape_head;
    input_on(t, 60, 100);
    for (k = 0; k < 8u; k++) {
        uint32_t i, gate = 0;
        run_to(song_ticks + 10u);
        for (i = 0; i < NVOICE; i++)
            gate |= t->v[i].gate;
        cut += !gate;
    }
    input_off(t, 60);
    block();
    list(0);
    bad += check("live, GRID: a key pressed again while its delayed note's end is due keeps sounding while held",
                 !cut && listed == 1u && nodes[0].at == at2 - at2 % 120u && nodes[0].len >= 70u && !nodes[0].held &&
                 !tp_open_n);                           /* (both on the same line: one node, the second's length) */
    stop();
    bad += check("live retrigger: nothing sounding after STOP", !sounding());
    return bad;
}

/* tape REC records a sequencer only while it runs; SEQ REC alone records nothing onto the tape */
static int test_seq_stopped_not_recorded(void)
{
    static const uint8_t NOTES[4] = {48, 52, 55, 59};
    track_t *t = &trk[0];
    uint32_t k, bad = 0, stopped, joined, after;
    fresh(1, 0);
    t->p[P_SLEN] = 16;
    for (k = 0; k < 4u; k++)
        put_step(t, 4u * k, 1, &NOTES[k], ST_NOTE, 0);
    tape_arm = 1;
    tape_req = TQ_REC;
    run_to(2u * TAPE_BAR);
    stopped = list(0);
    transport_req = 1;                                  /* (joins on the next bar: GRID OFF) */
    run_to(4u * TAPE_BAR - 10u);
    joined = list(0);
    transport_req = 2;
    block();
    run_to(6u * TAPE_BAR);
    after = list(0);
    bad += check("tape REC, the sequencer stopped: nothing; joined: its 4 notes; stopped again: none more, the tape runs on",
                 !stopped && joined == 4u && after == 4u && tape_go && tape_recording);
    stop();
    fresh(1, 0);
    t->p[P_SLEN] = 16;
    for (k = 0; k < 4u; k++)
        put_step(t, 4u * k, 1, &NOTES[k], ST_NOTE, 0);
    song.rec = 1;
    transport_req = 1;
    tape_req = TQ_PLAY;
    run_to(300);
    input_on(t, 60, 100);
    run_to(500);
    input_off(t, 60);
    run_to(2u * TAPE_BAR);
    bad += check("SEQ REC with the tape playing, tape REC off: nothing onto the tape (live or sequencer)", !list(0) && !tp_open_n);
    song.rec = 0;
    stop();
    return bad;
}

/* REC with the tape running: punch in, punch out; a note held over the punch out keeps its end */
static int test_punch(void)
{
    track_t *t = &trk[0];
    uint32_t bad = 0, in_at;
    fresh(4, 0);
    tape_arm = 1;
    tape_req = TQ_PLAY;
    run_to(300);
    input_on(t, 60, 100);
    run_to(400);
    input_off(t, 60);
    tape_req = TQ_REC;
    run_to(700);
    in_at = tape_head;
    input_on(t, 62, 100);
    run_to(1000);
    tape_req = TQ_REC;
    run_to(1100);
    input_off(t, 62);
    run_to(1300);
    input_on(t, 64, 100);
    run_to(1400);
    input_off(t, 64);
    block();
    list(0);
    bad += check("punch: before REC nothing, in: the note (its end after the punch out too), out: nothing; the tape runs on",
                 listed == 1u && nodes[0].note == 62u && nodes[0].at == in_at && near_at(nodes[0].len, 400, 3) &&
                 !nodes[0].held && !tp_open_n && tape_go && !tape_recording);
    stop();
    return bad;
}

int main(void)
{
    uint32_t fails = 0;
    fails += test_strum_passes();
    fails += test_live_retrigger();
    fails += test_live_off();
    fails += test_live_grid();
    fails += test_seq_passes();
    fails += test_arp_passes();
    fails += test_replace();
    fails += test_held_wrap();
    fails += test_overflow();
    fails += test_count_in();
    fails += test_mute();
    fails += test_ends();
    fails += test_engine_switch_voices();
    fails += test_seq_stopped_not_recorded();
    fails += test_punch();
    printf(fails ? "taperec: %u FAILED\n" : "taperec: all passed\n", (unsigned)fails);
    return fails ? 1 : 0;
}
