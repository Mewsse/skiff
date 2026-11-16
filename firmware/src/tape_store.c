/* SPDX-License-Identifier: GPL-3.0-only */
/* Skiff: the tape store. Each project slot's note tape (tape.c) and its six sequencers, and the music now's (TS_MUSIC:
 * what the autosave and a backup keep of it), as a set of TS_CHUNKS chunks of TS_LEN bytes, in the twenty sectors the
 * pattern pool had (fm1_flash_map.h FL_BANK; the FM-1's 0xE7000.., 0xEA000..):
 *   chunks 0..2  the tape: 8-byte records (tape.c tape_enc_page), track 0..3 in list order, no ghosts, no held notes
 *                (saves only while stopped), up to TAPE_N; the rest 0. Which track a record is on: the counts
 *   chunk 3      the sequencers, NTRK x TS_SEQ_REC: the type (0: the step sequencer), the 64 steps x 9 (project.c
 *                step_put9), their NUDGE (core.h nudge_pack), 15 spare bytes (0)
 * A chunk is written whole to a sector no set holds: the payload from offset 256 in 256-byte pages (ts_page: no
 * chunk-sized buffer), then the commit record at 0 (its magic, slot, chunk, the set's generation, the payload's length
 * and FNV-1a hash, and chunk 0's the tape's meta: L, the counts, the mutes, the target, OVERDUB; its own hash) last.
 * The four chunks of a set share one generation, and the slot moves to a new set only once all four are in: a write
 * torn anywhere leaves the set before in charge. At boot each slot takes its newest generation whose four chunks all
 * check, else the older complete set, else none. A set is written only when one of its chunks changed. Five slots of
 * four are the twenty sectors: a save finding fewer than four free gives up the music's set (the next autosave round
 * writes it again: tst.lost), and the music's own write, none free, goes over its own set (a torn one: none, as a torn
 * autosave). Free sectors are taken in turn (wear). The project record (project.c SKF5) is written after its set.
 * Records of before ("SKB1", the pattern pool) are not read: their sectors are free. Without the flash driver
 * (FELUCCA_FLASH 0) TS_RAM keeps the sectors in RAM (the host's UI tests); else none */
/* TS_SLOTS (the four project slots, the music now), TS_MUSIC, TS_NONE: project.c's */
#define TS_CHUNKS 4u
#define TS_TAPE 3u                                 /* the tape's chunks */
#define TS_LEN 3840u
#define TS_NSECT 20u
#define TS_MAGIC 0x31544B53u                       /* "SKT1" */
#define TS_VER 1u
#define TS_SEQ_REC 640u                            /* an instrument's sequencer in chunk 3 */
#define TS_SEQ_NUDGE (1u + NSTEP * 9u)
#define TS_SEQ_USED (TS_SEQ_NUDGE + NSTEP / 4u * 3u)
#define TS_OBJ0 40u                                /* the backup's objects (editor_backup.c): 40 + slot x 4 + chunk */
#define TS_OBJS (TS_SLOTS * TS_CHUNKS)
_Static_assert(TS_TAPE * TS_LEN == TAPE_N * 8u && TS_LEN % TAPE_PAGE == 0u, "the tape store: TAPE_N records, 3 chunks");
_Static_assert(NTRK * TS_SEQ_REC == TS_LEN && TS_LEN - NTRK * TS_SEQ_USED == 90u, "the sequencers' chunk: 90 B free");
_Static_assert(TS_SLOTS * TS_CHUNKS == TS_NSECT && TS_LEN <= 4096u - 256u, "the tape store's sectors");
typedef struct { uint16_t bars, counts[TAPE_TRACKS]; uint8_t mutes, target, overdub, ver, rsv[2]; } ts_meta_t;
typedef struct {
    uint32_t magic;
    uint8_t slot, chunk, ver, rsv0;
    uint32_t gen, len, sum;
    ts_meta_t meta;                                /* (chunk 0's; 0 in the others) */
    uint8_t rsv[24];
    uint32_t hsum;
} ts_hdr_t;
_Static_assert(sizeof(ts_meta_t) == 16u && sizeof(ts_hdr_t) == 64u, "the tape store's commit record");

