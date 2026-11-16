/* SPDX-License-Identifier: GPL-3.0-only */
/* Skiff: the pattern pool (DESIGN.md SAVE > PATTERNS). Sixty-four patterns, kept in flash exactly as they were played:
 * every step whole (its notes and chord, velocity, accent, slide, chance, RATCH or FLAM, trig condition, NUDGE, lane
 * hits and their accents), LEN DIV SWING GATE and DIR, and the track's automation and locks. (Phase 5) A project names
 * the pattern each track is on (SKF2: its entry A): the patterns are every project's. What a track has is kept as the
 * pattern it is on, by itself: waiting in RAM (PEND_N), written at the autosave's quiet stop.
 *
 * A pattern is BK_REC bytes:
 *   0          the version (0: empty)
 *   1          the parameter count its motion's ids are of (project.c proj_motion_ids): (Skiff phase 9) PROJ_NP,
 *              which firmware before reads, unless the motion names a parameter added since: then P_COUNT
 *   2..6       LEN DIV SWING GATE DIR, each + 64 (the sound's own values, not an automated one)
 *   7          its motion records (0..64), bit 7: the automation plays (motion.on)
 *   8..583     the 64 steps, 9 bytes each (project.c step_put9)
 *   584..631   their NUDGE (core.h nudge_pack)
 *   632..887   the motion, 64 x (step (bit 6: the id's bit 7), id (bit 7: a lock), value low, value high)
 * (Skiff 1.3) A record whose motion names an id from 128 on (P_COUNT passed 127: only one written with r[1] = P_COUNT,
 * a parameter added since phase 9 named) is version 2 (BK_VER2), its ids' bit 7 in bit 6 of their step byte; every
 * other one version 1 as before, which firmware before reads (it shows a version 2 as empty).
 * (Skiff 2.0, #25) A record with a step's per-note ties (core.h step_ties: byte 7 of a step without hits) is version 3
 * (BK_VER3, BK_VER2's ids too): 1.4 shows it as empty; one without any stays version 1 or 2, byte for byte as before
 * Phase 5: the records are one pool, ids 0..BK_POOL-1 (pool_put, pat_decode); id = track x 16 + slot, so a chunk is
 * still one track's A..D, E..H, I..L or M..P and every bank saved before reads the same by id.
 * Flash: BK_CHUNKS chunks of BK_PER patterns (ids 4c .. 4c+3), each written whole to one
 * of BK_NSECT sectors (fm1_flash.h FL_BANK, FL_USR: 0xE7000..0xE8FFF, 0xEA000..0xFBFFF) that holds no chunk's current
 * copy: the payload from offset 256, then the commit record at 0 (its own magic, the chunk, a sequence, the payload's
 * length and hash, the record's hash) last, so a write torn anywhere leaves the chunk's copy before in charge. At boot
 * each chunk's newest copy whose payload checks wins. Four sectors spare: saves go round the free ones (wear). No RAM
 * image: a pattern is read when it is loaded (SPI, short IRQ-off windows: playing goes on); a write reads its chunk,
 * changes its patterns and writes it, only while stopped (an erase mutes the audio).
 * Without the flash driver (FELUCCA_FLASH 0) BK_RAM keeps the sectors in RAM (the host's UI tests); else no bank. */
#define BK_VER 1u
#define BK_VER2 2u                                 /* (Skiff 1.3) an id from 128 on (see the top) */
#define BK_VER3 3u                                 /* (Skiff 2.0, #25) per-note ties (see the top) */
static int bk_ver(uint32_t v) { return v == BK_VER || v == BK_VER2 || v == BK_VER3; }
#define BK_PER 4u
#define BK_POOL NPAT                               /* the patterns, ids 0 .. 63 (every track's: phase 5) */
#define BK_CHUNKS (BK_POOL / BK_PER)
#define BK_STEPS 8u
#define BK_NUDGE (BK_STEPS + NSTEP * 9u)
#define BK_MOT (BK_NUDGE + NSTEP / 4u * 3u)
#define BK_REC (BK_MOT + MOTION_MAX * 4u)
#define BK_LEN (BK_PER * BK_REC)                   /* a chunk's payload */
#define BK_NSECT 20u
#define BK_NONE 0xFFu
#define BK_MAGIC 0x31424B53u                       /* "SKB1" */
_Static_assert(BK_REC == 888u && BK_LEN <= 4096u - 256u && BK_CHUNKS + 4u == BK_NSECT, "pattern bank layout");
typedef struct { uint32_t magic, chunk, seq, len, sum, rsv[2], hsum; } bk_hdr_t;   /* at a sector's 0 */
static const uint8_t BK_TIME[5] = {P_SLEN, P_SDIV, P_SSWING, P_SGATE, P_DIR};

