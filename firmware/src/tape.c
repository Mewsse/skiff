/* SPDX-License-Identifier: GPL-3.0-only */
/* The note tape: 4 tracks of note events (no knob moves), each a list sorted by tick, merged without loss.
 * One structural writer, the main loop: it walks with irqs on and commits each change in a short irq-off section of
 * a few stores. The audio ISR (tape_block) only moves each track's cur, sets the ghost bit, plays its voices and
 * fills the record queue. A node that leaves a list waits in limbo until the ISR finished a block since.
 * Included by seq.c after tape_length(). Needs, declared before it: grid.c TAPE_TPQ / TAPE_BAR; seq.c tape_len
 * (0: 4 bars), tape_length(), tape_head, tape_pass, song_ticks, tape_go / tape_recording; voice.c trk[], trk_note_on /
 * trk_note_off, tape_playing; core.h NTRK; fm1_irq.h fm1_irq_off / fm1_irq_on. */
#ifndef TAPE_N
#define TAPE_N 1440u                   /* nodes, all tracks: 10 B each (tape_store.c: three chunks of them) */
#endif
#define TAPE_TRACKS 4u
#define TAPE_END 0xFFFFu
#define TAPE_VOICES 48u
#define TAPE_OPEN 16u
#define TAPE_DEDUP 12u                 /* ticks: overdub merges same inst+note within this */
#define TAPE_RQ 128u
#define TAPE_BURST 16u                 /* nodes a track sounds in one block at most (the rest due then: skipped) */
#define TAPE_BARS_MAX 256u
#define TAPE_LEN_MAX (TAPE_BARS_MAX * TAPE_BAR)   /* a note's longest (a stored one past L keeps its own: L can shrink) */
#define TAPE_PAGE 256u
#define TAPE_ANY_PASS (1u << 16)         /* tape_insert's pass for an edit, not a recording: plays this pass when ahead */

/* tests define TAPE_YIELD (it runs tape_block inside every main-side walk) and TAPE_IRQ_HOOKS (their own irq-off) */
#ifndef TAPE_HAVE_PLAYING
static uint8_t tape_playing;           /* 1 while the tape sounds a note: the record hooks skip it (voice.c owns it) */
#endif
#ifndef TAPE_YIELD
#define TAPE_YIELD() do { } while (0)
#endif
#ifndef TAPE_LOAD_WAIT
#define TAPE_LOAD_WAIT 1               /* tape_load_begin waits for the ISR's block (hostsim.c: 0, the host's one thread) */
#endif
#ifndef TAPE_MS
#define TAPE_MS() fm1_ms               /* (core.h; tests/tape_test.c its own) */
#endif
#define TAPE_LOAD_MS 5u                /* .. at most this long (blocks are 0.7 ms) or TAPE_LOAD_SPINS tests (no ms tick: the
                                        * web emulator's, whose clock is its audio); then on: no ISR runs a block */
#define TAPE_LOAD_SPINS (1u << 20)
#ifndef TAPE_IRQ_HOOKS
static uint32_t tp_irq_save(void) { fm1_irq_off(); return 0; }
static void tp_irq_restore(uint32_t flags) { (void)flags; fm1_irq_on(); }
#endif

#define TP_AT(w) ((w) & 0xFFFFFu)
#define TP_NOTE(w) (((w) >> 20) & 0x7Fu)
#define TP_INST(w) (((w) >> 27) & 7u)
#define TP_KEY(w) ((w) & (0x3FFu << 20))        /* note + inst, in w0, a voice and a record event alike */
#define TP_GHOST (1u << 30)
#define TP_LEN(w) ((w) & 0xFFFFFu)
#define TP_VEL(w) (((w) >> 20) & 0x7Fu)
#define TP_HELD (1u << 27)
#define TP_VTRK(v) ((v) >> 30)

