/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff P10) The tape store (firmware/src/tape_store.c) against simulated NOR flash, with storage.c, user presets and
 * projects as the firmware has them:
 *   its twenty sectors inside the store, apart from every other object (and the FM-1's BTIF); a chunk 3,840 bytes, the
 *   tape TAPE_N records in three, the sequencers' chunk 90 bytes free;
 *   four slots each with a full tape (1,440 notes over the four tracks, L, mutes, target, OVERDUB) and six sequencers
 *   (every step field, NUDGE) saved and loaded back over a power cycle, the automation and SEQ RUN in SKF5;
 *   a set written only when one of its chunks changed;
 *   a save torn after any write (each page and commit record of the four chunks, then the SKF5 record): the set before
 *   (a cut in the record: the new set, the record before); a damaged newest set: the set before;
 *   saves go round the free sectors (wear); the music's set given up for a project's save, written again after;
 *   the generation wraps; nothing written while the transport runs; no flash: no set;
 *   a deleted slot's sets gone for good; SKF3 / SKF4 slots and the pattern pool's sectors of before: OLD FORMAT, free;
 *   (FELUCCA_LEGACY, the converter's) projects holding their steps (SKF1 ..) become SKF5 and a set, a cut anywhere
 *   ending the same at the next pass.
 * Built and run by tests/run_tests.sh (also -DBOARD_SMK37=1: the SMK-37 Pro's sectors). */
#ifndef BOARD_SMK37
#define BOARD_SMK37 0                                   /* -DBOARD_SMK37=1: the store on the SMK-37 Pro layout */
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
#include "../firmware/src/tape_store.c"

static int check(const char *what, int ok)
{
    printf("tape store: %-72s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void power_cycle(void)                     /* the RAM gone: the slots and the sets read from flash again */
{
    uint32_t i;
    memset(proj_slot, 0, sizeof proj_slot);
    for (i = 0; i < 4u; i++)
        proj_fetch(i);
    tape_init();
    ts_boot();
    for (i = 0; i < NTRK; i++)
        track_defaults_steps(&trk[i]);
    memset(&motion, 0, sizeof motion);
}
static void fresh(void)                           /* a new unit: flash erased, the tracks at power-on, an empty tape */
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
    tape_init();
    ts_boot();
    motion_lost = 0;
    seq_run = (uint8_t)((1u << NTRK) - 1u);
    (void)arm_cancel();
    transport_req = 0;
}
static uint32_t erases_total(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < 256u; i++)
        n += erases[i];
    return n;
}

/* track k: steps using every field a step has (seed: other ones), some automation and locks */
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
            step_set_rflam(s, 5);
        else
            step_set_ratchet(s, 1u + (i + seed) % 4u);
        s->cond = (uint8_t)((i + seed) % TC_COUNT);
        s->nudge = (int8_t)((int32_t)((i * 13u + seed) % 64u) - 32);
    }
    t->p[P_SLEN] = (int16_t)(37 + (int32_t)seed % 20);
    t->p[P_DIR] = DIR_PING;
    for (i = 0; i < 4u; i++) {
        motion_set_event(t, (i * 4u + seed) % NSTEP, P_REV, (int16_t)(i * 7u + seed % 9u));
        motion_set_lock(t, (i * 4u + 1u + seed) % NSTEP, P_E0 + i % 8u, 0);
    }
}
/* the tape filled: n notes over its four tracks (seed: other ones), L bars, the mutes, target, OVERDUB */
static void tape_fill(uint32_t n, uint32_t seed, uint32_t bars)
{
    uint32_t i;
    tape_init();
    tape_set_len(bars);
    for (i = 0; i < n; i++) {
        uint32_t at = (i * 7919u + seed * 104729u) % (bars * TAPE_BAR), pitch = (i * 13u + seed) % 128u;
        uint32_t ins = (i + seed) % NTRK, ticks = 1u + (i * 31u + seed) % (bars * TAPE_BAR);
        uint32_t velo = 1u + (i + seed) % 127u;
        (void)tape_insert((i + seed) % TAPE_TRACKS, at | pitch << 20 | ins << 27, ticks | velo << 20, TAPE_ANY_PASS);
    }
    tp_trk[seed % TAPE_TRACKS].mute = 1;
    tp_target = (uint8_t)((seed + 1u) % TAPE_TRACKS);
    tp_overdub = (uint8_t)(seed & 1u);
}

typedef struct {
    step_t step[NTRK][NSTEP];
    int16_t len[NTRK];
    motion_store_t m;
    uint8_t run;
    uint32_t w[TAPE_N][2];
    uint16_t count[TAPE_TRACKS];
    uint32_t tlen;
    uint8_t mutes, target, overdub;
} snap_t;
static void snap(snap_t *s)                       /* the music now: what a project and its set keep */
{
    uint32_t k, n = 0;
    memset(s, 0, sizeof *s);
    for (k = 0; k < NTRK; k++) {
        memcpy(s->step[k], trk[k].step, sizeof s->step[k]);
        s->len[k] = trk[k].p[P_SLEN];
    }
    s->m = motion;
    s->run = seq_run;
    for (k = 0; k < TAPE_TRACKS; k++) {
        uint16_t node;
        for (node = tp_trk[k].first; node != TAPE_END && n < TAPE_N; node = tp_nx[node]) {
            s->w[n][0] = tp_w0[node];
            s->w[n++][1] = tp_w1[node];
            s->count[k]++;
        }
        s->mutes |= (uint8_t)(tp_trk[k].mute << k);
    }
    s->tlen = tape_length();
    s->target = tp_target;
    s->overdub = tp_overdub;
}
static int same_seq(const snap_t *a, const snap_t *b)   /* the set's part: steps, tape */
{
    return !memcmp(a->step, b->step, sizeof a->step) && !memcmp(a->w, b->w, sizeof a->w) &&
           !memcmp(a->count, b->count, sizeof a->count) && a->tlen == b->tlen && a->mutes == b->mutes &&
           a->target == b->target && a->overdub == b->overdub;
}
static int same(const snap_t *a, const snap_t *b)       /* .. and SKF5's: LEN, the motion, SEQ RUN */
{
    return same_seq(a, b) && !memcmp(a->len, b->len, sizeof a->len) && !memcmp(&a->m, &b->m, sizeof a->m) &&
           a->run == b->run;
}
static void scramble(void)                        /* the music now: something else (a load must bring it all back) */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        rich(k, 50u + k);
    tape_fill(30, 99, 3);
    seq_run = 1;
}

