/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the note tape's core (firmware/src/tape.c), on its own: the voices, the clock and the irqs are mocks.
 *   build/host/tape_test [ops seed]
 * TAPE_YIELD runs the ISR's tape_block inside every main-loop walk, so the ISR interleaves as on the unit; the
 * irq-off sections are counted (stores, nesting). */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TAPE_TPQ 480u
#define TAPE_BAR (4u * TAPE_TPQ)
#define NTRK 6u
typedef struct { uint8_t id; } track_t;
static track_t trk[NTRK];
static struct { uint8_t playing, rec; } song;
static uint32_t song_ticks, tape_head, tape_len;
static uint16_t tape_pass;
static uint32_t tape_length(void) { return tape_len ? tape_len : 4u * TAPE_BAR; }

static uint8_t gate[NTRK][128];
static uint32_t fires[NTRK][128], off_at[NTRK][128], on_at[NTRK][128];
static void on_fire(uint32_t inst, uint32_t note, uint32_t vel);
static void trk_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t inst = (uint32_t)(t - trk);
    gate[inst][note] = 1;
    fires[inst][note]++;
    on_at[inst][note] = song_ticks;
    on_fire(inst, note, vel);
}
static void trk_note_off(track_t *t, uint32_t note)
{
    uint32_t inst = (uint32_t)(t - trk);
    gate[inst][note] = 0;
    off_at[inst][note] = song_ticks;
}

static int irq_off, irq_bad;
static uint32_t max_stores, sections;
static uint32_t tp_irq_save(void);
static void tp_irq_restore(uint32_t flags);
static void host_yield(void);
#define TAPE_N 1440u
#define TAPE_IRQ_HOOKS
#define TAPE_YIELD() host_yield()
#include "../firmware/src/tape.c"

static int failed;
static int check(const char *what, int ok)
{
    printf("tape: %-78s %s\n", what, ok ? "ok" : "FAIL");
    failed += !ok;
    return ok;
}
static void fail_now(const char *why)
{
    printf("tape: FAIL %s (head %u pass %u)\n", why, (unsigned)tape_head, (unsigned)tape_pass);
    exit(1);
}

/* ------------------------------------------------------------- irq-off: stores counted --- */
static uint32_t sn_w0[TAPE_N], sn_w1[TAPE_N], sn_len, sn_head;
static uint16_t sn_nx[TAPE_N], sn_trk[TAPE_TRACKS][5];
static uint8_t sn_seek;
static void snap_trk(uint16_t *out, uint32_t k)
{
    out[0] = tp_trk[k].first;
    out[1] = tp_trk[k].cur;
    out[2] = tp_trk[k].count;
    out[3] = tp_trk[k].mute;
    out[4] = tp_trk[k].kill;
}
static uint32_t tp_irq_save(void)
{
    uint32_t k;
    if (irq_off)
        irq_bad = 1;
    irq_off = 1;
    memcpy(sn_w0, tp_w0, sizeof tp_w0);
    memcpy(sn_w1, tp_w1, sizeof tp_w1);
    memcpy(sn_nx, tp_nx, sizeof tp_nx);
    for (k = 0; k < TAPE_TRACKS; k++)
        snap_trk(sn_trk[k], k);
    sn_len = tape_len;
    sn_head = tape_head;
    sn_seek = tp_seek_req;
    return 0;
}
static void tp_irq_restore(uint32_t flags)
{
    uint32_t k, j, stores = 0;
    uint16_t now[5];
    (void)flags;
    if (!irq_off)
        irq_bad = 1;
    irq_off = 0;
    for (k = 0; k < TAPE_N; k++)
        stores += (sn_w0[k] != tp_w0[k]) + (sn_w1[k] != tp_w1[k]) + (sn_nx[k] != tp_nx[k]);
    for (k = 0; k < TAPE_TRACKS; k++) {
        snap_trk(now, k);
        for (j = 0; j < 5; j++)
            stores += now[j] != sn_trk[k][j];
    }
    stores += (sn_len != tape_len) + (sn_head != tape_head) + (sn_seek != tp_seek_req);
    max_stores = stores > max_stores ? stores : max_stores;
    sections++;
}