/* w0: at:20 | note:7 | inst:3 | ghost:1 | spare:1. Main loop writes it before linking; after that only the ISR sets ghost */
static uint32_t tp_w0[TAPE_N] __attribute__((section(".pool")));
/* w1: len:20 (>= 1) | vel:7 | held:1 | spare:4. Main loop only, one 32-bit store. Held: len holds the pass it began */
static uint32_t tp_w1[TAPE_N] __attribute__((section(".pool")));
/* next node, same track, sorted by at (stable). Main loop only */
static uint16_t tp_nx[TAPE_N] __attribute__((section(".pool")));

typedef struct {
    uint16_t first;                    /* main loop, irq-off */
    volatile uint16_t cur;             /* next node to play: ISR advances; main loop writes only irq-off */
    uint16_t count;
    uint8_t mute;
    volatile uint8_t kill;             /* main sets: ISR ends this track's voices next block */
} tape_track_t;
static tape_track_t tp_trk[TAPE_TRACKS] __attribute__((section(".pool")));

/* sounding tape notes: left:20 | note:7 | inst:3 | track:2; 0 = free. One slot per (inst, note) */
static uint32_t tp_voice[TAPE_VOICES] __attribute__((section(".pool")));
static uint8_t tp_voices __attribute__((section(".pool")));             /* ISR: tp_voice's slots in use */

static uint16_t tp_free __attribute__((section(".pool"))), tp_limbo __attribute__((section(".pool")));
static uint16_t tp_limbo_tail __attribute__((section(".pool")));
static uint32_t tp_limbo_epoch __attribute__((section(".pool")));
static volatile uint32_t tp_epoch __attribute__((section(".pool")));     /* ISR: +1 per tape_block */
static uint16_t tp_open[TAPE_OPEN] __attribute__((section(".pool")));    /* main loop only: held nodes, oldest first */
static uint8_t tp_open_n __attribute__((section(".pool")));
static uint8_t tp_target __attribute__((section(".pool"))), tp_overdub __attribute__((section(".pool")));   /* tape_init: 0, 1 */
static uint8_t tp_full __attribute__((section(".pool")));                /* main: a note was refused (TAPE FULL) */
static volatile uint8_t tp_seek_req __attribute__((section(".pool")));   /* main: the ISR seeks before it plays */
static volatile uint8_t tp_ghosts __attribute__((section(".pool")));     /* ISR: replace mode ghosted a node (main sweeps
                                                                          * them once stopped) */
static uint32_t tp_seen_ticks __attribute__((section(".pool")));         /* ISR: song_ticks, tape_pass of its last block */
static uint16_t tp_seen_pass __attribute__((section(".pool")));

/* record queue, ISR -> main, single producer single consumer */
enum { TR_ON, TR_OFF, TR_CLOSE_INST, TR_CLOSE_ALL };
typedef struct { uint32_t w0; uint32_t w1; } tape_rec_t;  /* w0: at:20 | note:7 | inst:3 | kind:2; w1: pass:16 | vel:7 */
static tape_rec_t tp_rq[TAPE_RQ];
static volatile uint16_t tp_rq_w, tp_rq_r;
static volatile uint8_t tp_rq_lost;    /* ISR: an event dropped; main closes every open note */
static uint8_t tp_rec_mask;            /* ISR: instruments with a recorded note-on since the last close (their ends go in) */

static void tape_init(void)
{
    uint32_t k;
    for (k = 0; k < TAPE_N; k++) {
        tp_w0[k] = tp_w1[k] = 0;
        tp_nx[k] = (uint16_t)(k + 1u < TAPE_N ? k + 1u : TAPE_END);
    }
    for (k = 0; k < TAPE_TRACKS; k++) {
        tp_trk[k].first = tp_trk[k].cur = TAPE_END;
        tp_trk[k].count = 0;
        tp_trk[k].mute = tp_trk[k].kill = 0;
    }
    for (k = 0; k < TAPE_VOICES; k++)
        tp_voice[k] = 0;
    tp_voices = 0;
    tp_free = 0;
    tp_limbo = tp_limbo_tail = TAPE_END;
    tp_open_n = 0;
    tp_full = 0;
    tp_target = 0;
    tp_overdub = 1;
    tp_rq_w = tp_rq_r = 0;
    tp_rq_lost = 0;
    tp_rec_mask = 0;
    tp_epoch = 0;
    tp_limbo_epoch = 0;
    tp_seen_ticks = song_ticks;
    tp_seen_pass = tape_pass;
    tp_ghosts = 0;
}