static int layout(void)
{
    int bad = 0, ok = 1;
    uint32_t i, o, c;
    for (i = 0; i < TS_NSECT; i++) {
        uint32_t s = ts_sector(i);
#if BOARD_SMK37
        ok &= s >= FL_BANK_LO && s + 0x1000u <= FL_BANK_HI && FL_GUARD(s, 0x1000u) &&
              (s >= SMP_USER_BASE + SMP_USER_FLASH_SLOTS * SMP_USER_SIZE || s + 0x1000u <= SMP_USER_BASE);
#else
        ok &= (s >= 0xE7000u && s + 0x1000u <= 0xE9000u) || (s >= 0xEA000u && s + 0x1000u <= 0xFC000u);
#endif
        for (o = 0; o < OBJ_COUNT; o++)
            for (c = 0; c < 2u; c++)
                ok &= st_sector(o, c) != s;
        ok &= s != 0xE9000u || BOARD_SMK37;
        for (o = 0; o < i; o++)
            ok &= ts_sector(o) != s;
    }
    bad += check(BOARD_SMK37 ? "20 sectors (SMK-37 Pro): USRFLASH after the user slot, nor any other object's" :
                 "20 sectors: the VM area's last two, the package's USR region; not BTIF, nor any other object's", ok);
    bad += check("a chunk 3840 bytes: the tape's 1440 records in three, six sequencers of 640 (90 bytes free); 5 sets x 4",
                 TS_LEN == 3840u && TAPE_N == 1440u && TS_TAPE * TS_LEN == TAPE_N * 8u &&
                 TS_LEN - NTRK * TS_SEQ_USED == 90u && TS_SLOTS * TS_CHUNKS == TS_NSECT && sizeof(ts_hdr_t) == 64u);
    return bad;
}