static struct {
    uint8_t sect[TS_SLOTS][TS_CHUNKS];             /* each slot's set: its sectors + 1 (0: none) */
    uint32_t sum[TS_SLOTS][TS_CHUNKS];
    uint32_t gen[TS_SLOTS];
    ts_meta_t meta[TS_SLOTS];
    uint32_t top;                                  /* the newest generation */
    uint8_t next;                                  /* the sector a write tries first (wear) */
    uint8_t lost;                                  /* the music's set given up for another's save */
    uint8_t rslot, rmask, rzero;                   /* a backup's set coming in: its slot, the chunks in, those of 0 B */
    uint8_t rsect[TS_CHUNKS];
    uint32_t rgen, rsum[TS_CHUNKS];
    ts_meta_t rmeta;
} tst;
static uint8_t ts_page[TAPE_PAGE] __attribute__((section(".pool")));
/* what a set is written from: the music now (the tape, the sequencers' steps; 0: empty), or a backup's chunk */
typedef struct {
    const step_t *steps[NTRK];
    const uint8_t *raw;
    int tape;
    tape_it_t it;
} ts_src_t;

#if BOARD_SMK37
/* (SMK-37 Pro) twenty sectors in a row in USRFLASH, after the user sample slot (fm1_flash_map.h FL_BANK); the first
 * erase there releases the stock mapping's marker (storage.c st_stock_release) */
_Static_assert(FL_BANK_HI - FL_BANK_LO == TS_NSECT * 0x1000u && FL_BANK_LO >= FL_SMP_LO && FL_BANK_HI <= FL_SMP_HI,
               "SMK: the tape store's sectors");
static uint32_t ts_sector(uint32_t i) { return FL_BANK_LO + i % TS_NSECT * 0x1000u; }
#else
static uint32_t ts_sector(uint32_t i)
{
    i %= TS_NSECT;
    return i < 2u ? 0xE7000u + i * 0x1000u : 0xEA000u + (i - 2u) * 0x1000u;
}
#endif

#if FELUCCA_FLASH
#define TS_ON flash_ok
static int ts_read(uint32_t off, void *dst, uint32_t n) { return st_read(off, dst, n); }
static int ts_erase(uint32_t off) { return st_erase(off); }
static int ts_prog(uint32_t off, const void *src, uint32_t n) { return st_prog(off, src, n); }
#elif defined(TS_RAM)
#define TS_ON 1
static uint8_t ts_ram[TS_NSECT][0x1000];
static uint8_t *ts_at(uint32_t off)
{
#if BOARD_SMK37
    uint32_t i = (off - FL_BANK_LO) >> 12;
#else
    uint32_t i = off < 0xEA000u ? (off - 0xE7000u) >> 12 : ((off - 0xEA000u) >> 12) + 2u;
#endif
    return &ts_ram[i % TS_NSECT][off & 0xFFFu];
}
static int ts_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, ts_at(off), n); return 0; }
static int ts_erase(uint32_t off) { memset(ts_at(off), 0xFF, 0x1000u); return 0; }
static int ts_prog(uint32_t off, const void *src, uint32_t n)
{
    uint8_t *p = ts_at(off);
    for (uint32_t i = 0; i < n; i++)
        p[i] &= ((const uint8_t *)src)[i];
    return 0;
}
#else
#define TS_ON 0
static int ts_read(uint32_t off, void *dst, uint32_t n) { (void)off; (void)dst; (void)n; return -1; }
static int ts_erase(uint32_t off) { (void)off; return -1; }
static int ts_prog(uint32_t off, const void *src, uint32_t n) { (void)off; (void)src; (void)n; return -1; }
#endif