/* ------------------------------------------------------------------- clock and ISR --- */
static uint32_t yield_adv = 5, yield_every = 1, yield_calls, blocks;
static int yield_on = 1;
static void pass_check(void);
static void host_block(uint32_t adv)
{
    if (irq_off)
        fail_now("ISR ran inside an irq-off section");
    if (song.playing) {
        song_ticks += adv;
        tape_head += adv;
        while (tape_head >= tape_length()) {
            tape_head -= tape_length();
            tape_pass++;
        }
    }
    tape_block();
    blocks++;
    pass_check();
}
static void host_yield(void)
{
    if (irq_off)
        fail_now("TAPE_YIELD with irqs off");
    if (yield_on && ++yield_calls % yield_every == 0)
        host_block(yield_adv);
}
static void play(uint32_t ticks, uint32_t step)
{
    uint32_t done;
    for (done = 0; done < ticks; done += step)
        host_block(step);
}
/* play until the head is at tick (forward, wrapping if needed) */
static void play_to(uint32_t tick, uint32_t step)
{
    while (tape_head != tick) {
        uint32_t ahead = tick >= tape_head ? tick - tape_head : tape_length() - tape_head + tick;
        host_block(ahead < step ? ahead : step);
    }
}

/* ------------------------------------------------------- node tags: fires checked per pass --- */
/* every node gets a unique (inst, note, vel): a fire names its node */
#define TAGS (NTRK * 128u * 127u)
typedef struct { uint16_t node; uint8_t live; int32_t req, never; uint32_t fired[2]; } tag_t;
static tag_t tags[TAGS];
static uint32_t node_tag[TAPE_N], next_tag;
static int tracking;
static uint32_t checked_pass, double_fires, same_pass_fires, missed;

static uint32_t tag_w0(uint32_t tag, uint32_t at)
{
    uint32_t inst = tag / (128u * 127u), note = tag / 127u % 128u;
    return at | note << 20 | inst << 27;
}
static uint32_t tag_vel(uint32_t tag) { return tag % 127u + 1u; }

static void on_fire(uint32_t inst, uint32_t note, uint32_t vel)
{
    uint32_t tag = (inst * 128u + note) * 127u + vel - 1u, at, pass;
    tag_t *tg;
    if (!tracking)
        return;
    tg = &tags[tag];
    if (!tg->live)
        fail_now("a dead node fired");
    at = TP_AT(tp_w0[tg->node]);
    pass = at <= tape_head ? tape_pass : (uint16_t)(tape_pass - 1u);
    if (tg->fired[pass & 1u] == pass)
        double_fires++;
    if ((int32_t)pass == tg->never)
        same_pass_fires++;
    tg->fired[pass & 1u] = pass;
}

/* a pass ended: every node there all of it fired once in it */
static void pass_check(void)
{
    uint32_t k;
    if (!tracking)
        return;
    while (checked_pass != tape_pass) {
        for (k = 0; k < TAPE_TRACKS; k++) {
            uint16_t node;
            for (node = tp_trk[k].first; node != TAPE_END; node = tp_nx[node]) {
                tag_t *tg = &tags[node_tag[node]];
                if (tg->live && tg->req <= (int32_t)checked_pass && TP_AT(tp_w0[node]) < tape_length() &&
                    tg->fired[checked_pass & 1u] != checked_pass)
                    missed++;
            }
        }
        checked_pass = (uint16_t)(checked_pass + 1u);
    }
}

static uint32_t tag_new(void)
{
    while (tags[next_tag].live)
        next_tag = (next_tag + 1u) % TAGS;
    return next_tag;
}

/* reference: per track the tags in list order */
static uint32_t ref[TAPE_TRACKS][TAPE_N], ref_n[TAPE_TRACKS];
static uint32_t ref_at(uint32_t tag) { return TP_AT(tp_w0[tags[tag].node]); }