static struct {
    uint8_t sect[BK_CHUNKS];                       /* each chunk's current sector + 1 (0: never saved, all empty) */
    uint32_t top;                                  /* the newest sequence (a save: + 1) */
    uint8_t next;                                  /* the sector a save tries first (round: wear) */
    uint8_t len[BK_POOL];                          /* each pattern's LEN, 0 = empty (the page) */
    uint32_t gen;                                  /* any change (the page redraws) */
} bk;
static uint8_t bk_buf[BK_LEN] __attribute__((section(".pool")));   /* a chunk read, changed and written */
static uint8_t bk_rec[BK_REC] __attribute__((section(".pool")));   /* a track's pattern packed (pool_put) */
/* (phase 5) keeping edits: a pattern changed while playing waits here for the next quiet stop (pat_flush_one), and every
 * read of it sees this copy first. PEND_N records, each its pool id (PAT_NONE: the slot free); a record all 0: the
 * pattern is to be emptied */
#define PEND_N NTRK                                /* (one a track) */
static uint8_t pend_id[PEND_N] __attribute__((section(".pool")));   /* (.pool: not among the globals the audio ISR reaches) */
static uint8_t pend_rec[PEND_N][BK_REC] __attribute__((section(".pool")));
/* each track's pattern as it came in or was last kept (a hash of it packed): a copy unchanged since is not kept, so
 * another track on the same pattern that changed it is not undone */
static uint32_t trk_sig[NTRK] __attribute__((section(".pool")));
_Static_assert(BK_POOL == NPAT, "the pool is the bank's records");
static uint8_t bk_bufc;                            /* the chunk + 1 bk_buf holds whole (0: none, or changed; a backup's
                                                    * LIST reads a chunk in, its GETs read on: bank_chunk) */

#if BOARD_SMK37
/* (SMK-37 Pro) twenty sectors in a row in USRFLASH, after the user sample slot (fm1_flash_map.h FL_BANK); the first
 * erase there releases the stock mapping's marker (storage.c st_stock_release) */
_Static_assert(FL_BANK_HI - FL_BANK_LO == BK_NSECT * 0x1000u && FL_BANK_LO >= FL_SMP_LO && FL_BANK_HI <= FL_SMP_HI,
               "SMK: the pattern pool's sectors");
static uint32_t bk_sector(uint32_t i) { return FL_BANK_LO + i % BK_NSECT * 0x1000u; }
#else
static uint32_t bk_sector(uint32_t i) { return i < 2u ? 0xE7000u + i * 0x1000u : 0xEA000u + (i - 2u) * 0x1000u; }
#endif
static uint32_t bk_chunk(uint32_t id) { return id % BK_POOL / BK_PER; }

#if FELUCCA_FLASH
#define BK_ON flash_ok
static int bk_read(uint32_t off, void *d, uint32_t n) { return st_read(off, d, n); }
static int bk_erase(uint32_t off) { return st_erase(off); }
static int bk_prog(uint32_t off, const void *s, uint32_t n) { return st_prog(off, s, n); }
#elif defined(BK_RAM)
#define BK_ON 1
static uint8_t bk_ram[BK_NSECT][0x1000];
static uint8_t *bk_at(uint32_t off)
{
#if BOARD_SMK37
    uint32_t i = (off - FL_BANK_LO) >> 12;
#else
    uint32_t i = off < 0xEA000u ? (off - 0xE7000u) >> 12 : ((off - 0xEA000u) >> 12) + 2u;
#endif
    return &bk_ram[i % BK_NSECT][off & 0xFFFu];
}
static int bk_read(uint32_t off, void *d, uint32_t n) { memcpy(d, bk_at(off), n); return 0; }
static int bk_erase(uint32_t off) { memset(bk_at(off), 0xFF, 0x1000u); return 0; }
static int bk_prog(uint32_t off, const void *s, uint32_t n)
{
    uint8_t *p = bk_at(off);
    for (uint32_t i = 0; i < n; i++) p[i] &= ((const uint8_t *)s)[i];
    return 0;
}
#else
#define BK_ON 0
static int bk_read(uint32_t off, void *d, uint32_t n) { (void)off; (void)d; (void)n; return -1; }
static int bk_erase(uint32_t off) { (void)off; return -1; }
static int bk_prog(uint32_t off, const void *s, uint32_t n) { (void)off; (void)s; (void)n; return -1; }
#endif

static int bk_head(uint32_t i, bk_hdr_t *h)        /* sector i's commit record valid: 0 */
{
    if (bk_read(bk_sector(i), h, sizeof *h))
        return -1;
    return h->magic == BK_MAGIC && h->chunk < BK_CHUNKS && h->len == BK_LEN &&
           h->hsum == proj_hash(h, sizeof *h - 4u) ? 0 : -1;
}
static int bk_body(uint32_t i, const bk_hdr_t *h)  /* its payload -> bk_buf, the hash right: 0 */
{
    return bk_read(bk_sector(i) + 256u, bk_buf, BK_LEN) || proj_hash(bk_buf, BK_LEN) != h->sum ? -1 : 0;
}
static int bk_newer(uint32_t a, uint32_t b) { return a != b && a - b < 0x80000000u; }   /* (wraps) */
static void bk_lens(uint32_t c)                    /* chunk c's patterns (in bk_buf): their LENs for the page */
{
    uint32_t r;
    for (r = 0; r < BK_PER; r++) {
        const uint8_t *p = bk_buf + r * BK_REC;
        int32_t len = (int32_t)p[2] - 64;
        bk.len[c % BK_CHUNKS * BK_PER + r] = (uint8_t)(!bk_ver(p[0]) ? 0 : len < 1 ? 1 : len > (int32_t)NSTEP ? NSTEP : len);
    }
}