static int ts_on(void) { return TS_ON; }
static int ts_newer(uint32_t a, uint32_t b) { return a != b && a - b < 0x80000000u; }   /* (wraps) */
static void ts_meta_empty(ts_meta_t *m)            /* no tape: 4 bars, OVERDUB on */
{
    memset(m, 0, sizeof *m);
    m->bars = 4;
    m->overdub = 1;
    m->ver = TS_VER;
}
static int ts_meta_ok(const ts_meta_t *m)
{
    uint32_t k, n = 0;
    for (k = 0; k < TAPE_TRACKS; k++)
        n += m->counts[k];
    return m->bars >= 1u && m->bars <= TAPE_BARS_MAX && n <= TAPE_N && m->mutes < 16u && m->target < TAPE_TRACKS &&
           m->overdub <= 1u && m->ver == TS_VER;
}
static void ts_meta_now(ts_meta_t *m)              /* the tape now */
{
    uint32_t k;
    ts_meta_empty(m);
    m->bars = (uint16_t)(tape_length() / TAPE_BAR);
    tape_save_counts(m->counts);
    for (k = 0; k < TAPE_TRACKS; k++)
        m->mutes |= (uint8_t)((tp_trk[k].mute ? 1u : 0u) << k);
    m->target = (uint8_t)(tp_target % TAPE_TRACKS);
    m->overdub = (uint8_t)(tp_overdub != 0);
}

/* sector i's commit record valid: 0 */
static int ts_head(uint32_t i, ts_hdr_t *h)
{
    if (ts_read(ts_sector(i), h, sizeof *h))
        return -1;
    return h->magic == TS_MAGIC && h->slot < TS_SLOTS && h->chunk < TS_CHUNKS && h->ver == TS_VER && h->len == TS_LEN &&
           h->hsum == proj_hash(h, sizeof *h - 4u) ? 0 : -1;
}
static uint32_t ts_body(uint32_t i)                /* sector i's payload, hashed (a read error: not a hash it has) */
{
    uint32_t off, h = 0x811C9DC5u;
    for (off = 0; off < TS_LEN; off += TAPE_PAGE) {
        if (ts_read(ts_sector(i) + 256u + off, ts_page, TAPE_PAGE))
            return ~h;
        h = as_mix(h, ts_page, TAPE_PAGE);
    }
    return h;
}
static void ts_drop(uint32_t slot)                 /* slot's set forgotten (its sectors free) */
{
    memset(tst.sect[slot], 0, sizeof tst.sect[slot]);
    memset(tst.sum[slot], 0, sizeof tst.sum[slot]);
    tst.gen[slot] = 0;
    ts_meta_empty(&tst.meta[slot]);
}

/* persist_boot's (main.c): each slot's newest complete set (a torn or damaged one: the set before) */
static void ts_boot(void)
{
    uint32_t gen[TS_NSECT], i, slot, c;
    uint8_t sl[TS_NSECT], ch[TS_NSECT], ok[TS_NSECT];
    ts_hdr_t h;
    memset(&tst, 0, sizeof tst);
    for (slot = 0; slot < TS_SLOTS; slot++)
        ts_drop(slot);
    tst.rslot = TS_NONE;
    if (!TS_ON)
        return;
    for (i = 0; i < TS_NSECT; i++) {
        memset(&h, 0, sizeof h);
        ok[i] = ts_head(i, &h) == 0;
        sl[i] = h.slot;
        ch[i] = h.chunk;
        gen[i] = h.gen;
        if (ok[i] && (tst.top == 0u || ts_newer(h.gen, tst.top)))
            tst.top = h.gen;
    }
    for (slot = 0; slot < TS_SLOTS; slot++)
        for (;;) {
            uint32_t best = TS_NONE, all = 1;
            uint8_t at[TS_CHUNKS];
            for (i = 0; i < TS_NSECT; i++)
                if (ok[i] && sl[i] == slot && (best == TS_NONE || ts_newer(gen[i], gen[best])))
                    best = i;
            if (best == TS_NONE)
                break;
            for (c = 0; c < TS_CHUNKS; c++) {
                at[c] = TS_NONE;
                for (i = 0; i < TS_NSECT; i++)
                    if (ok[i] && sl[i] == slot && ch[i] == c && gen[i] == gen[best])
                        at[c] = (uint8_t)i;
                if (at[c] == TS_NONE || ts_head(at[c], &h) || ts_body(at[c]) != h.sum ||
                    (!c && !ts_meta_ok(&h.meta))) {
                    all = 0;
                    break;
                }
                tst.sum[slot][c] = h.sum;
                if (!c)
                    tst.meta[slot] = h.meta;
            }
            if (all) {
                for (c = 0; c < TS_CHUNKS; c++)
                    tst.sect[slot][c] = (uint8_t)(at[c] + 1u);
                tst.gen[slot] = gen[best];
                break;
            }
            for (i = 0; i < TS_NSECT; i++)     /* (that generation: not one to take) */
                if (sl[i] == slot && gen[i] == gen[best])
                    ok[i] = 0;
            ts_drop(slot);
        }
}