static int fidelity(void)
{
    int bad = 0, ok = 1;
    uint32_t s, k, e0;
    static snap_t a[4], b;
    fresh();
    for (s = 0; s < 4u; s++) {                    /* four slots, each a full tape and six sequencers */
        for (k = 0; k < NTRK; k++)
            rich(k, s * 10u + k);
        tape_fill(TAPE_N + 40u, s, 4u + s * 60u);   /* (more than fits: the rest refused) */
        seq_run = (uint8_t)(0x15u << (s & 1u));
        ok &= tp_trk[0].count + tp_trk[1].count + tp_trk[2].count + tp_trk[3].count == TAPE_N;
        snap(&a[s]);
        ok &= project_save(s) == 0 && ts_has(s);
    }
    ok &= !ts_has(TS_MUSIC);
    power_cycle();
    for (s = 0; s < 4u; s++) {
        scramble();
        project_load(s);
        snap(&b);
        ok &= same(&a[s], &b) && proj_cur == s;
    }
    bad += check("four slots, a full tape each (1440 notes, L, mutes, target, OVERDUB), six sequencers (every step "
                 "field, NUDGE), the automation, SEQ RUN: back over a power cycle", ok);
    e0 = erases_total();
    ok = project_save(3) == 0 && erases_total() == e0 + 1u;   /* (the record: one erase; the set none) */
    project_load(3);
    snap(&b);
    bad += check("  saved again unchanged: no set written (the record only)", ok && same(&a[3], &b));
    for (k = 0; k < NTRK; k++)
        rich(k, 70u + k);
    tape_fill(5, 7, 2);
    e0 = erases_total();
    ok = project_save(3) == 0 && erases_total() >= e0 + 4u;
    snap(&a[3]);
    power_cycle();
    scramble();
    project_load(3);
    snap(&b);
    bad += check("  changed: a new set (its four chunks), loaded back", ok && same(&a[3], &b));
    scramble();
    project_load(2);
    snap(&b);
    bad += check("  the other slots as they were", same(&a[2], &b));
    return bad;
}

static int torn(void)
{
    int bad = 0, ok = 1, n, cut_set = 0, cut_rec = 0;
    uint32_t k;
    static snap_t a, b, c;
    fresh();
    for (k = 0; k < NTRK; k++)
        rich(k, k);
    tape_fill(700, 1, 8);
    trk[0].p[P_LEVEL] = 30;
    ok = project_save(0) == 0;
    snap(&a);
    for (n = 0; n < 200; n++) {                   /* the power goes after n writes: each page, each commit, the record */
        uint32_t left;
        power_cycle();
        project_load(0);
        for (k = 0; k < NTRK; k++)
            rich(k, 20u + k);
        tape_fill(900, 2, 6);
        trk[0].p[P_LEVEL] = 90;
        snap(&c);
        fail_after = n;
        (void)project_save(0);
        left = (uint32_t)fail_after;
        fail_after = -1;
        power_cycle();
        scramble();
        project_load(0);
        snap(&b);
        if (left)                                 /* (it ended before the cut: done) */
            break;
        if (n < (int)(TS_CHUNKS * (TS_LEN / TAPE_PAGE + 1u))) {
            ok &= same(&a, &b) && trk[0].p[P_LEVEL] == 30;
            cut_set++;
        } else {
            ok &= same_seq(&c, &b) && trk[0].p[P_LEVEL] == 30;
            cut_rec++;
        }
    }
    bad += check("a save torn after any write of its set (64: each chunk's pages, its commit record): the set before",
                 ok && cut_set == 64);
    bad += check("  torn in the SKF5 record: the new set (complete), the record before (its sounds)", ok && cut_rec > 0);
    snap(&b);
    ok = same(&c, &b) && trk[0].p[P_LEVEL] == 90;
    bad += check("  then a whole one: the new set and record", ok);
    nor[ts_sector(tst.sect[0][1] - 1u) + 256u + 1000u] ^= 0x10u;   /* the newest set damaged (chunk 1) */
    power_cycle();
    scramble();
    project_load(0);
    snap(&b);
    bad += check("  its newest set damaged: the set before (complete)", same_seq(&a, &b));
    for (k = 0; k < NTRK; k++)
        rich(k, 5u + k);                          /* (changed: else there is nothing to write) */
    erase_fail = 1;
    ok = project_save(0) != 0 && str_eq(ui.msg, "SAVE ERROR");
    erase_fail = 0;
    bad += check("  an erase that fails: not saved, said so", ok);
    return bad;
}