static void ref_insert(uint32_t track, uint32_t tag)
{
    uint32_t pos = ref_n[track], at = ref_at(tag);
    while (pos && ref_at(ref[track][pos - 1u]) > at)
        pos--;
    memmove(&ref[track][pos + 1u], &ref[track][pos], (ref_n[track] - pos) * sizeof ref[0][0]);
    ref[track][pos] = tag;
    ref_n[track]++;
}

/* pass: the event's (tape_pass: recorded now) */
static int tag_insert(uint32_t track, uint32_t at, uint32_t len, uint32_t pass)
{
    uint32_t tag = tag_new();
    int32_t never = pass == tape_pass ? (int32_t)pass : -1;
    uint16_t node = tape_insert(track, tag_w0(tag, at), len | tag_vel(tag) << 20, pass);
    if (node == TAPE_END)
        return 0;
    tags[tag] = (tag_t){node, 1, (int32_t)tape_pass + 1, never, {0xFFFFFFFFu, 0xFFFFFFFFu}};
    node_tag[node] = tag;
    ref_insert(track, tag);
    return 1;
}

static void tag_delete(uint32_t track, uint32_t pos)
{
    uint32_t tag = ref[track][pos];
    if (!tape_delete(track, tags[tag].node))
        fail_now("delete: node not found");
    tags[tag].live = 0;
    memmove(&ref[track][pos], &ref[track][pos + 1u], (ref_n[track] - pos - 1u) * sizeof ref[0][0]);
    ref_n[track]--;
}

static void ref_merge(uint32_t dst, uint32_t src)
{
    uint32_t k;
    for (k = 0; k < ref_n[src]; k++)
        ref_insert(dst, ref[src][k]);
    ref_n[src] = 0;
}

static void tag_clear(uint32_t track)
{
    uint32_t k;
    for (k = 0; k < ref_n[track]; k++)
        tags[ref[track][k]].live = 0;
    ref_n[track] = 0;
    tape_clear(track);
}

/* lists = reference, sorted, counts; lists + free + limbo hold every node once */
static uint8_t seen[TAPE_N];
static int walk_mark(uint16_t node, uint32_t *n)
{
    for (; node != TAPE_END; node = tp_nx[node]) {
        if (node >= TAPE_N || seen[node]++)
            return 0;
        (*n)++;
    }
    return 1;
}
static const char *consistent(int with_ref)
{
    uint32_t k, n, total = 0;
    memset(seen, 0, sizeof seen);
    for (k = 0; k < TAPE_TRACKS; k++) {
        uint16_t node = tp_trk[k].first, prev = TAPE_END;
        n = 0;
        if (!walk_mark(node, &n))
            return "a node twice (list)";
        if (n != tp_trk[k].count)
            return "count";
        if (with_ref && n != ref_n[k])
            return "count vs reference";
        for (n = 0; node != TAPE_END; prev = node, node = tp_nx[node], n++) {
            if (prev != TAPE_END && TP_AT(tp_w0[prev]) > TP_AT(tp_w0[node]))
                return "unsorted";
            if (with_ref && node_tag[node] != ref[k][n])
                return "order vs reference";
        }
        total += tp_trk[k].count;
    }
    if (!walk_mark(tp_free, &total) || !walk_mark(tp_limbo, &total))
        return "a node twice (free / limbo)";
    if (total != TAPE_N)
        return "a node lost";
    if (irq_bad)
        return "irq nesting";
    return 0;
}