static int ts_has(uint32_t slot) { return slot < TS_SLOTS && tst.sect[slot][0] != 0u; }
static int ts_music_lost(void) { return tst.lost; }
static int ts_held(uint32_t i)                     /* sector i holds a set's chunk, or one of a set coming in */
{
    uint32_t slot, c;
    for (slot = 0; slot < TS_SLOTS; slot++)
        for (c = 0; c < TS_CHUNKS; c++)
            if (tst.sect[slot][c] == i + 1u)
                return 1;
    for (c = 0; tst.rslot != TS_NONE && c < TS_CHUNKS; c++)
        if (((tst.rmask >> c) & 1u) && tst.rsect[c] == i + 1u)
            return 1;
    return 0;
}
static uint32_t ts_free_n(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < TS_NSECT; i++)
        n += !ts_held(i);
    return n;
}
static uint32_t ts_take(void)                      /* the next free sector from tst.next on (wear); TS_NONE none */
{
    uint32_t j;
    for (j = 0; j < TS_NSECT; j++) {
        uint32_t i = (tst.next + j) % TS_NSECT;
        if (!ts_held(i)) {
            tst.next = (uint8_t)((i + 1u) % TS_NSECT);
            return i;
        }
    }
    return TS_NONE;
}
/* n sectors free, the music's set given up for them when needed: 1 */
static int ts_room(uint32_t n)
{
    if (ts_free_n() < n && ts_has(TS_MUSIC)) {
        ts_drop(TS_MUSIC);
        tst.lost = 1;
    }
    return ts_free_n() >= n;
}