/* persist_boot (main.c): each chunk's newest copy whose payload checks (a torn or damaged one: the copy before) */
static void bank_boot(void)
{
    bk_hdr_t h[BK_NSECT];
    uint8_t ok[BK_NSECT];
    uint32_t i, c;
    memset(&bk, 0, sizeof bk);
    memset(pend_id, PAT_NONE, sizeof pend_id);     /* (phase 5: nothing waiting to be kept) */
    bk_bufc = 0;
    if (!BK_ON)
        return;
    for (i = 0; i < BK_NSECT; i++) {
        ok[i] = bk_head(i, &h[i]) == 0;
        if (ok[i] && (bk.top == 0u || bk_newer(h[i].seq, bk.top)))
            bk.top = h[i].seq;
    }
    for (c = 0; c < BK_CHUNKS; c++)
        for (;;) {
            uint32_t best = BK_NONE;
            for (i = 0; i < BK_NSECT; i++)
                if (ok[i] && h[i].chunk == c && (best == BK_NONE || bk_newer(h[i].seq, h[best].seq)))
                    best = i;
            if (best == BK_NONE)
                break;
            if (bk_body(best, &h[best]) == 0) {
                bk.sect[c] = (uint8_t)(best + 1u);
                bk_lens(c);
                break;
            }
            ok[best] = 0;
        }
}

static uint32_t bk_free(void)                      /* a sector holding no chunk's current copy, from bk.next on */
{
    uint32_t i, j, used;
    for (i = 0; i < BK_NSECT; i++) {
        uint32_t s = (bk.next + i) % BK_NSECT;
        for (used = 0, j = 0; j < BK_CHUNKS; j++)
            used |= bk.sect[j] == s + 1u;
        if (!used)
            return s;
    }
    return BK_NONE;
}
/* bk_buf -> chunk c's next copy, the commit record last, then read back: 0 written */
static int bk_write(uint32_t c)
{
    uint32_t to = bk_free(), base, off;
    bk_hdr_t h, chk;
    int rc;
    if (to == BK_NONE)
        return -1;
    base = bk_sector(to);
    if ((rc = bk_erase(base)) != 0)
        return rc;
    for (off = 0; off < BK_LEN; off += 256u)
        if ((rc = bk_prog(base + 256u + off, bk_buf + off, BK_LEN - off > 256u ? 256u : BK_LEN - off)) != 0)
            return rc;
    h.magic = BK_MAGIC;
    h.chunk = c;
    h.seq = bk.top + 1u;
    h.len = BK_LEN;
    h.sum = proj_hash(bk_buf, BK_LEN);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hsum = proj_hash(&h, sizeof h - 4u);
    if ((rc = bk_prog(base, &h, sizeof h)) != 0)
        return rc;
    if (bk_head(to, &chk) || memcmp(&chk, &h, sizeof h) || bk_body(to, &chk))
        return -7;                                 /* (a part that does not keep what it is given: not SAVED) */
    bk.sect[c] = (uint8_t)(to + 1u);
    bk.top = h.seq;
    bk.next = (uint8_t)((to + 1u) % BK_NSECT);
    bk.gen++;
    return 0;
}

/* a pattern -> record r: steps st, LEN DIV SWING GATE DIR tv (the sound's own), the events of ms placed on track k, the
 * automation on or not: 0 for a step no format can hold (none the UI makes) */
static int bk_pack_raw(uint8_t *r, uint32_t k, const step_t *st, const int16_t *tv, const motion_store_t *ms, int on)
{
    uint32_t i, n = 0, np = PROJ_NP, ties = 0;
    /* ((Skiff phase 9) the motion's ids as a store of PROJ_NP has them, which firmware before reads; today's when it
     * names a parameter added since) */
    for (i = 0; i < ms->count && i < MOTION_MAX; i++)
        if ((ms->event[i].place >> 6) == k && MOTION_ID(&ms->event[i]) >= PROJ_NP - 8u && MOTION_ID(&ms->event[i]) < P_E0)
            np = P_COUNT;
    memset(r, 0, BK_REC);
    r[0] = BK_VER;
    r[1] = (uint8_t)np;
    for (i = 0; i < 5u; i++)
        r[2u + i] = (uint8_t)(tv[i] + 64);
    for (i = 0; i < NSTEP; i++) {
        if (!step_put9(r + BK_STEPS + i * 9u, &st[i]))
            return 0;
        ties |= step_ties(&st[i]);
    }
    nudge_pack(r + BK_NUDGE, st, NSTEP);
    for (i = 0; i < ms->count && n < MOTION_MAX; i++) {
        const motion_event_t *e = &ms->event[i];
        uint8_t *m = r + BK_MOT + n * 4u;
        if ((e->place >> 6) != k)
            continue;
        uint32_t id = np == P_COUNT || MOTION_ID(e) < P_E0 ? MOTION_ID(e) : MOTION_ID(e) - PROJ_ESHIFT;
        m[0] = (uint8_t)((e->place & 63u) | (id >> 7) << 6);   /* ((1.3) the id's bit 7: BK_VER2) */
        m[1] = (uint8_t)((id & 127u) | (e->lock ? MOTION_LOCK : 0u));
        if (id > 127u)
            r[0] = BK_VER2;
        m[2] = (uint8_t)e->value;
        m[3] = (uint8_t)((uint16_t)e->value >> 8);
        n++;
    }
    r[7] = (uint8_t)(n | (on ? 128u : 0u));
    if (ties)
        r[0] = BK_VER3;                            /* ((2.0) its ids as BK_VER2's too) */
    return 1;
}
/* track t's pattern as it is (the sound's own timing, its automation and locks) -> record r */
static int bk_pack(uint8_t *r, const track_t *t)
{
    int16_t tv[5];
    uint32_t i;
    for (i = 0; i < 5u; i++)
        tv[i] = motion_base_value(t, BK_TIME[i]);
    return bk_pack_raw(r, trk_index(t), t->step, tv, &motion, motion_enabled(t));
}

