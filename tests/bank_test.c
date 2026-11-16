/* SPDX-License-Identifier: GPL-3.0-only */
/* Skiff's pattern pool (firmware/src/bank.c) against simulated NOR flash, with storage.c, user presets and projects
 * as the firmware has them:
 *   a pattern kept exactly as played (every step field, NUDGE, LEN DIV SWING GATE DIR, automation and locks) and
 *   loaded back whole, over a power cycle; the pool's sectors inside the store, apart from every other object and from
 *   the package's BTIF; a write torn at any page leaves the pattern as it was; a damaged newest copy falls back to the
 *   copy before; writes go round the spare sectors; the sequence wraps; nothing is written while the transport runs.
 *   (phase 5) What a track has is kept as the pattern it is on (waiting, then written at the quiet stop), a new one
 *   gets the first free id; PATTERNS FULL; a pattern without room for its automation keeps its own; a project names
 *   each track's pattern (its entry A).
 * The SAVE > PATTERNS page: tests/ui_test.c test_bank. Built and run by tests/run_tests.sh. */
#ifndef BOARD_SMK37
#define BOARD_SMK37 0                                   /* -DBOARD_SMK37=1: the pool on the SMK-37 Pro layout */
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#define FELUCCA_FLASH 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
static void panel_setup(void) {}