static void ts_put(uint8_t *page, uint32_t off, uint32_t at, const uint8_t *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (at + i >= off && at + i < off + TAPE_PAGE)
            page[at + i - off] = src[i];
}
/* chunk c's TAPE_PAGE bytes from off into page (the tape's pages in order: chunk 0's first): 0 a step no format holds */
static int ts_fill(ts_src_t *src, uint32_t c, uint32_t off, uint8_t *page)
{
    uint32_t k, i;
    memset(page, 0, TAPE_PAGE);
    if (src->raw) {
        memcpy(page, src->raw + off, TAPE_PAGE);
        return 1;
    }
    if (c < TS_TAPE) {
        if (src->tape)
            (void)tape_enc_page(&src->it, page);
        return 1;
    }
    for (k = 0; k < NTRK; k++) {
        uint32_t base = k * TS_SEQ_REC;
        uint8_t b[NSTEP / 4u * 3u];
        step_t empty;
        if (base + TS_SEQ_REC <= off || base >= off + TAPE_PAGE)
            continue;
        memset(&empty, 0, sizeof empty);
        step_clear(&empty);
        for (i = 0; i < NSTEP; i++) {
            if (!step_put9(b, src->steps[k] ? &src->steps[k][i] : &empty))
                return 0;
            ts_put(page, off, base + 1u + i * 9u, b, 9u);
        }
        if (src->steps[k]) {
            nudge_pack(b, src->steps[k], NSTEP);
            ts_put(page, off, base + TS_SEQ_NUDGE, b, sizeof b);
        }
    }
    return 1;
}
/* every chunk's payload hash: 0 a step no format holds */
static int ts_sums(ts_src_t *src, uint32_t *sum)
{
    uint32_t c, off;
    tape_enc_begin(&src->it);
    for (c = 0; c < TS_CHUNKS; c++) {
        sum[c] = 0x811C9DC5u;
        for (off = 0; off < TS_LEN; off += TAPE_PAGE) {
            if (!ts_fill(src, c, off, ts_page))
                return 0;
            sum[c] = as_mix(sum[c], ts_page, TAPE_PAGE);
        }
    }
    return 1;
}
/* chunk c of slot's set (generation gen) from src into sector i, the commit record last, then read back: 0 written */
static int ts_chunk_write(uint32_t i, uint32_t slot, uint32_t c, uint32_t gen, ts_src_t *src, uint32_t sum,
                          const ts_meta_t *m)
{
    uint32_t base = ts_sector(i), off;
    ts_hdr_t h, chk;
    int rc;
    if ((rc = ts_erase(base)) != 0)
        return rc;
    for (off = 0; off < TS_LEN; off += TAPE_PAGE)
        if (!ts_fill(src, c, off, ts_page) || (rc = ts_prog(base + 256u + off, ts_page, TAPE_PAGE)) != 0)
            return rc ? rc : -2;
    memset(&h, 0, sizeof h);
    h.magic = TS_MAGIC;
    h.slot = (uint8_t)slot;
    h.chunk = (uint8_t)c;
    h.ver = TS_VER;
    h.gen = gen;
    h.len = TS_LEN;
    h.sum = sum;
    if (!c)
        h.meta = *m;
    memset(h.rsv, 0xFF, sizeof h.rsv);
    h.hsum = proj_hash(&h, sizeof h - 4u);
    if ((rc = ts_prog(base, &h, sizeof h)) != 0)
        return rc;
    if (ts_head(i, &chk) || memcmp(&chk, &h, sizeof h) || ts_body(i) != sum)
        return -7;                                 /* (a part that does not keep what it is given: not SAVED) */
    return 0;
}
/* slot's new set from src (its hashes sum, chunk 0's meta m), the generation after the newest; the slot on it once
 * all four are in: 0, else an error (the set before stays) */
static int ts_write(uint32_t slot, ts_src_t *src, const uint32_t *sum, const ts_meta_t *m)
{
    uint8_t to[TS_CHUNKS];
    uint32_t c, gen = tst.top + 1u;
    int rc;
    if (!ts_room(TS_CHUNKS))
        return -1;
    for (c = 0; c < TS_CHUNKS; c++)
        to[c] = (uint8_t)ts_take();
    tape_enc_begin(&src->it);
    for (c = 0; c < TS_CHUNKS; c++)
        if ((rc = ts_chunk_write(to[c], slot, c, gen, src, sum[c], m)) != 0)
            return rc;
    for (c = 0; c < TS_CHUNKS; c++) {
        tst.sect[slot][c] = (uint8_t)(to[c] + 1u);
        tst.sum[slot][c] = sum[c];
    }
    tst.meta[slot] = *m;
    tst.gen[slot] = gen;
    tst.top = gen;
    if (slot == TS_MUSIC)
        tst.lost = 0;
    return 0;
}
/* steps (0: the tracks' own), the tape now or none -> slot's set, when it differs from the one there (stopped): 0
 * written or the same, 1 a step no format holds, 2 a flash error (or no flash) */