/* chunk c as it is now -> bk_buf (never saved: all empty): 0, 2 a flash error */
static int bk_load(uint32_t c)
{
    bk_bufc = 0;
    if (!bk.sect[c]) {
        memset(bk_buf, 0, BK_LEN);
        return 0;
    }
    return bk_read(bk_sector(bk.sect[c] - 1u) + 256u, bk_buf, BK_LEN) ? 2 : 0;
}
/* record rec -> pattern id (rec 0: the pattern emptied): 0 done, 2 not (the transport runs, a flash error), 3 no flash */
static int pool_put(uint32_t id, const uint8_t *rec)
{
    uint32_t c = bk_chunk(id);
    uint8_t *r = bk_buf + id % BK_PER * BK_REC;
    bk_bufc = 0;
    if (!BK_ON)
        return 3;
    if (transport_busy() || bk_load(c))
        return 2;
    if (rec)
        memcpy(r, rec, BK_REC);
    else
        memset(r, 0, BK_REC);
    if (bk_write(c))
        return 2;
    bk_lens(c);
    return 0;
}
static uint32_t pend_at(uint32_t id)                /* id's waiting copy, PEND_N none */
{
    uint32_t i;
    for (i = 0; i < PEND_N; i++)
        if (pend_id[i] == id % BK_POOL)
            return i;
    return PEND_N;
}
static uint32_t pend_free(void)                    /* the slots free */
{
    uint32_t i, n = 0;
    for (i = 0; i < PEND_N; i++)
        n += pend_id[i] == PAT_NONE;
    return n;
}
static int pool_used(uint32_t id)
{
    uint32_t i = pend_at(id);
    return i < PEND_N ? bk_ver(pend_rec[i][0]) : bk.len[id % BK_POOL] != 0u;
}
/* pattern id's record as it is now -> r: its waiting copy, else flash's: 0, 1 none (empty, or one no firmware wrote) */
static int pat_read(uint32_t id, uint8_t *r)
{
    uint32_t i = pend_at(id), c = bk_chunk(id);
    if (i < PEND_N)
        memcpy(r, pend_rec[i], BK_REC);
    else if (!bk.len[id % BK_POOL] || !bk.sect[c] ||
             bk_read(bk_sector(bk.sect[c] - 1u) + 256u + id % BK_PER * BK_REC, r, BK_REC))
        return 1;
    return bk_ver(r[0]) && (r[7] & 127u) <= MOTION_MAX ? 0 : 1;
}
/* record r (all 0: empty it) to be pattern id: the same as it is now, nothing; else it waits (over id's waiting copy,
 * or in a free slot): 0 kept (or the same), 1 no room. Nothing is written here: pat_flush_one does, stopped */
static int pat_keep_rec(uint32_t id, const uint8_t *r)
{
    uint32_t i = pend_at(id);
    int now = pat_read(id, bk_buf);
    bk_bufc = 0;
    if (bk_ver(r[0]) ? !now && !memcmp(bk_buf, r, BK_REC) : now)
        return 0;                                  /* (the same, or empty and empty) */
    if (i == PEND_N)
        for (i = 0; i < PEND_N && pend_id[i] != PAT_NONE; i++)
            ;
    if (i == PEND_N)
        return 1;
    pend_id[i] = (uint8_t)(id % BK_POOL);
    memcpy(pend_rec[i], r, BK_REC);
    bk.gen++;
    return 0;
}
/* the waiting patterns of one chunk into flash, one write (the transport stopped; an erase mutes the audio): 0 done or
 * none waiting, 2 not (the transport runs, a flash error: they stay waiting) */