static void reset(void)
{
    song.playing = 0;
    song.rec = 0;
    tape_len = 0;
    tape_head = song_ticks = 0;
    tape_pass = 0;
    tape_init();
    tp_target = 0;
    tp_overdub = 1;
    memset(gate, 0, sizeof gate);
    memset(fires, 0, sizeof fires);
    memset(ref_n, 0, sizeof ref_n);
    memset(tags, 0, sizeof tags);
    tracking = 0;
    yield_on = 1;
    yield_every = 1;
    yield_adv = 5;
}
static void start(void)
{
    song.playing = 1;
    tape_seek(tape_head);
    checked_pass = tape_pass;
}
static uint16_t put(uint32_t track, uint32_t at, uint32_t note, uint32_t inst, uint32_t len)
{
    return tape_insert(track, at | note << 20 | inst << 27, len | 100u << 20, TAPE_ANY_PASS);
}
static int gates_off(void)
{
    uint32_t k, j;
    for (k = 0; k < NTRK; k++)
        for (j = 0; j < 128; j++)
            if (gate[k][j])
                return 0;
    for (k = 0; k < TAPE_VOICES; k++)
        if (tp_voice[k])
            return 0;
    return 1;
}
static void rec_event(uint32_t kind, uint32_t note, uint32_t vel)
{
    tape_rq_push(kind, tape_head, note, 0, vel);
    tape_poll();
}

static uint32_t rng = 1;
static uint32_t rnd(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return n ? rng % n : 0;
}

/* -------------------------------------------------------------------------------- tests --- */

static void test_random(uint32_t ops, uint32_t seed)
{
    uint32_t op, inserts = 0, refused = 0, deletes = 0, merges = 0, clears = 0;
    const char *bad = 0;
    char what[128];
    reset();
    rng = seed ? seed : 1;
    tracking = 1;
    start();
    for (op = 0; op < ops && !bad; op++) {
        uint32_t dice = rnd(1000), grow = (op / 5000u) & 1u ? 250u : 600u, track = rnd(TAPE_TRACKS);
        if (op % 997u == 0)
            yield_every = 1u << rnd(5);
        if (dice < grow) {
            uint32_t kind = rnd(3), at = kind ? rnd(tape_length()) : rnd(tape_head + 1u);
            uint32_t pass = kind == 0 ? tape_pass : kind == 1 ? (uint16_t)(tape_pass - 1u) : TAPE_ANY_PASS;
            uint32_t total = tp_trk[0].count + tp_trk[1].count + tp_trk[2].count + tp_trk[3].count;
            if (tag_insert(track, at, 1u + rnd(400), pass))
                inserts++;
            else if (total == TAPE_N || tp_limbo != TAPE_END)
                refused++;
            else
                bad = "insert refused with free nodes";
        } else if (dice < 960u) {
            if (ref_n[track]) {
                tag_delete(track, rnd(ref_n[track]));
                deletes++;
            }
        } else if (dice < 968u) {
            uint32_t src = rnd(TAPE_TRACKS);
            if (src != track) {
                tape_merge(track, src);
                ref_merge(track, src);
                merges++;
            }
        } else if (dice < 970u) {
            tag_clear(track);
            clears++;
        } else
            host_block(rnd(20));
        if (!bad)
            bad = consistent(1);
    }
    play(2u * tape_length(), 7);
    snprintf(what, sizeof what, "random %u ops (%u ins, %u full, %u del, %u merge, %u clear): %s", (unsigned)ops,
             (unsigned)inserts, (unsigned)refused, (unsigned)deletes, (unsigned)merges, (unsigned)clears,
             bad ? bad : "lists = reference");
    check(what, !bad);
    snprintf(what, sizeof what, "random: %u passes, no double fire (%u), none in its pass (%u), none missed (%u)",
             (unsigned)tape_pass, (unsigned)double_fires, (unsigned)same_pass_fires, (unsigned)missed);
    check(what, !double_fires && !same_pass_fires && !missed && tape_pass > 10u);
    tracking = 0;
}