static int ts_save_from(uint32_t slot, const step_t *const *steps, int tape)
{
    ts_src_t src;
    ts_meta_t m;
    uint32_t sum[TS_CHUNKS], k;
    if (!TS_ON || slot >= TS_SLOTS)
        return 2;
    memset(&src, 0, sizeof src);
    for (k = 0; k < NTRK; k++)
        src.steps[k] = steps ? steps[k] : trk[k].step;
    src.tape = tape;
    if (tape) {
        tape_sweep();                              /* (the ghosts replace mode left: gone) */
        ts_meta_now(&m);
    } else {
        ts_meta_empty(&m);
    }
    if (!ts_sums(&src, sum))
        return 1;
    if (ts_has(slot) && !memcmp(sum, tst.sum[slot], sizeof sum) && !memcmp(&m, &tst.meta[slot], sizeof m))
        return 0;
    if (!ts_has(slot)) {                           /* (all empty, and no set there: the same, none written) */
        ts_src_t none;
        ts_meta_t m0;
        uint32_t sum0[TS_CHUNKS];
        memset(&none, 0, sizeof none);
        ts_meta_empty(&m0);
        if (ts_sums(&none, sum0) && !memcmp(sum, sum0, sizeof sum) && !memcmp(&m, &m0, sizeof m))
            return 0;
    }
    return ts_write(slot, &src, sum, &m) ? 2 : 0;
}
static int ts_save(uint32_t slot) { return ts_save_from(slot, 0, 1); }   /* the music now -> slot's set */


/* slot's set into the music now (stopped): the tape (its L, mutes, target, OVERDUB) and, with steps, the sequencers'
 * steps; no set (TS_NONE too): both empty. 0, 1 a part that did not read (it is empty) */
static int ts_load(uint32_t slot, int steps)
{
    ts_meta_t m;
    uint32_t c, k, i, off, base3 = 0;
    int has = TS_ON && ts_has(slot), rc = 0;
    if (has)
        m = tst.meta[slot];
    else
        ts_meta_empty(&m);
    tape_set_len(m.bars);
    for (k = 0; k < TAPE_TRACKS; k++)
        tp_trk[k].mute = (uint8_t)((m.mutes >> k) & 1u);
    tp_target = (uint8_t)(m.target % TAPE_TRACKS);
    tp_overdub = m.overdub;
    tape_load_begin(m.counts);
    for (c = 0; has && c < TS_TAPE; c++)
        for (off = 0; off < TS_LEN; off += TAPE_PAGE) {
            if (ts_read(ts_sector(tst.sect[slot][c] - 1u) + 256u + off, ts_page, TAPE_PAGE)) {
                rc = 1;
                break;
            }
            tape_dec_page(ts_page, TAPE_PAGE);
        }
    tape_load_end();
    if (has)
        base3 = ts_sector(tst.sect[slot][TS_TAPE] - 1u) + 256u;
    for (k = 0; steps && k < NTRK; k++) {
        track_t *t = &trk[k];
        uint8_t b[NSTEP / 4u * 3u], type = 1;
        int got = has && !ts_read(base3 + k * TS_SEQ_REC, &type, 1) && type == 0u;   /* (another type: a newer
                                                                                     * firmware's, empty here) */
        for (i = 0; i < NSTEP; i++) {
            step_t s;
            memset(&s, 0, sizeof s);
            if (!got || ts_read(base3 + k * TS_SEQ_REC + 1u + i * 9u, b, 9u) || !step_get9(&s, b)) {
                memset(&s, 0, sizeof s);
                step_clear(&s);
                rc |= got;
            }
            fm1_irq_off();
            t->step[i] = s;
            fm1_irq_on();
        }
        if (got && !ts_read(base3 + k * TS_SEQ_REC + TS_SEQ_NUDGE, b, sizeof b)) {
            fm1_irq_off();
            nudge_unpack(t->step, b, NSTEP);
            fm1_irq_on();
        }
    }
    return rc;
}

/* slot's sets gone from flash (every copy: none comes back at boot): 0, 2 an erase failed */
static int ts_erase_slot(uint32_t slot)
{
    ts_hdr_t h;
    uint32_t i;
    int rc = 0;
    if (tst.rslot == slot)
        tst.rslot = TS_NONE;
    ts_drop(slot);
    if (!TS_ON)
        return 0;
    for (i = 0; i < TS_NSECT; i++)
        if (!ts_head(i, &h) && h.slot == slot && ts_erase(ts_sector(i)))
            rc = 2;
    return rc;
}

/* ---- the editor's backup (editor_backup.c): object n = 0 .. TS_OBJS - 1 (id TS_OBJ0 + n) is slot n / 4's chunk
 * n % 4: its payload, chunk 0's with its meta after it; 0 bytes: the slot has no set ---- */