static int wear_and_wrap(void)
{
    int bad = 0, ok = 1;
    uint32_t i, most = 0, others = 0;
    static snap_t a, b;
    uint8_t held[TS_NSECT];
    fresh();
    for (i = 0; i < 4u; i++) {                    /* four slots saved: 16 sectors held, 4 free */
        rich(0, i);
        tape_fill(10u + i, i, 4);
        ok &= project_save(i) == 0;
    }
    for (i = 0; i < TS_NSECT; i++)
        held[i] = ts_held(i) && tst.sect[3][0] != i + 1u && tst.sect[3][1] != i + 1u && tst.sect[3][2] != i + 1u &&
                  tst.sect[3][3] != i + 1u;
    memset(erases, 0, sizeof erases);
    for (i = 0; i < 40u; i++) {
        rich(3, i + 7u);
        ok &= project_save(3) == 0;
    }
    snap(&a);
    for (i = 0; i < TS_NSECT; i++) {
        uint32_t e = erases[ts_sector(i) >> 12];
        most = e > most ? e : most;
        others += held[i] ? e : 0u;
    }
    power_cycle();
    scramble();
    project_load(3);
    snap(&b);
    bad += check("40 saves of one slot, three others held: round its four and the four free (each erased 20 times), "
                 "the others' never", ok && same(&a, &b) && most <= 21u && !others);
    ok = ts_save(TS_MUSIC) == 0 && ts_has(TS_MUSIC) && !ts_free_n() && !ts_music_lost();
    rich(0, 99);
    ok &= project_save(0) == 0 && ts_music_lost() && !ts_has(TS_MUSIC) && ts_has(0);
    ok &= ts_save(TS_MUSIC) == 0 && ts_has(TS_MUSIC) && !ts_music_lost();
    bad += check("  the store full (the music's set too): a project's save takes the music's, written again after", ok);
    fresh();
    tst.top = 0xFFFFFFF0u;                        /* the generation about to wrap */
    for (i = 0; i < 30u; i++) {
        rich(0, i);
        ok &= project_save(0) == 0;
    }
    snap(&a);
    power_cycle();
    scramble();
    project_load(0);
    snap(&b);
    bad += check("  the generation wraps: the newest set still wins", ok && same(&a, &b) && tst.top < 0x100u);
    return bad;
}

static int playing_and_delete(void)
{
    int bad = 0, ok;
    uint32_t e, i, n = 0;
    ts_hdr_t h;
    fresh();
    rich(0, 4);
    project_save(1);
    e = erases_total();
    song.playing = 1;
    ok = project_save(1) == 1 && e == erases_total() && str_eq(ui.msg, "STOP TO SAVE");
    song.playing = 0;
    bad += check("playing: nothing written (an erase mutes)", ok);
    rich(0, 5);
    ok = project_save(1) == 0;                    /* (two sets of slot 2 in flash: the newest, the one before) */
    ok &= project_erase(1) == 0 && !ts_has(1);
    for (i = 0; i < TS_NSECT; i++)
        n += !ts_head(i, &h) && h.slot == 1u;
    power_cycle();
    ok &= !ts_has(1) && !project_used(1) && !n;
    bad += check("a slot deleted: every set of it gone from flash (none comes back at boot)", ok);
    flash_ok = 0;
    ts_boot();
    ok = !ts_has(1) && ts_save(1) == 2;
    flash_ok = 1;
    bad += check("no flash: no set, a save says so", ok);
    return bad;
}

/* the formats of before: SKF3 / SKF4 slots (the pattern pool's) OLD FORMAT, the pool's "SKB1" sectors free */
static int old_format(void)
{
    int bad = 0, ok;
    uint32_t i;
    char nm[PROJ_NAME_LEN + 1u];
    static project_t p;
    static project_store_t w;
    static snap_t a, b;
    fresh();
    for (i = 0; i < TS_NSECT; i++) {              /* the pool of before: every sector a chunk's ("SKB1") */
        memset(nor + ts_sector(i), 0x5Au, 0x1000u);
        memcpy(nor + ts_sector(i), "SKB1", 4);
    }
    ts_boot();
    ok = !ts_has(0) && !ts_has(TS_MUSIC) && ts_free_n() == TS_NSECT;
    rich(0, 3);
    tape_fill(100, 3, 4);
    snap(&a);
    ok &= project_save(0) == 0;
    power_cycle();
    project_load(0);
    snap(&b);
    bad += check("the pattern pool's sectors of before: not read, free; a save goes over them", ok && same(&a, &b));
    project_capture(&p);
    ok = proj_pack2(&w, &p, &PROJ_SKF4) && !st_save(OBJ_PROJECT0 + 2u, &w, PROJ_STORE4) &&
         proj_pack2(&w, &p, &PROJ_SKF3) && !st_save(OBJ_PROJECT0 + 3u, &w, PROJ_STORE3);
    power_cycle();
#if FELUCCA_LEGACY
    ok &= project_used(2) && project_used(3);    /* (the converter reads them: their sounds) */
    bad += check("(the converter's build) SKF3, SKF4 read (their sounds, no steps)", ok);
#else
    scramble();
    snap(&a);
    ok &= project_used(2) && project_name(2, nm) && str_eq(nm, "OLD FORMAT") && project_name(3, nm) &&
          str_eq(nm, "OLD FORMAT");
    project_load(2);
    snap(&b);
    ok &= str_eq(ui.msg, "OLD FORMAT") && same(&a, &b);
    bad += check("SKF4, SKF3 slots: OLD FORMAT (kept as they are; a load changes nothing)", ok);
#endif
    return bad;
}