/* ------------------------------------------------------------------ ISR side --- */

static void tp_voice_end(uint32_t slot)
{
    uint32_t voice = tp_voice[slot];
    tp_voice[slot] = 0;
    tp_voices--;
    tape_playing = 1;
    trk_note_off(&trk[TP_INST(voice)], TP_NOTE(voice));
    tape_playing = 0;
}

/* a node sounds: its (inst, note) slot retriggers and keeps the later end, else a free slot, else the shortest left.
 * Not inlined: its loop stays out of tape_block's (tests/target_budget.py) */
static __attribute__((noinline)) void tp_fire(uint32_t track, uint32_t w0, uint32_t w1)
{
    uint32_t key = TP_KEY(w0), len = TP_LEN(w1) ? TP_LEN(w1) : 1u, slot, spare = TAPE_VOICES, shortest = 0, least = 0xFFFFFFFFu, k;
    for (k = 0; k < TAPE_VOICES; k++) {
        uint32_t voice = tp_voice[k];
        if (!voice) {
            if (spare == TAPE_VOICES)
                spare = k;
        } else if (TP_KEY(voice) == key)
            break;
        else if (TP_LEN(voice) < least) {
            least = TP_LEN(voice);
            shortest = k;
        }
    }
    if (k < TAPE_VOICES) {
        uint32_t left = TP_LEN(tp_voice[k]);
        slot = k;
        len = left > len ? left : len;
    } else if (spare < TAPE_VOICES) {
        slot = spare;
        tp_voices++;
    } else {
        slot = shortest;
        tp_voice_end(slot);
        tp_voices++;
    }
    tp_voice[slot] = len | key | track << 30;
    tape_playing = 1;
    trk_note_on(&trk[TP_INST(w0)], TP_NOTE(w0), TP_VEL(w1));
    tape_playing = 0;
}

/* a track passes every due node (wrapped: first the rest of the pass, then from its start), so cur never lags the
 * head (a recorded insert stops before cur: the list stays sorted); it sounds TAPE_BURST of them at most. The walk is
 * bounded by TAPE_N (a list never holds more: a broken one cannot hang the ISR) */
static void tp_play(uint32_t track, uint32_t wrapped)
{
    tape_track_t *tt = &tp_trk[track];
    uint32_t budget = TAPE_BURST, len = tape_length(), quiet = tt->mute, steps = TAPE_N;
    uint32_t replace = tape_recording && track == tp_target && !tp_overdub;
    uint16_t node = tt->cur;
    int round;
    for (round = wrapped ? 0 : 1; round < 2; round++) {
        while (node != TAPE_END && steps--) {
            uint32_t w0 = tp_w0[node], w1;
            if (round ? TP_AT(w0) > tape_head : TP_AT(w0) >= len)
                break;
            w1 = tp_w1[node];
            if (!quiet && !(w0 & TP_GHOST) && !(w1 & TP_HELD)) {
                if (replace) {
                    tp_w0[node] = w0 | TP_GHOST;
                    tp_ghosts = 1;
                } else if (budget) {
                    tp_fire(track, w0, w1);
                    budget--;
                }
            }
            node = tp_nx[node];
        }
        if (!round)
            node = tt->first;
    }
    tt->cur = node;
}

/* per track cur = the first node at or after head (PLAY, SPP, a locate; the main loop asks with tp_seek_req) */
static __attribute__((noinline)) void tape_seek(uint32_t head)
{
    uint32_t k;
    for (k = 0; k < TAPE_TRACKS; k++) {
        uint16_t node = tp_trk[k].first;
        while (node != TAPE_END && TP_AT(tp_w0[node]) < head)
            node = tp_nx[node];
        tp_trk[k].cur = node;
    }
    tp_seek_req = 0;
    tp_seen_ticks = song_ticks;
    tp_seen_pass = tape_pass;
}