static uint32_t ts_obj_len(uint32_t n)
{
    n %= TS_OBJS;
    return ts_has(n / TS_CHUNKS) ? TS_LEN + (n % TS_CHUNKS ? 0u : (uint32_t)sizeof(ts_meta_t)) : 0u;
}
/* len bytes of object n from off -> dst: 0, -1 a read error */
static int ts_obj_read(uint32_t n, uint32_t off, uint8_t *dst, uint32_t len)
{
    uint32_t slot = n % TS_OBJS / TS_CHUNKS, c = n % TS_CHUNKS, part;
    if (!ts_has(slot) || off > ts_obj_len(n) || len > ts_obj_len(n) - off)
        return -1;
    if (off < TS_LEN) {
        part = len < TS_LEN - off ? len : TS_LEN - off;
        if (ts_read(ts_sector(tst.sect[slot][c] - 1u) + 256u + off, dst, part))
            return -1;
        dst += part;
        off += part;
        len -= part;
    }
    if (len)
        memcpy(dst, (const uint8_t *)&tst.meta[slot] + (off - TS_LEN), len);
    return 0;
}
/* a backup's object n (len bytes: 0, or as ts_obj_len): every record one a firmware of this store wrote: 1 */
static int ts_obj_ok(uint32_t n, const uint8_t *b, uint32_t len)
{
    uint32_t c = n % TS_CHUNKS, pos, k, i;
    if (!len)
        return 1;
    if (len != TS_LEN + (c ? 0u : (uint32_t)sizeof(ts_meta_t)))
        return 0;
    if (!c) {
        ts_meta_t m;
        memcpy(&m, b + TS_LEN, sizeof m);
        if (!ts_meta_ok(&m))
            return 0;
    }
    if (c < TS_TAPE) {
        for (pos = 0; pos < TS_LEN; pos += 8u) {
            uint32_t w0 = b[pos] | (uint32_t)b[pos + 1u] << 8 | (uint32_t)b[pos + 2u] << 16 |
                          (uint32_t)b[pos + 3u] << 24;
            uint32_t w1 = b[pos + 4u] | (uint32_t)b[pos + 5u] << 8 | (uint32_t)b[pos + 6u] << 16 |
                          (uint32_t)b[pos + 7u] << 24;
            if ((w0 || w1) && ((w0 >> 30) || TP_INST(w0) >= NTRK || !TP_LEN(w1) || !TP_VEL(w1) || (w1 >> 27)))
                return 0;
        }
        return 1;
    }
    for (k = 0; k < NTRK; k++) {
        step_t s;
        if (b[k * TS_SEQ_REC])
            return 0;
        for (i = 0; i < NSTEP; i++)
            if (!step_get9(&s, b + k * TS_SEQ_REC + 1u + i * 9u))
                return 0;
    }
    return 1;
}
/* .. checked, into slot n / 4's set coming in (its chunk n % 4); with the four in, that set is the slot's (all four of
 * 0 bytes: the slot has none). 0 done, 1 a write failed (the slot's set as it was) */