static int migrate(void)
{
#if !FELUCCA_LEGACY
    return 0;                                     /* ((2.0) the converter's: run_tests.sh's FELUCCA_LEGACY build runs it) */
#else
    int bad = 0, ok, n;
    uint32_t k, cuts = 0;
    static uint8_t before[sizeof nor];
    static project_t pj;
    static project_store_t w, ref, old4;
    static snap_t a, b;
    fresh();
    for (k = 0; k < PROJ_NT; k++)                 /* (the formats of before: four tracks) */
        rich(k, k + 3u);
    project_capture(&pj);
    snap(&a);
    ok = proj_pack(&w, &pj) && !st_save(OBJ_PROJECT0, &w, sizeof w) && !st_save(OBJ_PROJECT0 + 2u, &w, sizeof w) &&
         !st_save(OBJ_AUTOSAVE, &w, sizeof w);    /* (SKF1: slots A and C, the autosave) */
    ok &= proj_pack2(&old4, &pj, &PROJ_SKF4) && !st_save(OBJ_PROJECT0 + 1u, &old4, PROJ_STORE4);   /* (B: SKF4) */
    memcpy(before, nor, sizeof nor);
    power_cycle();
    ok &= proj_migrate(0) == 0 && !ts_has(0);
    bad += check("migrate: not after a crash, a hang or a failed boot (nothing written)", ok);
    ok = proj_migrate(1) == 1 && ts_has(0) && ts_has(2) && ts_has(TS_MUSIC) && !ts_has(1) && !ts_has(3);
    for (k = 0; k < 5u; k++) {
        uint32_t obj = k < 4u ? OBJ_PROJECT0 + k : OBJ_AUTOSAVE;
        n = st_load(obj, &w, sizeof w);
        if (k == 3u)
            ok &= n <= 0;
        else if (k == 1u)
            ok &= n == (int)PROJ_STORE4 && !memcmp(&w, &old4, PROJ_STORE4);
        else
            ok &= n == (int)PROJ_SKF->size && proj_fmt_of(&w, n) == &PROJ_SKF5;
    }
    ok &= proj_migrate(1) == 0;
    bad += check("  slots A C and the autosave: SKF5 and their sets (the steps); the SKF4 slot as it was", ok);
    scramble();
    project_load(0);
    snap(&b);
    bad += check("  the same music: every track's steps, LEN, automation and locks as before",
                 !memcmp(a.step, b.step, sizeof a.step) && !memcmp(a.len, b.len, sizeof a.len) &&
                 !memcmp(&a.m, &b.m, sizeof a.m) && !b.count[0]);
    st_load(OBJ_PROJECT0, &ref, sizeof ref);
    ok = 1;
    for (n = 0; n < 400; n++) {                   /* the power goes after n writes (each page, each commit) */
        memcpy(nor, before, sizeof nor);
        power_cycle();
        fail_after = n;
        proj_migrate(1);
        k = fail_after > 0;
        cuts += !k;
        fail_after = -1;
        power_cycle();
        proj_migrate(1);                          /* (the next boot) */
        ok &= st_load(OBJ_PROJECT0, &w, sizeof w) == (int)PROJ_SKF->size && !memcmp(&w, &ref, PROJ_SKF->size) &&
              ts_has(0) && ts_has(2) && ts_has(TS_MUSIC);
        if (k)
            break;
    }
    bad += check("  a power cut after any write: the next boot ends it the same", ok && cuts > 10u && n < 400);
    return bad;
#endif
}

/* ---- (Skiff phase 7) the DRUM-X kit is the project's: saved with it, back when loaded ---- */
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
    ok &= dx_kit[DV_KICK].a[DXP_DECAY] == 5;
    bad += check("(phase 7) the DRUM-X kit: saved with the project, back when it loads; an edit an autosave keeps", ok);
    memcpy(dx_kit, DX_KIT_DEF, sizeof dx_kit);
    sig = autosave_sig();
    tape_fill(3, 1, 4);
    bad += check("  (P10) the tape: an edit an autosave keeps", autosave_sig() != sig);
    return bad;
}

int main(void)
{
    int bad = layout() + fidelity() + torn() + wear_and_wrap() + playing_and_delete() + old_format() + migrate() +
              dx_kit_project();
    printf("%s\n", bad ? "TAPE STORE TEST FAILED" : "tape store tests passed");
    return bad != 0;
}