static void test_cursor(void)
{
    reset();
    yield_on = 0;
    put(0, 0, 60, 0, 50);
    put(0, 1000, 61, 0, 50);
    put(0, 2000, 62, 0, 50);
    start();
    play_to(1500, 10);
    tape_insert(0, 1500 | 70u << 20, 50 | 100u << 20, tape_pass);               /* same pass, at the head */
    tape_insert(0, 1400 | 71u << 20, 50 | 100u << 20, tape_pass);               /* same pass, behind */
    tape_insert(0, 1000 | 74u << 20, 50 | 100u << 20, tape_pass);               /* same pass, = a played node */
    tape_insert(0, 1500 | 72u << 20, 50 | 100u << 20, (uint16_t)(tape_pass - 1u));   /* earlier pass, at the head */
    tape_insert(0, 1490 | 73u << 20, 50 | 100u << 20, (uint16_t)(tape_pass - 1u));   /* earlier pass, behind */
    play_to(tape_length() - 10u, 10);
    check("insert at / behind the cursor, this pass: same-pass none, earlier-pass ahead once",
          fires[0][60] == 1 && fires[0][61] == 1 && fires[0][62] == 1 && !fires[0][70] && !fires[0][71] &&
              !fires[0][74] && fires[0][72] == 1 && !fires[0][73]);
    play(tape_length(), 10);
    check("insert at / behind the cursor, next pass: each once more",
          fires[0][60] == 2 && fires[0][61] == 2 && fires[0][62] == 2 && fires[0][70] == 1 && fires[0][71] == 1 &&
              fires[0][74] == 1 && fires[0][72] == 2 && fires[0][73] == 1 && !consistent(0));
}

static void test_record_pass(void)
{
    uint32_t len;
    uint16_t node;
    reset();
    start();
    play_to(500, 10);
    rec_event(TR_ON, 64, 90);
    play_to(800, 10);
    rec_event(TR_OFF, 64, 0);
    node = tp_trk[0].first;
    len = node == TAPE_END ? 0 : TP_LEN(tp_w1[node]);
    play_to(0, 10);
    check("recorded node: not in its own pass", node != TAPE_END && !fires[0][64] && len == 300);
    play_to(1000, 10);
    check("recorded node: next pass once, its length", fires[0][64] == 1 && !gate[0][64] &&
                                                           off_at[0][64] - on_at[0][64] == 300);
}

static void test_held_wrap(void)
{
    uint16_t node;
    reset();
    tape_set_len(1);
    start();
    play_to(1800, 10);
    rec_event(TR_ON, 65, 90);
    node = tp_trk[0].first;
    play_to(100, 10);
    check("held across the loop end: not played while held", !fires[0][65] && (tp_w1[node] & TP_HELD));
    rec_event(TR_OFF, 65, 0);
    check("held across the loop end: length 1920 - 1800 + 100", TP_LEN(tp_w1[node]) == 220 && !tp_open_n);
    play_to(0, 10);
    check("held across the loop end: plays once in the next pass", fires[0][65] == 1);
    play_to(1000, 10);
    rec_event(TR_ON, 66, 90);
    play(2u * TAPE_BAR + 100u, 10);
    rec_event(TR_OFF, 66, 0);
    node = tp_trk[0].first;
    check("held over two wraps: open kept, length clamped to L", TP_NOTE(tp_w0[node]) == 66 &&
                                                                     TP_LEN(tp_w1[node]) == TAPE_BAR && !tp_open_n);
}

static void test_replace(void)
{
    uint32_t k, old = 0, new_fires = 0;
    reset();
    yield_on = 0;
    for (k = 0; k < 16; k++)
        put(0, k * 480u, 40 + k, 0, 100);
    start();
    play(tape_length() - 10u, 10);
    song.rec = 1;
    tp_overdub = 0;
    for (k = 0; k < 16; k++) {
        play_to(240u + k * 480u, 10);
        rec_event(TR_ON, 80 + k, 90);
        play_to(340u + k * 480u, 10);
        rec_event(TR_OFF, 80 + k, 0);
    }
    play_to(0, 10);
    song.rec = 0;
    for (k = 0; k < 16; k++)
        old += fires[0][40 + k];
    tape_sweep();
    check("replace mode: the pass ghosts the old notes, not one plays", old == 16 && tp_trk[0].count == 16);
    play(tape_length(), 10);
    old = 0;
    for (k = 0; k < 16; k++) {
        old += fires[0][40 + k];
        new_fires += fires[0][80 + k];
    }
    check("replace mode: one pass replaced, the new ones play", old == 16 && new_fires == 16 && !consistent(0));
}