static int ts_obj_put(uint32_t n, const uint8_t *b, uint32_t len)
{
    uint32_t slot = n % TS_OBJS / TS_CHUNKS, c = n % TS_CHUNKS, sum[TS_CHUNKS], i;
    ts_src_t src;
    if (!TS_ON)
        return 1;
    if (tst.rslot != slot || ((tst.rmask >> c) & 1u)) {
        tst.rslot = (uint8_t)slot;                 /* (a set begun: its own generation) */
        tst.rmask = tst.rzero = 0;
        tst.rgen = ++tst.top;
        ts_meta_empty(&tst.rmeta);
    }
    memset(&src, 0, sizeof src);
    if (len) {
        src.raw = b;
        if (!c)
            memcpy(&tst.rmeta, b + TS_LEN, sizeof tst.rmeta);
        if (!ts_room(1) || (i = ts_take()) == TS_NONE ||
            ts_chunk_write(i, slot, c, tst.rgen, &src, proj_hash(b, TS_LEN), &tst.rmeta)) {
            tst.rslot = TS_NONE;
            return 1;
        }
        tst.rsect[c] = (uint8_t)(i + 1u);
        tst.rsum[c] = proj_hash(b, TS_LEN);
    } else {
        tst.rzero |= (uint8_t)(1u << c);
    }
    tst.rmask |= (uint8_t)(1u << c);
    if (tst.rmask != (1u << TS_CHUNKS) - 1u)
        return 0;
    if (tst.rzero == tst.rmask)
        return ts_erase_slot(slot) ? 1 : 0;
    memset(&src, 0, sizeof src);                   /* (a chunk of 0 bytes in a set: an empty one) */
    if (!ts_sums(&src, sum)) {
        tst.rslot = TS_NONE;
        return 1;
    }
    for (c = 0; c < TS_CHUNKS; c++) {
        if (!((tst.rzero >> c) & 1u))
            continue;
        if (!ts_room(1) || (i = ts_take()) == TS_NONE ||
            ts_chunk_write(i, slot, c, tst.rgen, &src, sum[c], &tst.rmeta)) {
            tst.rslot = TS_NONE;
            return 1;
        }
        tst.rsect[c] = (uint8_t)(i + 1u);
        tst.rsum[c] = sum[c];
    }
    memcpy(tst.sect[slot], tst.rsect, sizeof tst.rsect);
    memcpy(tst.sum[slot], tst.rsum, sizeof tst.rsum);
    tst.meta[slot] = tst.rmeta;
    tst.gen[slot] = tst.rgen;
    tst.rslot = TS_NONE;
    if (slot == TS_MUSIC)
        tst.lost = 0;
    return 0;
}

#if FELUCCA_LEGACY
/* ---- (the converter's, FELUCCA_LEGACY: web/conv) the projects of a format holding their steps (FUN1..FUN9, SKF1: the
 * four slots, the autosave) become SKF5 and their set (its steps, no tape); one of SKF2 .. SKF4, whose steps were the
 * pattern pool's, stays as it is (OLD FORMAT on a unit). After a backup's restore (editor_backup.c). The set is
 * written before the record: a power cut on the way leaves the record before. 1: something written ---- */
#if FELUCCA_FLASH
static void fm1_wdt_feed(void);                    /* (fm1_sys.h; the host tests' own) */
#endif
static int proj_migrate(int allowed)
{
    int wrote = 0;
#if FELUCCA_FLASH
    project_t *p = &proj_scratch;
    const step_t *steps[NTRK];
    uint32_t o, k;
    int len;
    if (!allowed || !TS_ON)
        return 0;
    for (o = 0; o < 5u; o++) {
        uint32_t obj = o < 4u ? OBJ_PROJECT0 + o : OBJ_AUTOSAVE;
        fm1_wdt_feed();
        proj_wire_gen++;
        len = st_load(obj, &proj_wire, sizeof proj_wire);
        if (len <= 0 || proj_fmt_of(&proj_wire, len) || !proj_import(p, &proj_wire, len))
            continue;                              /* (none, SKF2 .. SKF5 already, or no project) */
        proj_bound(p);
        proj_drums_to_part(p);
        proj_phys(p);
        proj_fm4(p);
        proj_perc(p);
        for (k = 0; k < NTRK; k++)
            steps[k] = p->t[k].preset == PROJ_DEF_SOUND ? 0 : p->t[k].step;   /* (cleared when it loads: none) */
        if (ts_save_from(o < 4u ? o : TS_MUSIC, steps, 0) || !proj_pack2(&proj_wire, p, PROJ_SKF) ||
            st_save(obj, &proj_wire, PROJ_SKF->size))
            continue;                              /* (this one as it is) */
        if (o < 4u)
            memcpy(&proj_slot[o], &proj_wire, sizeof proj_wire);
        wrote = 1;
    }
#else
    (void)allowed;
#endif
    return wrote;
}
#endif