static int pat_flush_one(void)
{
    uint32_t i, c;
    for (i = 0; i < PEND_N && pend_id[i] == PAT_NONE; i++)
        ;
    if (i == PEND_N)
        return 0;
    c = bk_chunk(pend_id[i]);
    if (BK_ON) {                                   /* (no flash: nowhere to keep them, dropped) */
        if (transport_busy() || bk_load(c))
            return 2;
        for (i = 0; i < PEND_N; i++)
            if (pend_id[i] != PAT_NONE && bk_chunk(pend_id[i]) == c) {
                uint8_t *r = bk_buf + pend_id[i] % BK_PER * BK_REC;
                if (bk_ver(pend_rec[i][0]))
                    memcpy(r, pend_rec[i], BK_REC);
                else
                    memset(r, 0, BK_REC);
            }
        if (bk_write(c))
            return 2;
        bk_lens(c);
    }
    for (i = 0; i < PEND_N; i++)
        if (pend_id[i] != PAT_NONE && bk_chunk(pend_id[i]) == c)
            pend_id[i] = PAT_NONE;
    bk.gen++;
    return 0;
}
/* pat_keep_rec; no slot free and the transport stopped: a chunk written first to make room */
static int pat_keep_now(uint32_t id, const uint8_t *r)
{
    return pat_keep_rec(id, r) && (transport_busy() || pat_flush_one() || pat_keep_rec(id, r));
}
/* the autosave's round may wait for its gap (project.c): 0 nothing waiting, 1 some, 2 no slot free (not to wait) */
static uint32_t keep_waiting(void) { return pend_free() == PEND_N ? 0u : pend_free() ? 1u : 2u; }
/* the first empty id, none waiting either; NPAT: the pool full */
static uint32_t pool_alloc(void)
{
    uint32_t id;
    for (id = 0; id < BK_POOL; id++)
        if (!pool_used(id) && pend_at(id) == PEND_N)
            return id;
    return NPAT;
}
/* (2.0, #34, the user's pick 2A) a copy's number: the first free after its original's (06's copy is 08 when 07 is
 * used), round to the first free before it, ((#44) none of taken's: a chain's copies before it); NPAT: the pool full */
static uint32_t pool_free_after(uint32_t orig, uint64_t taken)
{
    uint32_t i, id;
    for (i = 1; i <= BK_POOL; i++) {
        id = (orig + i) % BK_POOL;
        if (!pool_used(id) && pend_at(id) == PEND_N && !((taken >> id) & 1u))
            return id;
    }
    return NPAT;
}
static uint32_t pool_alloc_after(uint32_t orig) { return pool_free_after(orig, 0); }
/* a pattern whose record is r (the LENs compared first); NPAT none */
static uint32_t pool_find(const uint8_t *r)
{
    uint32_t id, i;
    int32_t len = (int32_t)r[2] - 64;
    for (id = 0; id < BK_POOL; id++) {
        i = pend_at(id);
        if (!pool_used(id) || (i == PEND_N && bk.len[id] != (uint8_t)(len < 1 ? 1 : len > (int32_t)NSTEP ? NSTEP : len)))
            continue;
        if (!pat_read(id, bk_buf) && !memcmp(bk_buf, r, BK_REC)) {
            bk_bufc = 0;
            return id;
        }
    }
    bk_bufc = 0;
    return NPAT;
}

/* pattern id, read and checked, for track t: its steps, LEN DIV SWING GATE DIR (fitted to t's sound), its automation
 * and locks (t's places): 0 ok, 1 nothing there (or a record no firmware wrote) */
static int pat_decode(const track_t *t, uint32_t id, step_t *st, int16_t *v, motion_store_t *ms)
{
    uint8_t *r = bk_buf;
    uint32_t tk = trk_index(t), i, out;
    bk_bufc = 0;
    if (pat_read(id, r))                           /* (a waiting copy first) */
        return 1;
    memset(st, 0, NSTEP * sizeof *st);
    for (i = 0; i < NSTEP; i++)
        if (!step_get9(&st[i], r + BK_STEPS + i * 9u))
            return 1;
    nudge_unpack(st, r + BK_NUDGE, NSTEP);
    for (i = 0; i < 5u; i++)
        v[i] = (int16_t)param_fit(track_desc(t, BK_TIME[i]), (int32_t)r[2u + i] - 64);
    memset(ms, 0, sizeof *ms);
    ms->count = (uint8_t)(r[7] & 127u);
    ms->on = (uint8_t)(r[7] & 128u ? 1u << tk : 0u);
    for (i = out = 0; i < ms->count && i < MOTION_MAX; i++) {
        const uint8_t *m = r + BK_MOT + i * 4u;
        int32_t val = (int16_t)(m[2] | (uint16_t)m[3] << 8);   /* (stored 16 bits: checked before it is a byte) */
        ms->event[i].place = (uint16_t)(tk << 6 | (m[0] & 63u));
        ms->event[i].id = (uint8_t)((m[1] & 127u) | (m[0] & 64u) << 1);   /* ((1.3) bit 7: BK_VER2's; 0 before) */
        ms->event[i].lock = m[1] >> 7;
        ms->event[i].value = (int8_t)val;
        out |= val < -64 || val > 127;
    }
    if (out || !proj_motion_ids(ms, r[1]) || !motion_valid(ms))
        ms->count = 0;                             /* (motion no firmware wrote: the steps still load) */
    return 0;
}

/* ---- (phase 5) a track's pattern (trk_pid): what it has is kept as that pattern; a new one gets the first free id
 * when it is first kept ---- */