/* once a block, after seq_tick. Always called: the epoch frees limbo even while stopped. Empty tracks and an idle
 * voice table cost a test each */
static __attribute__((noinline)) void tape_block(void)
{
    uint32_t k, adv, wraps, ending = 0;
    tp_epoch++;
    for (k = 0; k < TAPE_TRACKS; k++)
        if (tp_trk[k].kill || (tp_trk[k].mute && tape_go)) {
            ending |= 1u << k;
            tp_trk[k].kill = 0;
        }
    if (!tape_go) {
        if (ending && tp_voices)
            for (k = 0; k < TAPE_VOICES; k++)
                if (tp_voice[k] && (ending >> TP_VTRK(tp_voice[k]) & 1u))
                    tp_voice_end(k);
        return;
    }
    adv = song_ticks - tp_seen_ticks;
    wraps = (uint16_t)(tape_pass - tp_seen_pass);
    if (tp_seek_req || (int32_t)adv < 0 || wraps > 1u) {   /* a locate nobody told us of: no fires across it */
        tape_seek(tape_head);
        adv = wraps = 0;
    }
    tp_seen_ticks = song_ticks;
    tp_seen_pass = tape_pass;
    if (tp_voices)
        for (k = 0; k < TAPE_VOICES; k++) {
            uint32_t voice = tp_voice[k];
            if (!voice)
                continue;
            if (TP_LEN(voice) <= adv || (ending >> TP_VTRK(voice) & 1u))
                tp_voice_end(k);
            else
                tp_voice[k] = voice - adv;
        }
    for (k = 0; k < TAPE_TRACKS; k++)
        if (tp_trk[k].first != TAPE_END)
            tp_play(k, wraps);
}

/* ISR: a recorded note on / off (at, pass: the head now) */
static void tape_rq_push(uint32_t kind, uint32_t at, uint32_t note, uint32_t inst, uint32_t vel)
{
    uint16_t pos = tp_rq_w;
    if ((uint16_t)(pos - tp_rq_r) >= TAPE_RQ) {
        tp_rq_lost = 1;
        return;
    }
    tp_rq[pos & (TAPE_RQ - 1u)].w0 = TP_AT(at) | (note & 0x7Fu) << 20 | (inst & 7u) << 27 | kind << 30;
    tp_rq[pos & (TAPE_RQ - 1u)].w1 = tape_pass | (vel & 0x7Fu) << 16;
    tp_rq_w = (uint16_t)(pos + 1u);
}

/* ISR: STOP, a locate, SPP, panic, MIDI IN overflow end the tape's voices (inst < 0: all) and, close set, the open
 * notes (an engine switch: the voices only, a held key's note ends with its key). Call it before the head moves: the
 * close is stamped with the head now */
static __attribute__((noinline)) void tape_voices_end(int inst, int close)
{
    uint32_t k;
    for (k = 0; tp_voices && k < TAPE_VOICES; k++)
        if (tp_voice[k] && (inst < 0 || TP_INST(tp_voice[k]) == (uint32_t)inst))
            tp_voice_end(k);
    if (!close)
        return;
    tp_rec_mask &= (uint8_t)(inst < 0 ? 0u : ~(1u << inst));
    tape_rq_push(inst < 0 ? TR_CLOSE_ALL : TR_CLOSE_INST, tape_head, 0, inst < 0 ? 0u : (uint32_t)inst, 0);
}

/* ----------------------------------------------------------------- main side --- */

static uint16_t *tp_link(uint32_t track, uint16_t prev) { return prev == TAPE_END ? &tp_trk[track].first : &tp_nx[prev]; }

static void tp_to_limbo(uint16_t head, uint16_t tail)
{
    if (tp_limbo == TAPE_END)
        tp_limbo_tail = tail;
    tp_nx[tail] = tp_limbo;
    tp_limbo = head;
    tp_limbo_epoch = tp_epoch;
}