static void test_dedup(void)
{
    uint32_t pass, k, ok = 1, per[8] = {0};
    uint16_t node;
    reset();
    tape_set_len(1);
    start();
    for (pass = 0; pass < 8; pass++)
        for (k = 0; k < 8; k++) {
            int32_t jitter = (int32_t)rnd(11) - 5;
            uint32_t at = (uint32_t)((int32_t)(k * 240u) + jitter + (int32_t)TAPE_BAR) % TAPE_BAR;
            play_to(at, 3);
            rec_event(TR_ON, 50 + k, 90);
            play_to((at + 100u) % TAPE_BAR, 3);
            rec_event(TR_OFF, 50 + k, 0);
        }
    for (node = tp_trk[0].first; node != TAPE_END; node = tp_nx[node]) {
        ok &= TP_LEN(tp_w1[node]) >= 90u && TP_LEN(tp_w1[node]) <= 110u && TP_NOTE(tp_w0[node]) - 50u < 8u;
        per[(TP_NOTE(tp_w0[node]) - 50u) & 7u]++;
    }
    for (k = 0; k < 8; k++)
        ok &= per[k] == 1;
    check("dedupe: 8 sequencer passes (jitter +-5, across the loop end) leave one node a note",
          tp_trk[0].count == 8 && ok && !consistent(0));
}

static void test_merge(void)
{
    uint32_t k;
    const char *bad;
    reset();
    tracking = 1;
    for (k = 0; k < 700; k++) {
        tag_insert(0, rnd(tape_length()) & ~31u, 1u + rnd(300), TAPE_ANY_PASS);
        tag_insert(1, rnd(tape_length()) & ~31u, 1u + rnd(300), TAPE_ANY_PASS);
    }
    start();
    play(tape_length() + 3000u, 5);
    yield_every = 1;
    yield_adv = 3;
    tape_merge(0, 1);
    ref_merge(0, 1);
    bad = consistent(1);
    play(2u * tape_length(), 5);
    check("merge 700 + 700 while playing (ISR each step): every node, stable order", !bad && tp_trk[0].count == 1400 &&
                                                                                    tp_trk[1].first == TAPE_END);
    check("merge while playing: no double fire, none missed", !double_fires && !missed && tape_pass >= 3u);
    tracking = 0;
}

static void test_overlap(void)
{
    uint32_t stop_at;
    reset();
    put(0, 0, 60, 0, 500);
    put(1, 100, 60, 0, 100);
    put(2, 1000, 61, 0, 100);
    put(3, 1050, 61, 0, 500);
    start();
    play(1700, 10);
    check("overlapping same note: one voice, longest end wins, nothing hangs",
          fires[0][60] == 2 && !gate[0][60] && off_at[0][60] == 510 && fires[0][61] == 2 && !gate[0][61] &&
              off_at[0][61] == 1550);
    play_to(1060, 10);
    stop_at = song_ticks;
    tape_voices_end(-1);
    song.playing = 0;
    check("STOP: every tape voice ends", gates_off() && off_at[0][61] == stop_at);
    tape_poll();
}

static void test_all_end(void)
{
    uint32_t k;
    reset();
    for (k = 0; k < 300; k++)
        put(k & 3u, rnd(tape_length()), rnd(128), rnd(NTRK), 1u + rnd(3000));
    start();
    play(tape_length() + 1234u, 7);
    tape_voices_end(-1);
    check("300 notes, STOP mid-pass: voice table empty, every gate off", gates_off());
    start();
    play(tape_length() / 2u, 7);
    for (k = 0; k < TAPE_TRACKS; k++)
        tape_clear(k);
    play(4000, 7);
    check("clear all while sounding: kill ends the voices, nothing hangs, every node free or in limbo",
          gates_off() && !consistent(0) && !(tp_trk[0].count | tp_trk[1].count | tp_trk[2].count | tp_trk[3].count));
}