static uint32_t pool_len(uint32_t id)               /* pattern id's LEN, 0 empty (a waiting copy's first) */
{
    uint32_t i = pend_at(id);
    int32_t len;
    if (i == PEND_N)
        return bk.len[id % BK_POOL];
    if (!bk_ver(pend_rec[i][0]))
        return 0;
    len = (int32_t)pend_rec[i][2] - 64;
    return (uint32_t)(len < 1 ? 1 : len > (int32_t)NSTEP ? (int32_t)NSTEP : len);
}
static void rec_motion_of(uint8_t *r, uint32_t id)  /* pattern id's stored automation into record r (motion_lost) */
{
    if (!pat_read(id, bk_buf)) {
        r[7] = bk_buf[7];
        memcpy(r + BK_MOT, bk_buf + BK_MOT, BK_REC - BK_MOT);
    }
    bk_bufc = 0;
}
/* track k as a record (bk_rec): its steps, the sound's own timing, its automation (a pattern that came in without its
 * automation keeps its stored one: motion_lost). 0: a step no format holds */
static int trk_rec(uint32_t k)
{
    k %= NTRK;
    if (!bk_pack(bk_rec, &trk[k]))
        return 0;
    if (((motion_lost >> k) & 1u) && trk_pid[k])
        rec_motion_of(bk_rec, trk_pid[k] - 1u);
    return 1;
}
/* new pattern id (kept already) is track k's */
static void trk_name(uint32_t k, uint32_t id) { trk_pid[k] = (uint8_t)(id + 1u); }
/* track k kept: its pattern, changed since it came in (trk_sig), waits to be written (pat_keep_rec); steps on no
 * pattern yet become a new one, the first free id (trk_name). 0 kept (or unchanged, or nothing there), 1 not: PATTERNS
 * FULL (no id, no waiting slot) */
static int trk_keep(uint32_t k)
{
    uint32_t h, id;
    k %= NTRK;
    if (!BK_ON)
        return 0;
    if (!trk_pid[k] && seq_is_empty(&trk[k]) && !motion_count(&trk[k]))
        return 0;                                  /* (nothing there: nothing to keep) */
    if (!trk_rec(k))
        return 1;
    h = proj_hash(bk_rec, BK_REC);
    if (trk_pid[k] && h == trk_sig[k])
        return 0;
    id = trk_pid[k] ? trk_pid[k] - 1u : pool_alloc();
    if (id == NPAT || pat_keep_now(id, bk_rec))
        return 1;
    if (!trk_pid[k])
        trk_name(k, id);
    trk_sig[k] = h;
    return 0;
}
/* stopped (a project save or load, a backup): every track kept, all of it written now: 0, 1 PATTERNS FULL, 2 a flash
 * error (what is not written still waits) */
static int keep_all(void)
{
    uint32_t k;
    int rc = 0;
    for (k = 0; k < NTRK; k++)
        if (trk_keep(k))
            rc = 1;
    while (pend_free() < PEND_N)
        if (pat_flush_one())
            return 2;
    return rc;
}
/* the autosave's round (stopped and quiet: project.c autosave_poll): every track kept, one chunk of the waiting
 * patterns written (one erase a call): 0 nothing waits, 1 one written (more may wait), 2 a write failed */
static int keep_poll(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        (void)trk_keep(k);
    if (pend_free() == PEND_N)
        return 0;
    return pat_flush_one() ? 2 : 1;
}

/* pattern id into track t now (stopped: its steps, LEN DIV SWING GATE DIR, its automation and locks). No undo copy:
 * what the track had is kept. 0 loaded, 1 nothing there (or a record no firmware wrote), 2 loaded, its automation not
 * (AUTOMATION FULL: the pattern keeps it, motion_lost) */
static int pat_load(track_t *t, uint32_t id)
{
    static step_t st[NSTEP];
    static motion_store_t ms;
    uint32_t k = trk_index(t), i, full;
    int16_t v[5];
    if (pat_decode(t, id, st, v, &ms))
        return 1;
    motion_restore(t);
    fm1_irq_off();                                 /* the audio ISR must not see half a pattern */
    memcpy(t->step, st, sizeof st);
    for (i = 0; i < 5u; i++)
        t->p[BK_TIME[i]] = v[i];
    fm1_irq_on();
    full = motion_replace_track(t, &ms) != 0;
    if (full)
        motion_clear(t);                           /* (what it had goes with it, as at a launch: none) */
    motion_lost = (uint8_t)((motion_lost & ~(1u << k)) | (full ? 1u << k : 0u));
    motion_rebase(t);
    trk_pid[k] = (uint8_t)(id + 1u);
    trk_sig[k] = trk_rec(k) ? proj_hash(bk_rec, BK_REC) : 0u;
    bk.gen++;
    return full ? 2 : 0;
}
/* how many use pattern id: the saved projects */
static uint32_t pat_refs(uint32_t id) { return proj_slot_refs(id); }
/* (phase 5) SAVE > PATTERNS, the pool view: how many use pattern id: the saved projects, a track on it (0: it can be
 * cleared) */