static uint8_t nor[0x100000], flash_ok = 1;
static int fail_after = -1, erase_fail;
static uint32_t erases[256];                      /* per sector (offset >> 12) */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    erases[(off >> 12) & 255u]++;
    if (erase_fail)
        return -8;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)   /* (fail_after: the power goes after that many) */
{
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static void fm1_wdt_feed(void) {}
static uint32_t fl_jedec_ram(void) { return 0; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
#include "../firmware/src/bank.c"

static int check(const char *what, int ok)
{
    printf("bank: %-72s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void fresh(void)                           /* a new unit: flash erased, the tracks at power-on, on no pattern */
{
    memset(nor, 0xFF, sizeof nor);
    memset(erases, 0, sizeof erases);
    memset(&song, 0, sizeof song);
    sel_set(0);
    memset(trk, 0, sizeof trk);
    memset(&ui, 0, sizeof ui);
    memset(&motion, 0, sizeof motion);
    memset(proj_slot, 0, sizeof proj_slot);
    host_tracks_init();
    undo_clear();
    flash_ok = 1;
    fail_after = -1;
    erase_fail = 0;
    bank_boot();
    memset(trk_pid, 0, sizeof trk_pid);
    motion_lost = 0;
    (void)arm_cancel();
    transport_req = 0;
}
static void power_cycle(void) { bank_boot(); }    /* (the RAM gone: the pool read from flash again) */
static uint32_t erases_total(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < 256u; i++)
        n += erases[i];
    return n;
}

/* track k: a pattern using every field a step has (seed: another one) and a full share of motion */
static void rich(uint32_t k, uint32_t seed)
{
    track_t *t = &trk[k];
    uint32_t i, j;
    motion_clear(t);
    for (i = 0; i < NSTEP; i++) {
        step_t *s = &t->step[i];
        uint32_t r = (i * 7u + seed * 13u) % 11u;
        memset(s, 0, sizeof *s);
        s->time = (uint8_t)(r == 9u ? ST_TIE : r == 10u ? ST_REST : ST_NOTE);
        if (s->time == ST_NOTE) {
            s->n = (uint8_t)(1u + (i + seed) % 4u);
            for (j = 0; j < s->n; j++)
                s->note[j] = (uint8_t)(24u + (i * 5u + j * 7u + seed) % 100u);
            s->vel = (uint8_t)(1u + (i * 37u + seed) % 127u);
            s->flags = (uint8_t)((i % 3u == 0u ? SF_ACCENT : 0u) | (i % 5u == 0u ? SF_SLIDE : 0u));
            s->hit = (uint8_t)(i % 6u == 0u ? 0x81u : i % 6u == 1u ? 0x24u : 0u);
            s->acc = (uint8_t)(s->hit & 0x80u);
        }
        step_set_chance(s, (i * 11u + seed) % 101u);
        if (i % 7u == 3u)
            step_set_rflam(s, 5);                     /* FLAM */
        else
            step_set_ratchet(s, 1u + (i + seed) % 4u);
        s->cond = (uint8_t)((i + seed) % TC_COUNT);
        s->nudge = (int8_t)((int32_t)((i * 13u + seed) % 64u) - 32);
    }
    t->p[P_SLEN] = (int16_t)(37 + (int32_t)seed % 20);
    t->p[P_SDIV] = 3;
    t->p[P_SSWING] = 61;
    t->p[P_SGATE] = 90;
    t->p[P_DIR] = DIR_PING;
    for (i = 0; i < 16u; i++) {                   /* 16 automation events, 16 locks (inside each value's range) */
        const param_desc_t *d = param_desc_of(eng_idx(t->eng_req), P_E0 + i % 8u);
        int32_t span = d->max - d->min + 1;
        motion_set_event(t, (i * 4u + seed) % NSTEP, P_REV, (int16_t)(i * 7u + seed % 9u));
        motion_set_lock(t, (i * 4u + 1u + seed) % NSTEP, P_E0 + i % 8u,
                        (int16_t)(d->min + (int32_t)((i * 5u + seed) % (uint32_t)(span > 64 ? 64 : span))));
    }
}
typedef struct { step_t step[NSTEP]; int16_t p[5]; motion_store_t m; } snap_t;
static void snap(snap_t *s, uint32_t k)           /* what bank.c keeps of track k */
{
    memset(s, 0, sizeof *s);
    memcpy(s->step, trk[k].step, sizeof s->step);
    for (uint32_t i = 0; i < 5u; i++)
        s->p[i] = trk[k].p[BK_TIME[i]];
    motion_snapshot_track(&trk[k], &s->m);
}
static int same(const snap_t *a, const snap_t *b) { return !memcmp(a, b, sizeof *a); }
static void wipe(uint32_t k)                      /* the track emptied: steps, timing, motion */
{
    track_t *t = &trk[k];
    motion_clear(t);
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    t->p[P_SDIV] = 2;
    t->p[P_SSWING] = 0;
    t->p[P_SGATE] = 64;
    t->p[P_DIR] = DIR_FWD;
}
static void detach(uint32_t k) { trk_pid[k] = 0; wipe(k); }   /* emptied, on no pattern: nothing of it kept */

static int layout(void)
{
    int bad = 0, ok = 1;
    uint32_t i, o, c;
    for (i = 0; i < BK_NSECT; i++) {
        uint32_t s = bk_sector(i);
#if BOARD_SMK37                                  /* (-DBOARD_SMK37=1: the SMK-37 Pro's USRFLASH, fm1_flash_map.h) */
        ok &= s >= FL_BANK_LO && s + 0x1000u <= FL_BANK_HI && FL_GUARD(s, 0x1000u) &&
              (s >= SMP_USER_BASE + SMP_USER_FLASH_SLOTS * SMP_USER_SIZE || s + 0x1000u <= SMP_USER_BASE);
#else
        ok &= (s >= 0xE7000u && s + 0x1000u <= 0xE9000u) || (s >= 0xEA000u && s + 0x1000u <= 0xFC000u);
#endif
        for (o = 0; o < OBJ_COUNT; o++)           /* apart from every storage.c object's sectors */
            for (c = 0; c < 2u; c++)
                ok &= st_sector(o, c) != s;
        ok &= s != 0xE9000u || BOARD_SMK37;
        for (o = 0; o < i; o++)
            ok &= bk_sector(o) != s;
    }
    bad += check(BOARD_SMK37 ? "20 sectors (SMK-37 Pro): USRFLASH after the user slot, nor any other object's" :
                 "20 sectors: the VM area's last two, the package's USR region; not BTIF, nor any other object's", ok);
    bad += check("a pattern 888 bytes (steps, NUDGE, timing, 64 motion records), 4 a sector, 16 chunks + 4 spare",
                 BK_REC == 888u && BK_LEN <= 3840u && BK_CHUNKS == 16u && BK_NSECT == 20u);
    return bad;
}

/* track k's pattern kept as pattern id (NPAT: the first free), the track on it, written now (SAVE > PATTERNS' put
 * before the song layer went): 0, 2 not (the transport runs, a flash error), 3 no flash, 4 PATTERNS FULL */
static int put_as(uint32_t k, uint32_t id)
{
    if (!BK_ON)
        return 3;
    if (transport_busy())
        return 2;
    if (id == NPAT && (id = pool_alloc()) == NPAT)
        return 4;
    if (!trk_rec(k) || pat_keep_now(id, bk_rec))
        return 4;
    trk_pid[k] = (uint8_t)(id + 1u);
    trk_sig[k] = proj_hash(bk_rec, BK_REC);
    while (pend_free() < PEND_N)
        if (pat_flush_one())
            return 2;
    return 0;
}
/* track k onto pattern id, stopped, as a load: what it had kept first. 0, 1 nothing there, 2 without its automation,
 * 4 PATTERNS FULL */
static int go_to(uint32_t k, uint32_t id)
{
    if (trk_keep(k))
        return 4;
    return pool_used(id) ? pat_load(&trk[k], id) : 1;
}

static int fidelity(void)
{
    int bad = 0, ok;
    uint32_t e0;
    snap_t a, b, c;
    static uint8_t r[BK_REC], r0[BK_REC];
    static step_t st[NSTEP];
    static motion_store_t ms;
    int16_t v[5];
    fresh();
    rich(1, 3);
    snap(&a, 1);
    ok = put_as(1, NPAT) == 0 && trk_pat(1) == 0u && pool_len(0) == (uint32_t)trk[1].p[P_SLEN] && !pool_used(1) &&
         a.m.count >= 24u;
    detach(1);
    ok &= go_to(1, 0) == 0;
    snap(&b, 1);
    bad += check("track 2's pattern kept and back: every step field, NUDGE, LEN DIV SWING GATE DIR, 16 events, "
                 "16 locks", ok && same(&a, &b));
    rich(2, 7);
    snap(&c, 2);
    ok = put_as(2, NPAT) == 0;
    power_cycle();
    detach(1);
    detach(2);
    ok &= pool_used(0) && pool_used(1) && go_to(2, 1) == 0 && go_to(1, 0) == 0;
    snap(&b, 1);
    ok &= same(&a, &b);
    snap(&b, 2);
    bad += check("  over a power cycle, another track's beside it", ok && same(&c, &b));
    ok = !pool_used(2) && trk_pat(1) == 0u && trk_pat(2) == 1u && !undo_n;
    bad += check("  (phase 5) the first free ids; each track on its pattern; no undo copy", ok);
    detach(0);
    ok = go_to(0, 0) == 2;                        /* T2 and T3 hold 64 records, all there is: AUTOMATION FULL */
    snap(&b, 0);
    bad += check("  a pattern two tracks are on, onto T1; the motion pool full: the steps load, the automation not",
                 ok && !memcmp(a.step, b.step, sizeof a.step) && !motion_count(&trk[0]) && (motion_lost & 1u));
    e0 = erases_total();
    ok = !pat_read(0, r0) && keep_all() == 0 && erases_total() == e0;   /* (what was loaded, kept: the same, nothing) */
    motion_set_event(&trk[0], 2, P_REV, 9);       /* (refused: the pattern's own did not fit) */
    trk[0].step[1].vel ^= 1u;                     /* an edit: kept, the pattern's own automation with it */
    ok &= !motion_count(&trk[0]) && keep_all() == 0 && !pat_decode(&trk[3], 0, st, v, &ms) &&
          st[1].vel == (a.step[1].vel ^ 1u) && ms.count == a.m.count;
    trk[0].step[1].vel ^= 1u;
    ok &= keep_all() == 0 && !pat_read(0, r) && !memcmp(r, r0, BK_REC);
    bad += check("  .. kept: the same bytes (nothing written); an edit kept, its stored automation with it; none taken",
                 ok);
    detach(2);                                    /* (T3's 32 records gone: room) */
    ok = go_to(0, 0) == 0;
    snap(&b, 0);
    bad += check("  room again: on T1 whole, the automation placed on T1", ok && !memcmp(a.step, b.step, sizeof a.step) &&
                 !memcmp(a.p, b.p, sizeof a.p) && b.m.count == a.m.count && (b.m.event[0].place >> 6) == 0u &&
                 !(motion_lost & 1u));
    ok = go_to(3, 5) == 1 && pool_refs(0) == 2u && pool_refs(1) == 0u;   /* (T1 T2 on 0; T3 detached: 1 unused) */
    bad += check("  an empty id loads nothing; what uses a pattern: the tracks on it", ok);
    return bad;
}

/* (phase 5) keeping edits: records wait (PEND_N) for a quiet stop, reads see them first, flushes write them */
static int keep(void)
{
    int bad = 0, ok;
    static uint8_t r1[BK_REC], r2[BK_REC], r3[BK_REC], got[BK_REC], zero[BK_REC];
    uint32_t e0, e;
    fresh();
    rich(1, 3);
    ok = bk_pack(r1, &trk[1]);
    rich(2, 7);
    ok &= bk_pack(r2, &trk[2]);
    e0 = erases_total();
    ok &= pat_keep_rec(5, r1) == 0 && pool_used(5) && !bk.len[5] && pend_free() == PEND_N - 1u &&
          !pat_read(5, got) && !memcmp(got, r1, BK_REC) && erases_total() == e0;
    bad += check("keep: a record waits (not written), reads see it, the pool counts it", ok);
    ok = pat_keep_rec(5, r2) == 0 && pend_free() == PEND_N - 1u && !pat_read(5, got) && !memcmp(got, r2, BK_REC);
    bad += check("  the same id again: the waiting copy replaced, no second slot", ok);
    for (ok = 1, e = 6; e < 5u + PEND_N; e++)            /* (ids 5 .. 4 + PEND_N waiting) */
        ok &= pat_keep_rec(e, r1) == 0;
    ok &= !pend_free() && pat_keep_rec(5u + PEND_N, r1) == 1 && !pool_used(5u + PEND_N);
    bad += check("  PEND_N waiting (one a track): one more has no room (1), nothing taken", ok);
    ok = pat_flush_one() == 0 && pend_free() == 3u && bk.len[5] && bk.len[7] && !bk.len[8];   /* (5 6 7: one chunk) */
    ok &= pat_flush_one() == 0 && pend_free() == PEND_N && pat_flush_one() == 0 && erases_total() == e0 + 2u && bk.len[8];
    power_cycle();
    ok &= pool_used(5) && !pat_read(5, got) && !memcmp(got, r2, BK_REC) && pool_used(6) && pend_free() == PEND_N;
    bad += check("  flushed a chunk a call (its waiting patterns in one write), then kept over a power cycle", ok);
    e0 = erases_total();
    ok = pat_keep_rec(5, r2) == 0 && pend_free() == PEND_N && erases_total() == e0;
    bad += check("  the same as stored: nothing waits, nothing written", ok);
    ok = pat_keep_rec(6, zero) == 0 && !pool_used(6) && pat_flush_one() == 0 && !bk.len[6] && !pool_used(6);
    bad += check("  an empty record empties the pattern (at the flush)", ok);
    ok = pool_alloc() == 0u && pat_keep_rec(0, r1) == 0 && pool_alloc() == 1u;
    bad += check("  pool_alloc: the first empty id, a waiting one counts", ok);
    memcpy(r3, r2, BK_REC);
    r3[BK_STEPS] ^= 1u;
    ok = pool_find(r2) == 5u && pool_find(r1) == 0u && pool_find(r3) == NPAT;
    bad += check("  pool_find: the id with the same record (waiting or stored), else none", ok);
    song.playing = 1;
    ok = pat_flush_one() == 2 && pend_free() == PEND_N - 1u;
    song.playing = 0;
    ok &= pat_flush_one() == 0 && pend_free() == PEND_N;
    bad += check("  playing: no flush (it waits); stopped: written", ok);
    return bad;
}

/* ---- (phase 5) a track's steps kept as the pattern it is on; new ones a new pattern; a project names them ---- */
static void simple(uint32_t k, uint32_t note, uint32_t len, uint32_t dir)   /* new steps (on no pattern): step i
                                                                             * plays note + i */
{
    track_t *t = &trk[k];
    uint32_t i;
    trk_pid[k] = 0;
    wipe(k);
    for (i = 0; i < len; i++)
        t->step[i] = (step_t){{(uint8_t)(note + i)}, 1, ST_NOTE, 0, 100};
    t->p[P_SLEN] = (int16_t)len;
    t->p[P_DIR] = (int16_t)dir;
}
static int entries(void)
{
    int bad = 0, ok;
    uint32_t i, a, b;
    track_t *t = &trk[0];
    static uint8_t r[BK_REC];
    static project_t got;
    fresh();
    simple(0, 30, 4, DIR_FWD);                    /* new steps on T1, on no pattern */
    ok = !trk_pid[0] && keep_poll() == 1 && trk_pid[0] && pend_free() == PEND_N && pool_used(trk_pat(0)) &&
         keep_poll() == 0;
    a = trk_pat(0);
    bad += check("entries: new steps kept at the quiet stop (one write a poll): the first free id, the track on it", ok);
    simple(0, 50, 4, DIR_FWD);                    /* more new steps */
    ok = keep_all() == 0 && trk_pat(0) != a && trk_pat(0) != PAT_NONE;
    b = trk_pat(0);
    bad += check("  .. more new steps: another id, the first one as it was", ok);
    t->step[1].note[0] = 53;                      /* an edit, then the track to a */
    ok = go_to(0, a) == 0 && t->step[0].note[0] == 30 && pend_at(b) < PEND_N;
    ok &= go_to(0, b) == 0 && t->step[0].note[0] == 50 && t->step[1].note[0] == 53 && !undo_n;
    bad += check("  leaving a pattern keeps it first (waiting for the quiet stop); back on it: the edit there", ok);
    ok = keep_all() == 0 && pend_free() == PEND_N;
    t->step[2].note[0] = 54;                      /* an edit not kept yet when the power goes */
    power_cycle();
    detach(0);
    ok &= go_to(0, b) == 0 && t->step[1].note[0] == 53 && t->step[2].note[0] == 52;
    bad += check("  over a power cycle: what was kept (written stopped), not the edit after it (gone, as before)", ok);
    ok = project_save_as(1, "PAT") == 0 && proj_import(&got, &proj_slot[1], sizeof proj_slot[1]) &&
         got.ent[0][0][0] == b + 1u && !got.cur[0] && !got.ent[1][0][0] && pool_refs(b) == 2u;
    detach(0);
    project_load(1);
    ok &= trk_pat(0) == b && t->step[1].note[0] == 53;
    bad += check("  a project names each track's pattern (its entry A) and loads it back; a saved one counts as a use", ok);

    ok = bk_pack(r, &trk[3]);                     /* (an empty track's pattern) */
    for (i = 0; i < NPAT; i++)                    /* the pool full: every id holds a pattern */
        if (!pool_used(i))
            ok &= pool_put(i, r) == 0;
    simple(0, 70, 4, DIR_FWD);                    /* new steps: no id for them */
    ok &= pool_alloc() == NPAT && keep_all() == 1 && !trk_pid[0] && go_to(0, a) == 4 && t->step[0].note[0] == 70 &&
          project_save_as(2, 0) == 1 && str_eq(ui.msg, "PATTERNS FULL");
    ok &= put_as(0, b) == 0 && trk_pat(0) == b;
    bad += check("  the pool full: new steps get no id (PATTERNS FULL: no load, no project save; the track stays on "
                 "them); kept over a pattern it goes", ok);
    return bad;
}

/* (phase 6) the last track's automation (with six tracks its place 5 << 6 | 63 = 0x17F: past a byte) kept in a pattern
 * and loaded back onto it; a record holding a value no firmware wrote (200: a byte in RAM would wrap it into range):
 * its automation dropped, the steps load */
static int last_track(void)
{
    int bad = 0, ok;
    uint32_t k = NTRK - 1u, i, id, n = 0;
    static uint8_t r[BK_REC];
    static step_t st[NSTEP];
    static motion_store_t ms;
    int16_t v[5];
    fresh();
    simple(k, 40, 64, DIR_FWD);
    ok = motion_put(&trk[k], 63, P_PAN, -64, MOTION_LOCK) == 0 && keep_all() == 0 && trk_pid[k] != 0u;
    id = trk_pid[k] - 1u;
    detach(k);
    ok &= go_to(k, id) == 0;
    for (i = 0; i < motion.count; i++)
        n += motion.event[i].place == (k << 6 | 63u) && (motion.event[i].id == P_PAN && motion.event[i].lock) &&
             motion.event[i].value == -64;
    bad += check("the last track's lock on step 64 (its place past a byte with six) kept and loaded back",
                 ok && n == 1u && motion_count(&trk[k]) == 1u);
    ok = !pat_read(id, r) && (r[7] & 127u) == 1u;
    r[BK_MOT + 2u] = 200u;                            /* (its value: 200, 0) */
    r[BK_MOT + 3u] = 0u;
    ok &= pat_keep_rec(id, r) == 0 && !pat_decode(&trk[k], id, st, v, &ms) && !ms.count && st[5].note[0] == 45u;
    bad += check("  a record holding a value outside -64..127: its automation dropped, the steps load", ok);
    /* (Skiff phase 9) the motion's ids as a store of PROJ_NP has them (firmware before reads the record), unless the
     * motion names a parameter added since: then today's */
    fresh();
    simple(k, 40, 64, DIR_FWD);
    ok = motion_put(&trk[k], 3, P_E5, 20, MOTION_LOCK) == 0 && keep_all() == 0 && trk_pid[k] != 0u;
    id = trk_pid[k] - 1u;
    ok &= !pat_read(id, r) && r[1] == PROJ_NP && r[BK_MOT + 1u] == ((P_E5 - PROJ_ESHIFT) | MOTION_LOCK) &&
          !pat_decode(&trk[k], id, st, v, &ms) && ms.count == 1u && (ms.event[0].id == P_E5 && ms.event[0].lock);
    ok &= r[0] == BK_VER;
    bad += check("  (phase 9) a lock on E5: the record of 103 parameters (E5 at 100), back at today's P_E5", ok);
    ok = motion_put(&trk[k], 4, P_DTONE, -9, MOTION_LOCK) == 0 && keep_all() == 0 && trk_pid[k] == id + 1u;
    ok &= !pat_read(id, r) && r[1] == P_COUNT && !pat_decode(&trk[k], id, st, v, &ms) && ms.count == 2u;
    for (i = n = 0; i < ms.count; i++)
        n += (ms.event[i].id == P_E5 && ms.event[i].lock) || (ms.event[i].id == P_DTONE && ms.event[i].lock);
    bad += check("  .. and one on DRIVE's TONE (phase 9's): the record of today's ids, both back", ok && n == 2u);
    ok = !pat_read(id, r) && P_E5 > 127u && r[0] == BK_VER2 && (r[BK_MOT] & 64u) &&
         r[BK_MOT + 1u] == ((P_E5 & 127u) | MOTION_LOCK) && !(r[BK_MOT + 4u] & 64u);
    bad += check("  (1.3) E5 is past 127 there: version 2, its id's bit 7 in its step byte's bit 6", ok);
    return bad;
}

static int torn(void)
{
    int bad = 0, ok = 1, n;
    snap_t a, b, c;
    fresh();
    rich(0, 1);
    snap(&a, 0);
    put_as(0, 0);                                 /* the copy before: pattern 01 (ids 0..3's chunk) */
    rich(0, 9);
    snap(&c, 0);
    for (n = 0; n <= (int)(BK_LEN / 256u) + 1; n++) {   /* the power goes after n writes: the payload's pages, the header */
        rich(0, 9);
        fail_after = n;
        ok &= put_as(0, 0) != 0;
        fail_after = -1;
        power_cycle();
        detach(0);
        ok &= go_to(0, 0) == 0;
        snap(&b, 0);
        ok &= same(&a, &b);
    }
    bad += check("a save torn after any write (each page, before the header): 01 as it was", ok);
    fail_after = -1;
    rich(0, 9);
    ok = put_as(0, 0) == 0;
    power_cycle();
    detach(0);
    ok &= go_to(0, 0) == 0;
    snap(&b, 0);
    bad += check("  then a whole one: the new 01", ok && same(&c, &b));
    nor[bk_sector(bk.sect[0] - 1u) + 256u + 1000u] ^= 0x10u;   /* the newest copy damaged */
    power_cycle();
    detach(0);
    ok = go_to(0, 0) == 0;
    snap(&b, 0);
    bad += check("  its newest copy damaged: the copy before (the first 01)", ok && same(&a, &b));
    rich(0, 5);                                   /* (changed: else there is nothing to write) */
    erase_fail = 1;
    ok = put_as(0, 0) != 0;
    erase_fail = 0;
    bad += check("  an erase that fails: not saved, said so", ok && pool_used(0));
    return bad;
}

static int wear_and_wrap(void)
{
    int bad = 0, ok = 1;
    uint32_t i, most = 0;
    snap_t a, b;
    static uint8_t r[BK_REC];
    fresh();
    for (i = 0; i < BK_CHUNKS; i++) {              /* a full bank: every chunk has a copy, 4 sectors spare */
        rich(0, i);
        ok &= bk_pack(r, &trk[0]) && pool_put(i * BK_PER, r) == 0;
    }
    detach(0);
    memset(erases, 0, sizeof erases);
    for (i = 0; i < 40u; i++) {
        rich(3, i);
        ok &= put_as(3, 1) == 0;
    }
    snap(&a, 3);
    for (i = 0; i < BK_NSECT; i++)
        most = erases[bk_sector(i) >> 12] > most ? erases[bk_sector(i) >> 12] : most;
    power_cycle();
    detach(3);
    ok &= go_to(3, 1) == 0;
    snap(&b, 3);
    bad += check("a full bank, 40 saves of one pattern: round the spare sectors (each erased about 10 times), the last "
                 "kept", ok && same(&a, &b) && most <= 11u && pool_used(0) && pool_used(44) && trk_pat(3) == 1u);
    fresh();
    bk.top = 0xFFFFFFF0u;                         /* the sequence about to wrap */
    for (i = 0; i < 30u; i++) {
        rich(0, i);
        ok &= put_as(0, 0) == 0;
    }
    snap(&a, 0);
    power_cycle();
    detach(0);
    ok &= go_to(0, 0) == 0 && bk.top < 0x100u;
    snap(&b, 0);
    bad += check("  the sequence wraps: the newest copy still wins", ok && same(&a, &b));
    return bad;
}

static int playing(void)
{
    int bad = 0;
    uint32_t e;
    fresh();
    rich(0, 4);
    put_as(0, 1);
    e = erases_total();
    song.playing = 1;
    bad += check("playing: nothing written (an erase mutes)", put_as(0, 3) == 2 && pat_flush_one() == 0 &&
                 e == erases_total() && pool_used(1) && !pool_used(3));
    song.playing = 0;
    flash_ok = 0;
    bank_boot();
    bad += check("no flash: an empty bank, a put says so", !pool_used(1) && put_as(0, 1) == 3);
    flash_ok = 1;
    return bad;
}

static uint32_t pool_count(void)
{
    uint32_t id, n = 0;
    for (id = 0; id < BK_POOL; id++)
        n += (uint32_t)pool_used(id);
    return n;
}
static int migrate(void)
{
#if !FELUCCA_LEGACY
    return 0;                                     /* ((2.0) the converter's: run_tests.sh's FELUCCA_LEGACY build runs it) */
#else
    int bad = 0, ok, n;
    uint32_t k, cuts = 0;
    static uint8_t before[sizeof nor];
    static project_t pj, got;
    static project_store_t w, ref;
    static uint8_t r[BK_REC];
    snap_t a[NTRK], b;
    fresh();
    rich(0, 5);                                   /* T1 every field; T2 the same pattern (on its own track); T3 empty; */
    rich(1, 5);                                   /* T4 8 steps */
    simple(3, 50, 8, DIR_FWD);
    for (k = 0; k < NTRK; k++)
        snap(&a[k], k);
    project_capture(&pj);
    ok = proj_pack(&w, &pj) && !st_save(OBJ_PROJECT0, &w, sizeof w) && !st_save(OBJ_PROJECT0 + 2u, &w, sizeof w) &&
         !st_save(OBJ_AUTOSAVE, &w, sizeof w);    /* (SKF1: slots A and C, the autosave) */
    memcpy(before, nor, sizeof nor);
    ok &= proj_migrate(0) == 0 && !pool_count();
    bad += check("migrate: not after a crash, a hang or a failed boot (nothing written)", ok);
    ok = proj_migrate(1) == 1 && pool_count() == 2u;
    for (k = 0; k < 5u; k++) {
        uint32_t obj = k < 4u ? OBJ_PROJECT0 + k : OBJ_AUTOSAVE;
        n = st_load(obj, &w, sizeof w);
        if (k == 1u || k == 3u) {
            ok &= n <= 0;                         /* (empty slots: none) */
            continue;
        }
        ok &= n == (int)PROJ_SKF->size && proj_import(&got, &w, n) && got.ent[0][0][0] && got.ent[1][0][0] == got.ent[0][0][0] &&
              !got.ent[2][0][0] && got.ent[3][0][0] && got.ent[3][0][0] != got.ent[0][0][0] && !got.cur[0] && !got.ent[0][1][0];
    }
    ok &= proj_migrate(1) == 0;                   /* (again: all SKF2 (SKF3 with six tracks), nothing to do) */
    bad += check("  slots A C and the autosave: SKF2 (SKF3), each track's entry A its pattern (T1 T2 one, shared), T3 none",
                 ok);
    favorite_set(ENGI_DRUMX, 0, 1);               /* ((Skiff phase 7) row 14 is DRUM-X's favourites) */
    ok = proj_migrate(1) == 0 && favorite_has(ENGI_DRUMX, 0);
    favorite_set(ENGI_DRUMX, 0, 0);
    bad += check("  a DRUM-X favourite (row 14) stays", ok);
    for (k = 0; k < NTRK; k++)
        detach(k);
    project_load(0);
    ok = 1;
    for (k = 0; k < NTRK; k++) {
        snap(&b, k);
        ok &= same(&a[k], &b);
    }
    bad += check("  the same music: every track's steps, timing, automation and locks as before", ok);
    st_load(OBJ_PROJECT0, &ref, sizeof ref);
    ok = 1;
    for (n = 0; n < 400; n++) {                   /* the power goes after n writes (each page, each commit) */
        memcpy(nor, before, sizeof nor);
        power_cycle();
        fail_after = n;
        proj_migrate(1);
        k = fail_after > 0;                       /* (it ended before the cut: done) */
        cuts += !k;
        fail_after = -1;
        power_cycle();
        proj_migrate(1);                          /* (the next boot) */
        ok &= st_load(OBJ_PROJECT0, &w, sizeof w) == (int)PROJ_SKF->size && !memcmp(&w, &ref, PROJ_SKF->size) &&
              pool_count() == 2u && st_load(OBJ_AUTOSAVE, &w, sizeof w) == (int)PROJ_SKF->size;
        if (k)
            break;
    }
    bad += check("  a power cut after any write: the next boot ends it the same (the same patterns, none twice)",
                 ok && cuts > 10u && n < 400);
    memcpy(nor, before, sizeof nor);
    power_cycle();
    for (k = 0; k < NTRK; k++)
        detach(k);
    ok = bk_pack(r, &trk[2]);                     /* (an empty track's pattern: none of the project's) */
    for (k = 0; k < BK_POOL; k++)
        ok &= pool_put(k, r) == 0;
    ok &= proj_migrate(1) == 0 && st_load(OBJ_PROJECT0, &w, sizeof w) == (int)sizeof w;
    memset(proj_slot, 0, sizeof proj_slot);       /* (the RAM copies lost: from flash) */
    proj_fetch(0);
    project_load(0);
    for (k = 0; k < NTRK; k++) {
        snap(&b, k);
        ok &= same(&a[k], &b);
    }
    bad += check("  the pool full: the projects as they were (SKF1), loading with their own steps", ok);
    return bad;
#endif
}

/* ---- (Skiff phase 7) the DRUM-X kit is the project's: saved with it (SKF3), back when loaded, the factory's for a project
 * without one; an edit of it is something an autosave keeps ---- */
static int dx_kit_project(void)
{
    int bad = 0, ok;
    uint32_t sig;
    fresh();
    memcpy(dx_kit, DX_KIT_DEF, sizeof dx_kit);
    sig = autosave_sig();
    dx_kit[DV_KICK].a[DXP_DECAY] = 5;
    ok = autosave_sig() != sig && project_save_as(1, "KIT") == 0;
    dx_kit[DV_KICK].a[DXP_DECAY] = 99;
    project_load(1);
    if (NTRK > PROJ_NT) {
        ok &= dx_kit[DV_KICK].a[DXP_DECAY] == 5;
        bad += check("(phase 7) the DRUM-X kit: saved with the project (SKF3), back when it loads; an edit an autosave keeps", ok);
    } else {
        ok &= !memcmp(dx_kit, DX_KIT_DEF, sizeof dx_kit);
        bad += check("(phase 7) the DRUM-X kit with four tracks (SKF2 holds none): the factory's when a project loads", ok);
    }
    memcpy(dx_kit, DX_KIT_DEF, sizeof dx_kit);
    return bad;
}

int main(void)
{
    int bad = layout() + fidelity() + keep() + entries() + last_track() + torn() + wear_and_wrap() + playing() +
              migrate() + dx_kit_project();
    printf("%s\n", bad ? "BANK TEST FAILED" : "pattern bank tests passed");
    return bad != 0;
}