static void test_full(void)
{
    uint32_t k, n = 0;
    uint16_t node;
    reset();
    for (k = 0; k < TAPE_N; k++)
        n += put(k & 3u, rnd(tape_length()), 60, 0, 10) != TAPE_END;
    tp_full = 0;
    node = put(0, 100, 61, 0, 10);
    check("tape full: 1440 in, the next refused, TAPE FULL flagged, lists intact",
          n == TAPE_N && node == TAPE_END && tp_full && !consistent(0));
    start();
    rec_event(TR_ON, 62, 90);
    rec_event(TR_OFF, 62, 0);
    check("tape full: a recorded note dropped, nothing left open", !tp_open_n && !consistent(0));
    song.playing = 0;
    yield_on = 0;
    tape_delete(0, tp_trk[0].first);
    node = put(1, 100, 63, 0, 10);
    check("freed node not reused within the ISR block (limbo)", node == TAPE_END);
    host_block(0);
    node = put(1, 100, 63, 0, 10);
    check("freed node reused after a block (stopped too)", node != TAPE_END && !consistent(0));
}

static void test_yield_edit(void)
{
    uint32_t k, rounds;
    const char *bad = 0;
    reset();
    tracking = 1;
    start();
    yield_every = 1;
    yield_adv = 2;
    for (rounds = 0; rounds < 40 && !bad; rounds++) {
        for (k = 0; k < 60; k++)
            tag_insert(rnd(2), rnd(tape_head + 1u), 1u + rnd(200), tape_pass);
        for (k = 0; k < 30; k++)
            if (ref_n[2])
                tag_delete(2, rnd(ref_n[2]));
        for (k = 0; k < 40; k++)
            tag_insert(2, rnd(tape_length()), 1u + rnd(200), (uint16_t)(tape_pass - 1u));
        k = rnd(2);
        tape_merge(3, k);
        ref_merge(3, k);
        bad = consistent(1);
        if (tp_trk[3].count > 900u)
            tag_clear(3);
    }
    check("main-loop edits with the ISR at every walk step stay consistent", !bad && blocks > 1000u);
    check("main-loop edits with the ISR: no double fire, none in its pass, none missed",
          !double_fires && !same_pass_fires && !missed);
    tracking = 0;
}

static void test_length(void)
{
    reset();
    put(0, 1000, 70, 0, 50);
    put(0, 3000, 71, 0, 50);
    put(0, 5000, 72, 0, 50);
    put(0, 7000, 73, 0, 50);
    start();
    play(tape_length(), 10);
    play_to(6000, 10);
    tape_set_len(2);
    check("shorten 4 -> 2 bars at 6000: the head wraps to 2160", tape_head == 2160 && tape_length() == 2 * TAPE_BAR);
    play(2u * tape_length(), 10);
    check("shortened: the notes inside play each pass, past L none, all kept",
          fires[0][70] == 4 && fires[0][71] == 4 && fires[0][72] == 2 && fires[0][73] == 1 && tp_trk[0].count == 4);
    tape_set_len(4);
    play_to(0, 10);
    play(tape_length(), 10);
    check("restored to 4 bars: every note plays again", fires[0][70] == 5 && fires[0][71] == 6 && fires[0][72] == 4 &&
                                                            fires[0][73] == 3 && !consistent(0));
}