static uint32_t pool_refs(uint32_t id)
{
    uint32_t n, k;
    id %= BK_POOL;
    n = pat_refs(id);
    for (k = 0; k < NTRK; k++)
        n += trk_pid[k] == id + 1u;
    return n;
}
/* .. pattern id cleared (its dialog's OCT+: nothing uses it; stopped: an erase); said */
/* the waiting copies written, stopped (a clear's, (2.0) an empty's): 0, else said (rc 4: the keep found no room) */
static int pool_flush_said(int rc)
{
    while (!rc && pend_free() < PEND_N)
        if (pat_flush_one())
            rc = 2;
    bk.gen++;
    ui.force = 1;
    if (rc)
        ui_way(rc == 4 ? "PATTERNS FULL" : "CLEAR FAILED", rc == 4 ? WAY_PATTERNS : "");
    return rc;
}
static void pool_clear(uint32_t id)
{
    char b[16];
    id %= BK_POOL;
    if (!BK_ON) {
        ui_message("STORAGE ERROR");
        return;
    }
    if (transport_busy() || pool_refs(id)) {
        ui_message(transport_busy() ? "STOP TO CLEAR" : "IN USE");
        return;
    }
    memset(bk_rec, 0, BK_REC);
    if (pool_flush_said(pat_keep_now(id, bk_rec) ? 4 : 0))
        return;
    str_cpy(b, "CLEARED ", sizeof b);
    pat_num(b + 8, id);
    ui_message(b);
}
/* (2.0, #46) SAVE > PATTERNS' ACTION: the patterns nothing uses (CLEAR UNUSED's count) */
static uint32_t pool_unused(void)
{
    uint32_t id, n = 0;
    for (id = 0; id < BK_POOL; id++)
        n += pool_used(id) && !pool_refs(id);
    return n;
}
/* (2.0) record r emptied: no steps, NUDGEs or automation, the version every firmware reads; its LEN DIV SWING GATE DIR
 * kept, or track t's (NEW) */
static void rec_empty(uint8_t *r, const track_t *t)
{
    step_t e;
    uint32_t i;
    for (i = 0; t && i < 5u; i++)                  /* ((2.0.2) GATE the default, 1 STEP: the track's can be a 1.x
                                                    * pattern's 50 %) */
        r[2u + i] = (uint8_t)((BK_TIME[i] == P_SGATE ? TP[P_SGATE].def : motion_base_value(t, BK_TIME[i])) + 64);
    memset(&e, 0, sizeof e);
    step_clear(&e);
    (void)step_put9(r + BK_STEPS, &e);
    for (i = 1; i < NSTEP; i++)
        memcpy(r + BK_STEPS + i * 9u, r + BK_STEPS, 9u);
    memset(r + BK_NUDGE, 0, BK_REC - BK_NUDGE);
    r[0] = BK_VER;
    r[1] = PROJ_NP;
    r[7] = 0;
}
/* .. EMPTY (its dialog's OCT+, stopped): pattern id's steps, NUDGEs and automation gone, its number, LEN DIV SWING GATE
 * DIR and every entry naming it kept; a track on it now empties too (its steps are what is kept of it). Said */
static void pool_empty(uint32_t id)
{
    char b[16];
    uint32_t k;
    id %= BK_POOL;
    if (!BK_ON || transport_busy() || pat_read(id, bk_rec)) {
        ui_message(!BK_ON ? "STORAGE ERROR" : transport_busy() ? "STOP TO EMPTY" : "NOTHING TO EMPTY");
        return;
    }
    rec_empty(bk_rec, 0);
    for (k = 0; k < NTRK; k++)
        if (trk_pid[k] == id + 1u) {
            track_defaults_steps(&trk[k]);
            trk_sig[k] = 0;
        }
    if (pool_flush_said(pat_keep_now(id, bk_rec) ? 4 : 0))
        return;
    str_cpy(b, "EMPTIED ", sizeof b);
    pat_num(b + 8, id);
    ui_message(b);
}
/* .. CLEAR UNUSED (its dialog's OCT+, stopped): every pattern nothing uses, cleared. Said: how many */
static void pool_clear_unused(void)
{
    char b[24];
    uint32_t id, n = 0;
    int rc = 0;
    if (!BK_ON || transport_busy()) {
        ui_message(!BK_ON ? "STORAGE ERROR" : "STOP TO CLEAR");
        return;
    }
    memset(bk_rec, 0, BK_REC);
    for (id = 0; id < BK_POOL && !rc; id++)
        if (pool_used(id) && !pool_refs(id)) {
            if (pat_keep_now(id, bk_rec))
                rc = 4;
            else
                n++;
        }
    if (pool_flush_said(rc))
        return;
    str_cpy(b, "CLEARED ", sizeof b);
    fmt_int(b + 8, (int32_t)n);
    str_cpy(b + str_len(b), n == 1u ? " PATTERN" : " PATTERNS", sizeof b - str_len(b));
    ui_message(b);
}
static uint32_t bank_gen(void) { return bk.gen; }