static uint16_t tp_alloc(void)
{
    uint16_t node;
    if (tp_free == TAPE_END && tp_limbo != TAPE_END && tp_epoch != tp_limbo_epoch) {
        tp_free = tp_limbo;
        tp_limbo = tp_limbo_tail = TAPE_END;
    }
    node = tp_free;
    if (node != TAPE_END)
        tp_free = tp_nx[node];
    return node;
}

static void tp_open_drop(uint16_t node)
{
    uint32_t k, n = 0;
    for (k = 0; k < tp_open_n; k++)
        if (tp_open[k] != node)
            tp_open[n++] = tp_open[k];
    tp_open_n = (uint8_t)n;
}

/* a node joins track between its sorted neighbours. pass: the recording's; same as now, it never plays this pass
 * (stops before cur: the live note sounded); else it plays this pass when still ahead. Returns TAPE_END when full */
static uint16_t tape_insert(uint32_t track, uint32_t w0, uint32_t w1, uint32_t pass)
{
    tape_track_t *tt = &tp_trk[track];
    uint16_t node = tp_alloc(), prev = TAPE_END, next;
    uint32_t at = TP_AT(w0), irq;
    if (node == TAPE_END) {
        tp_full = 1;
        return TAPE_END;
    }
    tp_w0[node] = w0 & ~TP_GHOST;
    tp_w1[node] = w1;
    next = tt->first;
    while (next != TAPE_END && TP_AT(tp_w0[next]) <= at && !(pass == tape_pass && next == tt->cur)) {
        prev = next;
        next = tp_nx[next];
        TAPE_YIELD();
    }
    irq = tp_irq_save();
    tp_nx[node] = next;
    *tp_link(track, prev) = node;
    if (next == tt->cur && pass != tape_pass && at >= tape_head)
        tt->cur = node;
    tp_irq_restore(irq);
    tt->count++;
    return node;
}

static void tp_unlink(uint32_t track, uint16_t prev, uint16_t node)
{
    tape_track_t *tt = &tp_trk[track];
    uint32_t irq = tp_irq_save();
    *tp_link(track, prev) = tp_nx[node];
    if (tt->cur == node)
        tt->cur = tp_nx[node];
    tp_irq_restore(irq);
    tt->count--;
    tp_open_drop(node);
    tp_to_limbo(node, node);
}

/* 1: node was on track and is gone */
static int tape_delete(uint32_t track, uint16_t node)
{
    uint16_t prev = TAPE_END, next = tp_trk[track].first;
    while (next != TAPE_END && next != node) {
        prev = next;
        next = tp_nx[next];
        TAPE_YIELD();
    }
    if (next == TAPE_END)
        return 0;
    tp_unlink(track, prev, node);
    return 1;
}

/* the nodes replace mode ghosted leave (idle, before a save or TAPE_GET) */
static void tape_sweep(void)
{
    uint32_t k;
    for (k = 0; k < TAPE_TRACKS; k++) {
        uint16_t prev = TAPE_END, node = tp_trk[k].first;
        while (node != TAPE_END) {
            uint16_t next = tp_nx[node];
            if (tp_w0[node] & TP_GHOST)
                tp_unlink(k, prev, node);
            else
                prev = node;
            node = next;
            TAPE_YIELD();
        }
    }
}

static void tape_clear(uint32_t track)
{
    tape_track_t *tt = &tp_trk[track];
    uint16_t chain, tail;
    uint32_t irq = tp_irq_save();
    chain = tt->first;
    tt->first = tt->cur = TAPE_END;
    tt->kill = 1;
    tp_irq_restore(irq);
    tt->count = 0;
    if (chain == TAPE_END)
        return;
    for (tail = chain;; tail = tp_nx[tail]) {
        tp_open_drop(tail);
        if (tp_nx[tail] == TAPE_END)
            break;
        TAPE_YIELD();
    }
    tp_to_limbo(chain, tail);
}

/* every node of src into dst, in order (dst's first on equal ticks), no allocation. A src node not played yet this
 * pass (src's cur) stays not played in dst; one played already stays behind dst's cur */