static void test_codec(void)
{
    static uint8_t store[TAPE_N * 8u + TAPE_PAGE];
    static uint32_t want0[TAPE_N], want1[TAPE_N];
    uint32_t k, bytes = 0, page, wanted = 0, ok = 1;
    uint16_t counts[TAPE_TRACKS], node;
    tape_it_t it;
    reset();
    for (k = 0; k < 1300; k++) {
        node = put(rnd(3), rnd(tape_length()), rnd(128), rnd(NTRK), 1u + rnd(5000));
        if (rnd(10) == 0)
            tp_w0[node] |= TP_GHOST;
        else if (rnd(10) == 0)
            tp_w1[node] |= TP_HELD;
    }
    for (k = 0; k < TAPE_TRACKS; k++)
        for (node = tp_trk[k].first; node != TAPE_END; node = tp_nx[node])
            if (tp_saved(node)) {
                want0[wanted] = tp_w0[node];
                want1[wanted++] = tp_w1[node];
            }
    tape_save_counts(counts);
    tape_enc_begin(&it);
    while ((page = tape_enc_page(&it, store + bytes)) != 0u)
        bytes += page;
    check("encode: 8 B a record, ghosts and held left out", bytes == wanted * 8u &&
                                                               (uint32_t)(counts[0] + counts[1] + counts[2] + counts[3]) == wanted);
    tape_init();
    tape_load_begin(counts);
    for (page = 0; page < bytes; page += TAPE_PAGE)
        tape_dec_page(store + page, bytes - page < TAPE_PAGE ? bytes - page : TAPE_PAGE);
    tape_load_end();
    for (k = 0, wanted = 0; k < TAPE_TRACKS; k++) {
        ok &= tp_trk[k].count == counts[k];
        for (node = tp_trk[k].first; node != TAPE_END; node = tp_nx[node], wanted++)
            ok &= tp_w0[node] == want0[wanted] && tp_w1[node] == want1[wanted];
    }
    check("decode: the same lists, page by page", ok && !consistent(0));
    store[0 + 3] |= 0x38u;                          /* inst 7: dropped, still counted */
    tape_load_begin(counts);
    for (page = 0; page < bytes; page += TAPE_PAGE)
        tape_dec_page(store + page, bytes - page < TAPE_PAGE ? bytes - page : TAPE_PAGE);
    tape_load_end();
    check("decode: a bad record dropped, the next tracks still in place",
          tp_trk[0].count == counts[0] - 1u && tp_trk[1].count == counts[1] && !consistent(0));
}

static void test_queue(void)
{
    uint32_t k, held = 0;
    uint16_t node;
    reset();
    start();
    for (k = 0; k < 20; k++) {
        play(10, 10);
        tape_rq_push(TR_ON, tape_head, 30 + k, 0, 90);
    }
    tape_poll();
    check("open table full: the oldest closed at its length so far", tp_open_n == TAPE_OPEN && tp_trk[0].count == 20);
    for (k = 0; k < 200; k++)
        tape_rq_push(TR_OFF, tape_head, 100, 1, 0);
    check("record queue overflow flagged", tp_rq_lost);
    tape_poll();
    for (node = tp_trk[0].first; node != TAPE_END; node = tp_nx[node])
        held += (tp_w1[node] & TP_HELD) != 0;
    check("overflow: every open note closed", !tp_open_n && !held && !tp_rq_lost);
    rec_event(TR_ON, 90, 90);
    tape_rq_push(TR_CLOSE_INST, tape_head, 0, 0, 0);
    tape_poll();
    check("TR_CLOSE_INST closes that instrument's notes", !tp_open_n);
}

int main(int argc, char **argv)
{
    uint32_t ops = argc > 1 ? (uint32_t)strtoul(argv[1], 0, 10) : 100000u;
    uint32_t seed = argc > 2 ? (uint32_t)strtoul(argv[2], 0, 10) : 1u;
    char what[96];
    test_cursor();
    test_record_pass();
    test_held_wrap();
    test_replace();
    test_dedup();
    test_merge();
    test_overlap();
    test_all_end();
    test_full();
    test_yield_edit();
    test_length();
    test_codec();
    test_queue();
    test_random(ops, seed);
    snprintf(what, sizeof what, "irq-off sections: %u, at most %u stores, never nested", (unsigned)sections,
             (unsigned)max_stores);
    check(what, max_stores <= 8u && !irq_bad);
    printf("tape: %s\n", failed ? "FAILED" : "all ok");
    return failed != 0;
}