/* ---- the editor's backup (editor_backup.c): each chunk an object, ids 16..31 ---- */
/* chunk c, read into bk_buf: *len BK_LEN, 0 when every pattern in it is empty (or never saved) */
static const uint8_t *bank_chunk(uint32_t c, uint32_t *len)
{
    uint32_t r;
    c %= BK_CHUNKS;
    *len = 0;
    if (!bk.sect[c])
        return bk_buf;
    if (bk_bufc != c + 1u) {
        bk_bufc = 0;
        if (bk_read(bk_sector(bk.sect[c] - 1u) + 256u, bk_buf, BK_LEN))
            return bk_buf;
        bk_bufc = (uint8_t)(c + 1u);
    }
    for (r = 0; r < BK_PER; r++)
        if (bk_ver(bk_buf[r * BK_REC]))
            *len = BK_LEN;
    return bk_buf;
}
/* a chunk from a backup (len 0: all empty): each pattern empty or as some firmware with this record wrote it */
static int bank_chunk_ok(const uint8_t *b, uint32_t len)
{
    uint32_t r, i;
    step_t s;
    if (!len)
        return 1;
    if (len != BK_LEN)
        return 0;
    for (r = 0; r < BK_PER; r++) {
        const uint8_t *p = b + r * BK_REC;
        if (!p[0])
            continue;
        if (!bk_ver(p[0]) || p[1] < 8u || p[1] > P_COUNT || (p[7] & 127u) > MOTION_MAX)
            return 0;
        for (i = 0; i < NSTEP; i++)
            if (!step_get9(&s, p + BK_STEPS + i * 9u))
                return 0;
    }
    return 1;
}
/* a backup's chunk c into the bank (len 0: its patterns emptied): 0 done (or nothing to do), 1 not written */
static int bank_restore(uint32_t c, const uint8_t *b, uint32_t len)
{
    uint32_t k;
    c %= BK_CHUNKS;
    bk_bufc = 0;
    if (!len && !bk.sect[c])
        return 0;                                  /* (empty here too) */
    if (!BK_ON)
        return 1;
    if (len)
        memcpy(bk_buf, b, BK_LEN);
    else
        memset(bk_buf, 0, BK_LEN);
    if (bk_write(c))
        return 1;
    bk_lens(c);
    for (k = 0; k < PEND_N; k++)
        if (pend_id[k] != PAT_NONE && bk_chunk(pend_id[k]) == c)
            pend_id[k] = PAT_NONE;                 /* (the backup's patterns there now: not what waited) */
    for (k = 0; k < NTRK; k++)
        if (trk_pid[k] && bk_chunk(trk_pid[k] - 1u) == c) {
            trk_pid[k] = 0;                        /* (its steps: a new pattern when next kept) */
            motion_lost &= (uint8_t)~(1u << k);
        }
    return 0;
}

#if FELUCCA_LEGACY
/* ---- (phase 5) the projects of before SKF2 (the four slots, the autosave) become SKF2 ((phase 6) SKF3 with six
 * tracks): each track's steps, timing and automation a pool pattern (one the same there already: that one; an
 * empty track: none), its entry A; their old scenes none (the settings' old scene bytes freed). After a clean boot
 * (main.c: never after a crash, a hang or a failed boot) and after a backup's restore (editor_backup.c). The patterns
 * are written before the project naming them: a power cut on the way leaves the record before, and the next time
 * finds those patterns again. The pool full: that project stays as it is (it loads as before, its steps its own). 1:
 * something written. (2.0) The converter's alone (FELUCCA_LEGACY, web/conv): 2.0 reads none of them ---- */
#if FELUCCA_FLASH
static void fm1_wdt_feed(void);                    /* (fm1_sys.h; the host tests' own) */
#endif
static int proj_migrate(int allowed)
{
    int wrote = 0;
#if FELUCCA_FLASH
    project_t *p = &proj_scratch;
    uint32_t o, k, i, id, used;
    int16_t tv[5];
    int len;
    if (!allowed || !BK_ON)
        return 0;
    for (o = 0; o < 5u; o++) {
        uint32_t obj = o < 4u ? OBJ_PROJECT0 + o : OBJ_AUTOSAVE;
        fm1_wdt_feed();
        proj_wire_gen++;
        len = st_load(obj, &proj_wire, sizeof proj_wire);
        if (len <= 0 || proj_fmt_of(&proj_wire, len) || !proj_import(p, &proj_wire, len))
            continue;                              /* (none, SKF2 .. SKF4 already, or no project) */
        proj_bound(p);
        proj_drums_to_part(p);                     /* (what a load converts, here first: a pattern's automation */
        proj_phys(p);                              /* comes as it is stored) */
        proj_fm4(p);
        proj_perc(p);
        for (k = 0; k < NTRK; k++) {
            for (i = used = 0; i < NSTEP; i++)
                used |= p->t[k].step[i].n | p->t[k].step[i].hit;
            for (i = 0; i < p->motion.count; i++)
                used |= (p->motion.event[i].place >> 6) == k;
            if (!used || p->t[k].preset == PROJ_DEF_SOUND)
                continue;                          /* (empty, or its steps cleared when it loads: no pattern) */
            for (i = 0; i < 5u; i++)
                tv[i] = p->t[k].p[BK_TIME[i]];
            if (!bk_pack_raw(bk_rec, k, p->t[k].step, tv, &p->motion, (p->motion.on >> k) & 1u))
                break;
            if ((id = pool_find(bk_rec)) == NPAT && ((id = pool_alloc()) == NPAT || pat_keep_now(id, bk_rec)))
                break;                             /* (the pool full) */
            p->ent[k][0][0] = (uint8_t)(id + 1u);
        }
        while (pend_free() < PEND_N)
            if (pat_flush_one())
                return wrote;                      /* (a flash error: the rest another time) */
        if (k < NTRK || !proj_pack2(&proj_wire, p, PROJ_SKF) || st_save(obj, &proj_wire, PROJ_SKF->size))
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