static void tape_merge(uint32_t dst, uint32_t src)
{
    tape_track_t *td = &tp_trk[dst], *ts = &tp_trk[src];
    uint16_t prev = TAPE_END, next = td->first, node;
    if (dst == src || dst >= TAPE_TRACKS || src >= TAPE_TRACKS)
        return;
    while ((node = ts->first) != TAPE_END) {
        uint32_t at = TP_AT(tp_w0[node]), irq, unplayed;
        while (next != TAPE_END && TP_AT(tp_w0[next]) <= at) {
            prev = next;
            next = tp_nx[next];
            TAPE_YIELD();
        }
        irq = tp_irq_save();
        ts->first = tp_nx[node];
        unplayed = ts->cur == node;
        if (unplayed)
            ts->cur = ts->first;
        tp_nx[node] = next;
        *tp_link(dst, prev) = node;
        if (unplayed && next == td->cur)
            td->cur = node;
        tp_irq_restore(irq);
        ts->count--;
        td->count++;
        prev = node;
        TAPE_YIELD();
    }
}

/* L in bars (1..256); the head wraps into it, the ISR seeks. Nodes past L stay, unplayed */
static void tape_set_len(uint32_t bars)
{
    uint32_t len, irq;
    bars = bars < 1u ? 1u : bars > TAPE_BARS_MAX ? TAPE_BARS_MAX : bars;
    len = bars * TAPE_BAR;
    irq = tp_irq_save();
    tape_len = len;
    if (tape_head >= len)
        tape_head %= len;
    tp_seek_req = 1;
    tp_irq_restore(irq);
}

/* a held node ends at (at, pass): its length across the passes, 1..L */
static void tp_close(uint16_t node, uint32_t at, uint32_t pass)
{
    uint32_t w1 = tp_w1[node], len = tape_length();
    int64_t ticks = (int64_t)(int16_t)(uint16_t)(pass - TP_LEN(w1)) * len + (int64_t)at - TP_AT(tp_w0[node]);
    ticks = ticks < 1 ? 1 : ticks > (int64_t)len ? (int64_t)len : ticks;
    tp_w1[node] = (uint32_t)ticks | (w1 & (0x7Fu << 20));
}

/* every open node matching (mask over w0's key bits) ends at (at, pass) */
static void tp_close_open(uint32_t key, uint32_t mask, uint32_t at, uint32_t pass)
{
    uint32_t k, n = 0;
    for (k = 0; k < tp_open_n; k++) {
        uint16_t node = tp_open[k];
        if ((tp_w0[node] & mask) == key)
            tp_close(node, at, pass);
        else
            tp_open[n++] = node;
    }
    tp_open_n = (uint8_t)n;
}

static void tp_open_add(uint16_t node)
{
    uint32_t k;
    for (k = 0; k < tp_open_n; k++)
        if (tp_open[k] == node)
            return;
    if (tp_open_n >= TAPE_OPEN) {
        tp_close(tp_open[0], tape_head, tape_pass);
        for (k = 1; k < TAPE_OPEN; k++)
            tp_open[k - 1u] = tp_open[k];
        tp_open_n--;
    }
    tp_open[tp_open_n++] = node;
}

/* overdub: a node of the same inst+note within TAPE_DEDUP ticks (around the loop) on track */
static uint16_t tp_dedup(uint32_t track, uint32_t key, uint32_t at)
{
    uint32_t len = tape_length();
    uint16_t node;
    for (node = tp_trk[track].first; node != TAPE_END; node = tp_nx[node]) {
        uint32_t w0 = tp_w0[node], there = TP_AT(w0), dist = there > at ? there - at : at - there;
        if (there > at + TAPE_DEDUP && at >= TAPE_DEDUP && at + TAPE_DEDUP < len)
            break;
        if (dist < len && len - dist < dist)
            dist = len - dist;
        if (TP_KEY(w0) == key && !(w0 & TP_GHOST) && dist <= TAPE_DEDUP)
            return node;
        TAPE_YIELD();
    }
    return TAPE_END;
}

static void tp_rec_on(uint32_t ev0, uint32_t ev1)
{
    uint32_t at = TP_AT(ev0), key = TP_KEY(ev0), pass = ev1 & 0xFFFFu, vel = (ev1 >> 16) & 0x7Fu;
    uint32_t w1 = TP_HELD | (vel ? vel : 1u) << 20 | pass;
    uint16_t node = TAPE_END;
    if (TP_INST(ev0) >= NTRK)
        return;
    if (tp_overdub)
        node = tp_dedup(tp_target, key, at);
    if (node != TAPE_END) {
        uint32_t there = TP_AT(tp_w0[node]), half = tape_length() / 2u;
        if (there + half < at)                  /* the node is just past the loop's end: the next pass's tick */
            w1 = (w1 & ~0xFFFFFu) | ((pass + 1u) & 0xFFFFu);
        else if (there > at + half)
            w1 = (w1 & ~0xFFFFFu) | ((pass - 1u) & 0xFFFFu);
        tp_w1[node] = w1;
    } else
        node = tape_insert(tp_target, at | key, w1, pass);
    if (node != TAPE_END)
        tp_open_add(node);
}

/* main loop: the record queue in, the notes it opened and closed */
static void tape_poll(void)
{
    while (tp_rq_r != tp_rq_w) {
        tape_rec_t ev = tp_rq[tp_rq_r & (TAPE_RQ - 1u)];
        uint32_t at = TP_AT(ev.w0), pass = ev.w1 & 0xFFFFu;
        tp_rq_r = (uint16_t)(tp_rq_r + 1u);
        switch (ev.w0 >> 30) {
        case TR_ON:
            tp_rec_on(ev.w0, ev.w1);
            break;
        case TR_OFF:
            tp_close_open(TP_KEY(ev.w0), 0x3FFu << 20, at, pass);
            break;
        case TR_CLOSE_INST:
            tp_close_open(ev.w0 & (7u << 27), 7u << 27, at, pass);
            break;
        default:
            tp_close_open(0, 0, at, pass);
            break;
        }
        TAPE_YIELD();
    }
    if (tp_rq_lost) {
        uint32_t irq = tp_irq_save(), lost = tp_rq_lost;   /* (tested and cleared at once: one the ISR sets after stays) */
        tp_rq_lost = 0;
        tp_irq_restore(irq);
        if (lost)
            tp_close_open(0, 0, tape_head, tape_pass);
    }
    if (tp_ghosts && !tape_go) {                  /* stopped: the ghosts go, the counts are the notes (the TAPE page) */
        tp_ghosts = 0;
        tape_sweep();
    }
}

/* ------------------------------------------------- store records (tape_store.c) --- */
/* 8 B a record, little endian: w0 at:20 | note:7 | inst:3; w1 len:20 | vel:7. Track 0..3 in list order, no ghosts,
 * no held nodes; a track's records follow its count. 256 B pages, encoded and decoded one at a time */
typedef struct { uint8_t track; uint16_t node; } tape_it_t;
static struct { uint16_t left[TAPE_TRACKS], head[TAPE_TRACKS], tail[TAPE_TRACKS]; uint8_t track; } tp_ld;

static int tp_saved(uint16_t node) { return !(tp_w0[node] & TP_GHOST) && !(tp_w1[node] & TP_HELD); }

static void tape_save_counts(uint16_t *counts)
{
    uint32_t k;
    for (k = 0; k < TAPE_TRACKS; k++) {
        uint16_t node;
        counts[k] = 0;
        for (node = tp_trk[k].first; node != TAPE_END; node = tp_nx[node])
            counts[k] += (uint16_t)tp_saved(node);
    }
}

static void tape_enc_begin(tape_it_t *it)
{
    it->track = 0;
    it->node = tp_trk[0].first;
}

/* the next page: bytes written (a multiple of 8), 0 when done */
static uint32_t tape_enc_page(tape_it_t *it, uint8_t *page)
{
    uint32_t out = 0, k;
    while (out + 8u <= TAPE_PAGE) {
        uint16_t node;
        uint32_t word[2];
        while (it->node == TAPE_END) {
            if (it->track + 1u >= TAPE_TRACKS) {
                it->track = TAPE_TRACKS;
                return out;
            }
            it->node = tp_trk[++it->track].first;
        }
        node = it->node;
        it->node = tp_nx[node];
        if (!tp_saved(node))
            continue;
        word[0] = tp_w0[node] & 0x3FFFFFFFu;
        word[1] = tp_w1[node] & 0x7FFFFFFu;
        for (k = 0; k < 8u; k++)
            page[out + k] = (uint8_t)(word[k >> 2] >> (8u * (k & 3u)));
        out += 8u;
    }
    return out;
}

/* stopped only, audio running: every track empties, then the pages come in (tape_dec_page), then tape_load_end */
static void tape_load_begin(const uint16_t *counts)
{
    uint32_t k, irq, epoch, since, spins;
    for (k = 0; k < TAPE_TRACKS; k++) {
        irq = tp_irq_save();
        tp_trk[k].first = tp_trk[k].cur = TAPE_END;
        tp_trk[k].kill = 1;
        tp_irq_restore(irq);
        tp_trk[k].count = 0;
        tp_ld.left[k] = counts[k];
        tp_ld.head[k] = tp_ld.tail[k] = TAPE_END;
    }
    tp_ld.track = 0;
    irq = tp_irq_save();                          /* (what the ISR recorded before: not into the tape coming in) */
    tp_rq_r = tp_rq_w;
    tp_rq_lost = 0;
    tp_irq_restore(irq);
    epoch = tp_epoch;
    since = TAPE_MS();
    for (spins = 0; TAPE_LOAD_WAIT && epoch && tp_epoch == epoch && TAPE_MS() - since < TAPE_LOAD_MS &&
                    spins < TAPE_LOAD_SPINS; spins++)      /* (epoch 0: the audio has not started, no ISR to wait for) */
        TAPE_YIELD();
    for (k = 0; k < TAPE_N; k++)
        tp_nx[k] = (uint16_t)(k + 1u < TAPE_N ? k + 1u : TAPE_END);
    tp_free = 0;
    tp_limbo = tp_limbo_tail = TAPE_END;
    tp_open_n = 0;
}

/* a page in: bad or out of order records are dropped (they still count for their track) */
static void tape_dec_page(const uint8_t *page, uint32_t bytes)
{
    uint32_t pos, k;
    for (pos = 0; pos + 8u <= bytes; pos += 8u) {
        uint32_t word[2] = {0, 0}, track;
        uint16_t node, tail;
        while (tp_ld.track < TAPE_TRACKS && !tp_ld.left[tp_ld.track])
            tp_ld.track++;
        if (tp_ld.track >= TAPE_TRACKS)
            return;
        track = tp_ld.track;
        tp_ld.left[track]--;
        for (k = 0; k < 8u; k++)
            word[k >> 2] |= (uint32_t)page[pos + k] << (8u * (k & 3u));
        word[0] &= 0x3FFFFFFFu;
        word[1] &= 0x7FFFFFFu;
        tail = tp_ld.tail[track];
        if (TP_INST(word[0]) >= NTRK || !TP_LEN(word[1]) || !TP_VEL(word[1]) ||
            (tail != TAPE_END && TP_AT(tp_w0[tail]) > TP_AT(word[0])) || tp_free == TAPE_END)
            continue;
        if (TP_LEN(word[1]) > TAPE_LEN_MAX)
            word[1] = (word[1] & ~0xFFFFFu) | TAPE_LEN_MAX;
        node = tp_free;
        tp_free = tp_nx[node];
        tp_w0[node] = word[0];
        tp_w1[node] = word[1];
        tp_nx[node] = TAPE_END;
        if (tail == TAPE_END)
            tp_ld.head[track] = node;
        else
            tp_nx[tail] = node;
        tp_ld.tail[track] = node;
        tp_trk[track].count++;
    }
}

static void tape_load_end(void)
{
    uint32_t k, irq;
    for (k = 0; k < TAPE_TRACKS; k++) {
        irq = tp_irq_save();
        tp_trk[k].first = tp_trk[k].cur = tp_ld.head[k];
        tp_irq_restore(irq);
    }
    tp_seek_req = 1;
}
