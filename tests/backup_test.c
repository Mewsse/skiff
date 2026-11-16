/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The editor's full backup (editor_backup.c: LIST / GET / PUT) against simulated NOR flash:
 * CRC before any write, stale runtime copies, USB resets and timeouts, malformed objects, older
 * project formats, settings values and user preset banks kept byte for byte. */
static unsigned char host_samples[3][0x14000];
#define SMP_USER_XIP(k) host_samples[k]
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }   /* (MENU > SCREEN OFF: lcd.c) */
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#define FELUCCA_FLASH 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
static void panel_setup(void) {}

static uint8_t nor[0x100000], flash_ok = 1;
static int erase_error;
static uint32_t erases;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { if (erase_error) return -8; memset(nor + off, 0xFF, 4096); erases++; return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n) { memcpy(nor + off, src, n); return 0; }
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
#include "../firmware/src/tape_store.c"           /* ((Skiff P10) the tape store, objects 40..59) */

enum { ED_BACKUP_LIST = 65, ED_BACKUP_GET, ED_BACKUP_PUT };
static uint8_t rep[4096];
static uint32_t rep_n;
static void ed_b(uint32_t v) { if (rep_n < sizeof rep) rep[rep_n++] = (uint8_t)(v & 127u); }
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        uint32_t k = na > 7u ? 7u : na;
        if (!k || n + k > max || (m >> k))
            return 0;
        for (j = 0; j < k; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return na ? 0u : n;
}
static uint8_t ed_smp_buf[512] __attribute__((aligned(4)));
static void fm1_wdt_feed(void) {}
static int ed_flash_stop(void) { return transport_busy(); }
#include "../firmware/src/editor_backup.c"

static int check(const char *what, int ok)
{
    printf("backup: %-72s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}

static void reset(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(&song, 0, sizeof song);
    sel_set(0);
    memset(trk, 0, sizeof trk);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(up_bank, 0, sizeof up_bank);
    memset(&persist_saved, 0, sizeof persist_saved);
    memset(&settings, 0, sizeof settings);
    memset(&ui, 0, sizeof ui);
    panel = PANEL_DEFAULT;
    settings_init();
    host_tracks_init();
    fm1_ms = 0;
    transport_req = 0;
    usb.resets = 0;
    erase_error = 0;
    erases = 0;
    ed_bk_valid = ed_bk_put = 0;
    tape_init();
    ts_boot();                                           /* ((Skiff P10) the tape store, from the erased flash: empty) */
    motion_lost = 0;
}

static uint32_t call(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    rep_n = 0;
    ed_backup_handle(cmd, a, n);
    return rep_n;
}
static void put32(uint8_t *a, uint32_t v) { for (uint32_t i = 0; i < 5u; i++) a[i] = (uint8_t)((v >> (7u * i)) & 127u); }

/* PUT begin: rc */
static uint32_t put_begin(uint32_t id, uint32_t len, uint32_t crc)
{
    uint8_t a[12] = {0, (uint8_t)id};
    put32(a + 2, len);
    put32(a + 7, crc);
    call(ED_BACKUP_PUT, a, sizeof a);
    return rep[2];
}
/* PUT one chunk of data at off: rc */
static uint32_t put_chunk(uint32_t id, uint32_t off, const uint8_t *p, uint32_t n)
{
    uint8_t a[16 + 300];
    uint32_t k = 7;
    a[0] = 1;
    a[1] = (uint8_t)id;
    put32(a + 2, off);
    while (n) {
        uint32_t g = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < g; i++) m |= (uint32_t)(p[i] >> 7) << i;
        a[k++] = (uint8_t)m;
        for (i = 0; i < g; i++) a[k++] = p[i] & 127u;
        p += g;
        n -= g;
    }
    call(ED_BACKUP_PUT, a, k);
    return rep[2];
}
static uint32_t put_end(uint32_t id, uint32_t op)   /* op 2 commit, 3 abort */
{
    uint8_t a[2] = {(uint8_t)op, (uint8_t)id};
    call(ED_BACKUP_PUT, a, 2);
    return rep[2];
}
/* a whole object: begin, 256-byte chunks, commit; the first failing rc or the commit's */
static uint32_t put_all(uint32_t id, const void *v, uint32_t len, uint32_t crc)
{
    const uint8_t *p = v;
    uint32_t off, rc = put_begin(id, len, crc);
    for (off = 0; !rc && off < len; off += 256u)
        rc = put_chunk(id, off, p + off, len - off > 256u ? 256u : len - off);
    return rc ? rc : put_end(id, 2);
}
/* LIST: rc; the length and CRC of object id */
static uint32_t list(uint32_t id, uint32_t *len, uint32_t *crc)
{
    uint32_t i;
    call(ED_BACKUP_LIST, 0, 0);
    if (rep[1])
        return rep[1];
    for (i = 0; i < rep[2]; i++) {
        const uint8_t *o = rep + 3 + i * 11u;
        if (o[0] == id) {
            *len = ed_bk_r32(o + 1);
            *crc = ed_bk_r32(o + 6);
        }
    }
    return 0;
}
/* (Skiff) GET a whole object in 256-byte pieces into out: 0 ok */
static uint32_t get(uint32_t id, uint32_t off, uint32_t count);
static uint32_t get_all(uint32_t id, uint8_t *out, uint32_t len)
{
    uint32_t off;
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        if (get(id, off, n) || ed_unpack7(rep + 9, rep_n - 9u, out + off, n) != n)
            return 1;
    }
    return 0;
}
static uint32_t get(uint32_t id, uint32_t off, uint32_t count)
{
    uint8_t a[8] = {(uint8_t)id};
    put32(a + 1, off);
    a[6] = (uint8_t)(count & 127u);
    a[7] = (uint8_t)(count >> 7);
    call(ED_BACKUP_GET, a, sizeof a);
    return rep[1];
}

int main(void)
{
    int bad = 0;
    uint32_t len = 0, crc = 0, before;
    static project_store_t st, was;
#if FELUCCA_LEGACY
    static project_v6_t v6;
#endif
    sks_t ps;                                            /* ((2.0) the settings object: SKS1) */

    reset();
    trk[0].step[0] = (step_t){{60}, 1, ST_NOTE, 0, 96, 0, 0};
    bad += check("LIST captures the runtime: 33 objects (id 8 empty, id 9 the FM6 patches, (Skiff P10) 40..59 the tape "
                 "store), runtime 2560 B (SKF5), the music's set written first",
                 list(0, &len, &crc) == 0 && rep[2] == 33u && rep[3 + 10u * 11u] == 40u && rep[3 + 29u * 11u] == 59u &&
                 rep[3 + 30u * 11u] == 32u && len == PROJ_SKF->size && len == 2560u && crc == st_crc32(ED_BK_RAW, len) &&
                 ts_has(TS_MUSIC) && !ts_has(0));
    bad += check("an empty project slot lists as length 0", list(2, &len, &crc) == 0 && len == 0);
    bad += check("GET of the runtime copy", get(0, 0, 64) == 0);
    project_save(0);
    bad += check("a project save after LIST reused the staging RAM: GET of the runtime is stale (5)",
                 get(0, 0, 64) == 5u);
    bad += check("a new LIST makes it readable again", list(0, &len, &crc) == 0 && get(0, 0, 64) == 0);
    usb.resets++;
    bad += check("a USB reset after LIST: GET is stale (5)", get(2, 0, 16) == 5u);

    /* settings */
    reset();
    memset(&ps, 0, sizeof ps);
    settings_export(&ps);
    ps.lowcut = 2;                                       /* SPEAKER BASS+ */
    ps.palette = palette_to_stored(3);
    bad += check("settings with SPEAKER BASS+ restore", put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 &&
                 settings.lowcut == 2u && fx_lowcut == 2u && settings.palette == 3u);
    ps.palette = 13;                                     /* an older backup: its PAPER (old id 13) */
    bad += check("settings of an older backup restore with the palette migrated",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && settings.palette == 2u);
    ps.palette = palette_to_stored(3);
    before = erases;
    ps.lowcut = 3;
    bad += check("settings with an unknown SPEAKER value are refused, nothing written",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 2u && erases == before && settings.lowcut == 2u);
    ps.lowcut = 0;
    ps.bold = hold_to_stored(0, 3);                      /* HOLD 0.6 s */
    bad += check("settings with HOLD 0.6 s restore",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && HOLD_MS[settings_hold] == 600u);
    ps.bold = 1;                                         /* an older backup (its font weight): HOLD 0.4 s */
    bad += check("settings of an older backup restore with HOLD 0.4 s",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && HOLD_MS[settings_hold] == 400u);
    before = erases;
    ps.bold = HOLD_TAG + 7u;
    bad += check("settings with an unknown HOLD value are refused, nothing written",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 2u && erases == before);
    ps.bold = 0;
    ps.zoom = LEDS_TAG | LEDS_INV;                       /* LEDS INV */
    bad += check("settings with LEDS INV restore",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && settings_leds == LEDS_INV);
    ps.zoom = 1;                                         /* an older backup (its large readout): LEDS DIM */
    bad += check("settings of an older backup restore with LEDS DIM",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && settings_leds == LEDS_DIM);
    before = erases;
    ps.zoom = LEDS_TAG | LEDS_DIM_LO;                    /* LEDS DIM LO, OFF (appended) */
    bad += check("settings with LEDS DIM LO restore",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && settings_leds == LEDS_DIM_LO);
    ps.zoom = LEDS_TAG | LEDS_OFF;
    bad += check("settings with LEDS OFF restore",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && settings_leds == LEDS_OFF);
    before = erases;
    ps.zoom = LEDS_TAG + 4u;
    bad += check("settings with an unknown LEDS value are refused, nothing written",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 2u && erases == before);
    ps.zoom = 0;
    {   /* (2.0, Tier B 6) an archive of before: its settings a PER4 (Skiff's: bank.c's mark), read, then kept as SKS1 */
        static persist_t po;
        static sks_t got;
        memset(&po, 0, sizeof po);
        po.magic = PERSIST_MAGIC; po.palette = palette_to_stored(2); po.lowcut = 1; po.panel = panel;
        po.favorites.factory[15][24] = 0xA5u;
        po.favorites.factory[15][26] = 2u;               /* (LAUNCH's old byte, before ui_set_rev 4: GRID OFF) */
        po.favorites.factory[9][0] = 4;                  /* (PHYS's third preset starred) */
        bad += check("(2.0) settings of a PER4 archive restore, stored as SKS1",
                     put_all(1, &po, sizeof po, st_crc32(&po, sizeof po)) == 0 && settings.palette == 2u &&
                     settings.lowcut == 1u && favorites.factory[15][26] == 0u && favorites.factory[9][0] == 4u &&
                     st_load(OBJ_SETTINGS, &got, sizeof got) == (int)sizeof got && got.magic == SKS_MAGIC &&
                     got.s.grid == 0u && got.fav[9][0] == 4u && !memcmp(&got, &persist_saved, sizeof got));
        before = erases;
        po.magic = SKS_MAGIC;
        bad += check("  a PER4-sized record of another magic is refused, nothing written",
                     put_all(1, &po, sizeof po, st_crc32(&po, sizeof po)) == 2u && erases == before);
    }
    bad += check("a wrong CRC is refused before any write",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps) ^ 1u) == 2u && erases == before);

    /* malformed requests */
    bad += check("an unknown object id is refused", put_begin(8, sizeof ps, 0) == 1u);
    bad += check("a project of the wrong length is refused", put_begin(2, sizeof(project_store_t) - 4u, 0) == 1u);
    bad += check("settings of the wrong length are refused", put_begin(1, sizeof ps + 4u, 0) == 1u);
    bad += check("a chunk without a begin is refused (5)", (ed_bk_put = 0, put_chunk(1, 0, (const uint8_t *)&ps, 16)) == 5u);
    put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
    bad += check("a chunk at the wrong offset is refused", put_chunk(1, 16, (const uint8_t *)&ps, 16) == 1u);
    bad += check("a chunk for another object is refused (5)", put_chunk(2, 0, (const uint8_t *)&ps, 16) == 5u);
    {
        uint32_t off = 0, step = sizeof ps - 8u;
        put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
        for (; off + 256u <= step; off += 256u)
            put_chunk(1, off, (const uint8_t *)&ps + off, 256);
        if (off < step) put_chunk(1, off, (const uint8_t *)&ps + off, step - off);
        bad += check("a chunk past the length is refused", put_chunk(1, step, (const uint8_t *)&ps, 16) == 1u);
    }
    bad += check("commit before all bytes arrived is refused", put_end(1, 2) == 2u && erases == before);
    put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
    put_chunk(1, 0, (const uint8_t *)&ps, 64);
    usb.resets++;
    bad += check("a USB reset during PUT ends it (5)", put_chunk(1, 64, (const uint8_t *)&ps + 64, 64) == 5u);
    put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
    fm1_ms += 16000u;
    bad += check("16 s without a chunk ends the PUT (5)", put_chunk(1, 0, (const uint8_t *)&ps, 64) == 5u);
    put_begin(2, sizeof st, 0);
    put_chunk(2, 0, (const uint8_t *)&ps, 64);
    project_save(1);
    bad += check("a project save during PUT (the staging RAM) ends it (5)",
                 put_chunk(2, 64, (const uint8_t *)&ps, 64) == 5u);
    transport_req = 1;
    bad += check("PUT while starting PLAY is refused (3)", put_begin(1, sizeof ps, 0) == 3u);
    transport_req = 0;

    /* projects */
    reset();
    trk[1].step[3] = (step_t){{64}, 1, ST_NOTE, 0, 96, 0, 0};
#if !FELUCCA_LEGACY
    project_capture(&proj_scratch);
    proj_pack2(&st, &proj_scratch, PROJ_SKF);
    bad += check("(P10) an SKF5 project restores into slot 3 (flash and RAM)",
                 put_all(4, &st, PROJ_STORE4, st_crc32(&st, PROJ_STORE4)) == 0 && project_used(2) &&
                 !memcmp(&proj_slot[2], &st, PROJ_STORE4) &&
                 st_load(OBJ_PROJECT0 + 2, &proj_wire, sizeof proj_wire) == (int)PROJ_STORE4);
    {   /* (D3) a slot's project of before (SKF1; the editor's Restore converts one first, and keeps one it couldn't: the
         * archive's patterns full): kept as it is, OLD FORMAT; a damaged one refused (2), the slot as it was. One in flash
         * (a command-line install's) the same after a power cycle; a backup takes its bytes as flash keeps them */
        static uint8_t old[PROJ_STORE_SIZE], back[PROJ_STORE_SIZE];
        uint32_t sum, m = PROJ_MAGIC, sz = PROJ_STORE_SIZE;
        char nm[PROJ_NAME_LEN + 1u];
        memset(old, 0, sizeof old);
        memcpy(old, &m, 4); memcpy(old + 4, &sz, 4); old[100] = 7;
        sum = proj_hash(old, sizeof old - 4u); memcpy(old + sizeof old - 4u, &sum, 4);
        memcpy(&was, &proj_slot[3], sizeof was);
        old[200] ^= 1;
        bad += check("(2.0) a damaged project of before (its hash) is refused (2), the slot as it was",
                     put_all(5, old, sizeof old, st_crc32(old, sizeof old)) == 2u && !memcmp(&proj_slot[3], &was, sizeof was));
        old[200] ^= 1;
        bad += check("(2.0, D3) a whole one is kept as it is (flash), OLD FORMAT",
                     put_all(5, old, sizeof old, st_crc32(old, sizeof old)) == 0 && proj_old(3) &&
                     st_load(OBJ_PROJECT0 + 3, back, sizeof back) == (int)sizeof old && !memcmp(back, old, sizeof old));
        memset(&proj_slot[3], 0, sizeof proj_slot[3]);    /* (a power cycle: the RAM copy from flash) */
        proj_fetch(3);
        bad += check("(2.0, D3) .. used (SAVE asks before writing over it), named OLD FORMAT, LOAD says so",
                     project_used(3) && project_name(3, nm) && str_eq(nm, "OLD FORMAT") &&
                     (project_load(3), str_eq(ui.msg, "OLD FORMAT")));
        bad += check("(2.0, D3) .. a backup lists it and reads it back byte for byte (the editor's Restore converts it)",
                     list(5, &len, &crc) == 0 && len == sizeof old && crc == st_crc32(old, sizeof old) &&
                     !get_all(5, back, sizeof back) && !memcmp(back, old, sizeof old));
        bad += check("(2.0, D3) .. DELETE clears it", project_erase(3) == 0 && !project_used(3) && !proj_old(3));
    }
#else
    project_capture(&proj_scratch);
    proj_pack(&st, &proj_scratch);
    bad += check("a FUN9 project restores into slot 3 (flash and RAM)",
                 put_all(4, &st, sizeof st, st_crc32(&st, sizeof st)) == 0 && project_used(2) &&
                 !memcmp(&proj_slot[2], &st, sizeof st) && st_load(OBJ_PROJECT0 + 2, &proj_wire, sizeof proj_wire) == (int)sizeof st);
    {   /* FUN8 of 1.0.x (3584 bytes, 91 parameters, the patches at 3056): restores as FUN9, E0 at P_E0, lanes 100 % */
        static uint8_t v8[3584];
        uint32_t pos = 68u, pos9 = 68u, k, j, sum;
        trk[2].p[P_E0] = 2;
        project_capture(&proj_scratch);
        proj_pack(&st, &proj_scratch);
        memcpy(v8, st.raw, 68);
        v8[66] = 91;
        for (k = 0; k < PROJ_NT; k++) {
            for (j = 0; j < 91u; j++) v8[pos++] = st.raw[pos9 + (j < 83u ? j : j + (PROJ_NP - 8u - 83u))];
            pos9 += PROJ_NP;
            memcpy(v8 + pos, st.raw + pos9, 2u + NSTEP * 9u);
            pos += 2u + NSTEP * 9u; pos9 += 2u + NSTEP * 9u + PROJ_NUDGE_BYTES;   /* (Skiff: SKF1's NUDGE plane) */
        }
        memcpy(v8 + pos, st.raw + pos9, sizeof(chain_config_t) + sizeof(motion_store_t));
        memcpy(v8 + 3056u, st.raw + PROJ_FM6_OFF, PROJ_NT * FM6_PACKED);
        ((uint32_t *)v8)[0] = 0x46554E38u;
        ((uint32_t *)v8)[1] = 3584u;
        sum = proj_hash(v8, 3580u);
        memcpy(v8 + 3580u, &sum, 4);
        bad += check("a FUN8 project (1.0.x) restores as FUN9 into slot 2, E0 at P_E0, lane levels 100 %",
                     put_all(3, v8, sizeof v8, st_crc32(v8, sizeof v8)) == 0 && ((uint32_t *)proj_slot[1].raw)[0] == PROJ_MAGIC &&
                     proj_import(&proj_scratch, &proj_slot[1], sizeof st) && proj_scratch.t[2].p[P_E0] == 2 &&
                     proj_scratch.t[2].p[P_LN3] == 127 && proj_scratch.t[1].step[3].note[0] == 64u);
    }
    memset(&v6, 0, sizeof v6);                           /* FUN6: 69 parameters, steps out of range */
    v6.magic = PROJ_MAGIC_V6;
    v6.size = sizeof v6;
    for (uint32_t i = 0; i < G_COUNT; i++)
        v6.g[i] = GP[i].def;
    for (uint32_t i = 0; i < PROJ_NT; i++)
        v6.t[i].engine = trk[i].engine;
    v6.t[0].step[0] = (step10_t){{255, 72}, 9, 7, 0, 96, 0, 0};
    v6.t[0].p[61] = 3;                                   /* old E0 */
    chain_defaults(&v6.chain);
    v6.sum = proj_hash(&v6, sizeof v6 - 4u);
    bad += check("a FUN6 project restores as FUN9, bounded, its E0 at P_E0",
                 put_all(5, &v6, sizeof v6, st_crc32(&v6, sizeof v6)) == 0 && ((uint32_t *)proj_slot[3].raw)[0] == PROJ_MAGIC &&
                 proj_import(&proj_scratch, &proj_slot[3], sizeof st) && proj_scratch.t[0].step[0].n == 4u &&
                 proj_scratch.t[0].step[0].note[0] == 127u && proj_scratch.t[0].p[P_E0] ==
                 clamp(3, param_desc_of(trk[0].engine, P_E0)->min, param_desc_of(trk[0].engine, P_E0)->max));
#endif
    {   /* 1.0.3: the FM6 patch bank (id 8) is retired; an older archive's bank moves into its user presets (ids 6, 7
         * restored first), the user presets' patches are id 9 */
        static up_bank_t ub;
        static fm6_bank_t bk;
        static upf_t got;
        uint8_t pk[FM6_PACKED];
        static const int16_t SL[5] = {FM6_NFACTORY + 2, 1, FM6_NFACTORY + 4, FM6_NFACTORY + 2, FM6_NFACTORY + 2};
        memset(&ub, 0, sizeof ub);
        ub.magic = UP_BANK_MAGIC; ub.rsize = sizeof(up_rec_t); ub.nslot = UP_PER_BANK;
        const uint32_t np = 91u;                        /* ((2.1) 1.0.2's records by count: E0..E7 the last eight) */
        for (uint32_t k = 0; k < 5u; k++) {             /* slots 1..5: B3, F2, B5 (empty), B3 on DRUM, B3 */
            up_rec_t *r = &ub.r[k];
            uint32_t e = k == 3u ? ENGI_DRUM : ENGI_FM6;
            r->used = UP_USED; r->ver = UP_VER; r->engine = (uint8_t)e; r->np = (uint8_t)np;
            r->name[0] = (char)('A' + k);
            for (uint32_t i = 0; i < np; i++)
                up_set_value(r, i, param_desc_of(e, i < np - 8u ? i : P_E0 + i - (np - 8u))->def);
            up_set_value(r, np - 1u, SL[k]);              /* (E7, the raw stored value: B slots as 1.0.2 wrote them) */
        }
        ub.r[4].name[1] = 'X';
        up_set_value(&ub.r[4], np - 8u, 3);              /* (E0: another sound than slot 1's) */
        memset(&bk, 0, sizeof bk);
        bk.magic = FM6_BANK_MAGIC; bk.ver = 1; bk.nslot = FM6_BANK_N; bk.used = 1u << 2;
        memcpy(bk.v[2], FM6_FACTORY[5], FM6_PACKED);
        memcpy(bk.v[2] + 118, "BANK B3   ", 10);
        upf_empty();
        bad += check("the user presets of an old archive (id 6) restore", put_all(6, &ub, sizeof ub, st_crc32(&ub, sizeof ub)) == 0);
#if !FELUCCA_LEGACY
        bad += check("(2.0) an old archive's FM6 bank (id 8) is refused at begin (1): the converter moves its patches",
                     put_begin(8, sizeof bk, st_crc32(&bk, sizeof bk)) == 1u && upf_get(0, pk));
        bad += check("(2.0) an empty id 8 (a 1.0.3 archive) is taken and ignored", put_all(8, 0, 0, st_crc32(0, 0)) == 0);
        upf_set(0, bk.v[2]);                             /* (slot 1's patch, as the converter writes id 9: below) */
        (void)got;
#else
        bad += check("an old archive's FM6 bank (id 8) is taken (0)", put_all(8, &bk, sizeof bk, st_crc32(&bk, sizeof bk)) == 0);
        bad += check("  .. its B3 patch is now the B3 presets' own (slots 1 and 5)",
                     !upf_get(0, pk) && !memcmp(pk + 118, "BANK B3   ", 10) && !upf_get(4, pk) && !memcmp(pk + 118, "BANK B3", 7));
        bad += check("  .. an F slot, an empty B slot and a DRUM sound get none (factory / init / no patch, as before)",
                     upf_get(1, pk) && upf_get(2, pk) && upf_get(3, pk));
        bad += check("  .. written to flash (the user presets' FM6 patches)",
                     st_load(OBJ_UPFM6, &got, sizeof got) == (int)sizeof got && !memcmp(&got, &upf, sizeof got));
        up_load(0);
        bad += check("  .. and the B3 preset loads it, SLOT OWN", TSEL->eng_req == ENGI_FM6 &&
                     !memcmp(fm6_patch[song.sel] + FP_NAME, "BANK B3", 7) && TSEL->p[P_E7] == FM6_OWN);
        up_load(1);
        bad += check("  .. the F2 preset loads the factory patch, SLOT F2", TSEL->p[P_E7] == 1 &&
                     !memcmp(fm6_patch[song.sel] + FP_NAME, FM6_FACTORY[1] + 118, 10));
        bad += check("a restore of id 8 twice moves nothing more (idempotent)",
                     put_all(8, &bk, sizeof bk, st_crc32(&bk, sizeof bk)) == 0 && !memcmp(&got, &upf, sizeof got));
        bad += check("an empty id 8 (a 1.0.3 archive) is taken and ignored", put_all(8, 0, 0, st_crc32(0, 0)) == 0 &&
                     !memcmp(&got, &upf, sizeof got));
        bk.v[3][0] = 200;
        bad += check("an FM6 bank with a byte above 127 is refused (2)", put_all(8, &bk, sizeof bk, st_crc32(&bk, sizeof bk)) == 2u);
#endif
        bad += check("id 8 lists empty, id 9 with the patches' length", list(8, &len, &crc) == 0 && len == 0 &&
                     list(9, &len, &crc) == 0 && len == sizeof(upf_t) && crc == st_crc32(&upf, sizeof upf));
        memcpy(&got, &upf, sizeof got);
        upf_empty();
        bad += check("id 9 restores the user presets' FM6 patches (flash and RAM)",
                     put_all(9, &got, sizeof got, st_crc32(&got, sizeof got)) == 0 && !memcmp(&got, &upf, sizeof got) &&
                     !upf_get(0, pk) && !memcmp(pk + 118, "BANK B3", 7));
        got.magic ^= 1;
        bad += check("an id 9 of another layout is refused (2), the patches kept",
                     put_all(9, &got, sizeof got, st_crc32(&got, sizeof got)) == 2u && !upf_get(0, pk));
        bad += check("an id 9 of the wrong size is refused at begin (1)", put_begin(9, sizeof got - 4u, 0) == 1u);
        bad += check("an id 10 is refused (1)", put_begin(10, 0, 0) == 1u);
    }
    memcpy(&st, &proj_slot[2], sizeof st);
    erase_error = 1;
    proj_slot[2].raw[100] ^= 1;                          /* (marks the RAM copy, to see it is kept) */
    memcpy(&was, &proj_slot[2], sizeof was);
    bad += check("a flash error on commit (4) keeps the slot's RAM copy",
                 put_all(4, &st, sizeof st, st_crc32(&st, sizeof st)) == 4u && !memcmp(&proj_slot[2], &was, sizeof was));
    erase_error = 0;
    bad += check("an empty project object clears the slot", put_all(4, 0, 0, st_crc32(0, 0)) == 0 && !project_used(2));
    st.raw[200] ^= 1;                                    /* hash no longer matches */
    bad += check("a project whose own hash fails is refused", put_all(4, &st, sizeof st, st_crc32(&st, sizeof st)) == 2u);

    /* the runtime */
    reset();
    trk[2].step[5] = (step_t){{67}, 1, ST_NOTE, 0, 96, 0, 0};
    trk[2].p[P_LEVEL] = 77;
#if FELUCCA_LEGACY
    project_capture(&proj_scratch);
    proj_pack(&st, &proj_scratch);
#else
    (void)ts_save(TS_MUSIC);                             /* ((Skiff P10) the music's set: its steps, as LIST writes it) */
    project_capture(&proj_scratch);
    proj_pack2(&st, &proj_scratch, PROJ_SKF);
#endif
    host_tracks_init();
    bad += check("the runtime object restores the tracks",
                 put_all(0, &st, sizeof st, st_crc32(&st, sizeof st)) == 0 && trk[2].step[5].note[0] == 67u &&
                 trk[2].p[P_LEVEL] == 77);

    /* user preset banks: unknown record versions are kept as bytes */
    reset();
    {
        static up_bank_t b;
        memset(&b, 0, sizeof b);
        b.magic = UP_BANK_MAGIC;
        b.rsize = sizeof(up_rec_t);
        b.nslot = UP_PER_BANK;
        b.r[4].used = UP_USED;
        b.r[4].ver = 99;                                 /* a future record */
        memcpy(b.r[4].name, "Future", 7);
        b.r[4].p[0] = 55;
        bad += check("a bank with a future record version restores byte for byte",
                     put_all(6, &b, sizeof b, st_crc32(&b, sizeof b)) == 0 &&
                     st_load(OBJ_UPRESET0, proj_wire_u.raw, sizeof proj_wire_u.raw) == (int)sizeof b &&
                     !memcmp(proj_wire_u.raw, &b, sizeof b));   /* ((2.0) proj_wire: SKF4's 2,560, the staging all) */
        b.nslot = 3;
        bad += check("a bank with the wrong slot count is refused", put_all(7, &b, sizeof b, st_crc32(&b, sizeof b)) == 2u);
    }
    /* (Skiff P10) the tape store: each chunk an object (40 + slot x 4 + chunk; the music's 56..59), read out and
     * written back exactly; the pattern pool's objects of before (16..31) taken and dropped */
    reset();
    {
        static uint8_t got[TS_OBJS][TS_LEN + 16u];
        static step_t want[NTRK][NSTEP];
        static uint32_t w0[TAPE_N], w1[TAPE_N];
        static uint8_t chunk[TS_LEN + 16u];
        uint32_t lens[TS_OBJS], crcs[TS_OBJS], c, k, n, e0;
        uint16_t node;
        int ok = 1;
        for (k = 0; k < NTRK; k++) {
            track_t *t = &trk[k];
            track_defaults_steps(t);
            t->step[3] = (step_t){{(uint8_t)(48u + k), 64}, 2, ST_NOTE, SF_ACCENT, 99};
            t->step[3].nudge = (int8_t)(-20 + (int32_t)k);
            t->step[3].cond = TC_2_4;
            step_set_rflam(&t->step[3], 5);
            motion_set_lock(t, 3, P_REV, (int16_t)(40 + k));
        }
        tape_set_len(8);
        for (n = 0; n < 600u; n++)
            (void)tape_insert(n % TAPE_TRACKS, (n * 61u) % (8u * TAPE_BAR) | (40u + n % 50u) << 20 | (n % NTRK) << 27,
                              (1u + n) | 90u << 20, TAPE_ANY_PASS);
        tp_trk[2].mute = 1;
        memcpy(want, trk[0].step, sizeof want);
        for (k = n = 0; k < TAPE_TRACKS; k++)
            for (node = tp_trk[k].first; node != TAPE_END; node = tp_nx[node], n++) {
                w0[n] = tp_w0[node];
                w1[n] = tp_w1[node];
            }
        ok &= project_save(1) == 0 && ts_has(1);
        for (c = 0; c < TS_OBJS; c++)
            ok &= list(TS_OBJ0 + c, &lens[c], &crcs[c]) == 0;
        ok &= !lens[0] && !lens[3] && lens[4] == TS_LEN + 16u && lens[5] == TS_LEN && lens[7] == TS_LEN &&
              lens[16] == TS_LEN + 16u && lens[19] == TS_LEN;   /* (slot 2's; the music's: LIST wrote it) */
        for (c = 0; c < TS_OBJS; c++)
            ok &= !lens[c] || (get_all(TS_OBJ0 + c, got[c], lens[c]) == 0 && st_crc32(got[c], lens[c]) == crcs[c]);
        ok &= !memcmp(got[4], nor + ts_sector(tst.sect[1][0] - 1u) + 256u, TS_LEN) &&
              !memcmp(got[4] + TS_LEN, &tst.meta[1], 16u);
        bad += check("(P10) the tape store: 20 objects; a set's chunks 3840 B (chunk 0 + its 16 B meta), CRC as listed; "
                     "an empty slot's 0", ok);
        for (c = 0; c < TS_NSECT; c++)                    /* a new unit: the store's sectors erased */
            memset(nor + ts_sector(c), 0xFF, 0x1000u);
        tape_init();
        ts_boot();
        ok = !ts_has(1) && !ts_has(TS_MUSIC);
        for (c = 0; c < TS_OBJS; c++)
            ok &= put_all(TS_OBJ0 + c, got[c], lens[c], lens[c] ? crcs[c] : st_crc32(got[c], 0)) == 0;
        ok &= ts_has(1) && ts_has(TS_MUSIC) && !ts_has(0) && !ts_has(2);
        tape_init();
        ts_boot();
        for (k = 0; k < NTRK; k++)
            track_defaults_steps(&trk[k]);
        ok &= ts_has(1) && !ts_load(1, 1) && !memcmp(trk[0].step, want, sizeof want) && tp_trk[2].mute &&
              tape_length() == 8u * TAPE_BAR;
        for (k = n = 0; k < TAPE_TRACKS; k++)
            for (node = tp_trk[k].first; node != TAPE_END; node = tp_nx[node], n++)
                ok &= tp_w0[node] == w0[n] && tp_w1[node] == w1[n];
        ok &= n == 600u;
        bad += check("  restored onto an empty store: every set as it was (the tape, the steps load exactly), after a "
                     "power cycle", ok);
        e0 = erases;
        ok = put_all(TS_OBJ0 + 4u, got[4], TS_LEN + 16u, crcs[4]) == 0 && put_all(TS_OBJ0 + 5u, got[5], TS_LEN, crcs[5]) == 0 &&
             ts_has(1) && tst.sect[1][0] && erases == e0 + 2u;
        tape_init();
        ts_boot();
        bad += check("  half a set coming in (two of its four chunks): the slot keeps the set it had (power cycle too)",
                     ok && ts_has(1));
        ok = 1;
        for (c = 4u; c < 8u; c++)
            ok &= put_all(TS_OBJ0 + c, got[c], 0, st_crc32(got[c], 0)) == 0;
        ok &= !ts_has(1);
        tape_init();
        ts_boot();
        bad += check("  a set of four empty objects: the slot's set gone (power cycle too)", ok && !ts_has(1));
        memcpy(chunk, got[7], TS_LEN);
        chunk[2u * TS_SEQ_REC + 1u + 4u * 9u + 4u] = 7u; /* (T3 step 5: n 7, no firmware wrote) */
        ok = put_all(TS_OBJ0 + 7u, chunk, TS_LEN, st_crc32(chunk, TS_LEN)) == 2u;
        memcpy(chunk, got[4], TS_LEN + 16u);
        chunk[3] = 0xC0u;                                 /* (a record's bits 30, 31: no firmware wrote) */
        ok &= put_all(TS_OBJ0 + 4u, chunk, TS_LEN + 16u, st_crc32(chunk, TS_LEN + 16u)) == 2u;
        memcpy(chunk, got[4], TS_LEN + 16u);
        chunk[4] = chunk[5] = 0xFFu;                      /* (a record 2^20 - 1 ticks long: past any tape) */
        chunk[6] |= 0x0Fu;
        ok &= put_all(TS_OBJ0 + 4u, chunk, TS_LEN + 16u, st_crc32(chunk, TS_LEN + 16u)) == 2u;
        memcpy(chunk, got[4], TS_LEN + 16u);
        chunk[TS_LEN] = 0;                                /* (L 0 bars) */
        chunk[TS_LEN + 1u] = 0;
        ok &= put_all(TS_OBJ0 + 4u, chunk, TS_LEN + 16u, st_crc32(chunk, TS_LEN + 16u)) == 2u && ts_has(TS_MUSIC);
        bad += check("  a chunk holding a step, a record (bits 30, 31; a length past 256 bars) or a meta no firmware wrote: refused (2), the store as it was", ok);
        song.playing = 1;
        bad += check("  playing: PUT and LIST refused (3)", put_begin(TS_OBJ0 + 4u, TS_LEN + 16u, crcs[4]) == 3u &&
                     list(TS_OBJ0, &len, &crc) == 3u);
        song.playing = 0;
        bad += check("  a chunk of the wrong length: refused at begin (1)", put_begin(TS_OBJ0 + 5u, TS_LEN + 16u, 0) == 1u &&
                     put_begin(TS_OBJ0 + 4u, TS_LEN, 0) == 1u && put_begin(60u, 0, 0) == 1u);
        e0 = erases;
        memset(chunk, 0x5A, sizeof chunk);
        ok = put_all(16u, chunk, 3552u, st_crc32(chunk, 3552u)) == 0 && put_all(31u, chunk, 0, st_crc32(chunk, 0)) == 0 &&
             erases == e0 && put_begin(18u, 3553u, 0) == 1u;
        bad += check("  an archive of before: its pattern pool's chunks (16..31, up to 3552 B) taken and dropped", ok);
    }
    /* (Skiff P10) a restore cut after three chunks of slot 1's set, the store full: the set coming in is released by a
     * save short of room, a USB reset, or ED_BK_STALE_MS without a chunk; then the saves work, the music's set again */
    {
        static uint8_t part[3][TS_LEN + 16u];
        uint32_t lens[3], crcs[3], c, mode, slot;
        int ok = 1, held = 1;
        for (mode = 0; mode < 3u; mode++) {
            reset();
            tape_set_len(2);
            for (slot = 0; slot < TS_SLOTS; slot++) {
                (void)tape_insert(0, (100u + slot) | 60u << 20, 50u | 90u << 20, TAPE_ANY_PASS);
                ok &= ts_save(slot) == 0;
            }
            for (c = 0; c < 3u; c++)
                ok &= list(TS_OBJ0 + c, &lens[c], &crcs[c]) == 0 && get_all(TS_OBJ0 + c, part[c], lens[c]) == 0;
            for (c = 0; c < 3u; c++)
                ok &= put_all(TS_OBJ0 + c, part[c], lens[c], crcs[c]) == 0;
            ok &= ts_restoring() && !ts_has(TS_MUSIC) && ts_music_lost();
            if (mode == 1u)
                usb.resets++;
            else if (mode == 2u)
                fm1_ms += ED_BK_STALE_MS + 1u;
            ed_bk_poll();
            held &= mode ? !ts_restoring() : ts_restoring();
            (void)tape_insert(0, 900u | 62u << 20, 50u | 90u << 20, TAPE_ANY_PASS);
            ok &= ts_save(1) == 0 && !ts_restoring() && ts_music_lost();
            ok &= ts_save(TS_MUSIC) == 0 && ts_has(TS_MUSIC) && !ts_music_lost();
            tape_init();
            ts_boot();
            ok &= ts_has(0) && ts_has(1) && ts_has(TS_MUSIC) && !ts_restoring();
        }
        bad += check("(P10) a restore cut after 3 chunks: released (a save short of room, USB reset, 15 s), saves work, "
                     "the music's set written again", ok && held);
    }
#if FELUCCA_LEGACY                                       /* ((2.0) the converter's: web/conv) */
    /* (Skiff phase 5) an older archive: its projects (SKF1) stored as they come; once its music (id 0, the last
     * object of a restore) is in, (P10) SKF5 and their slot's set (the steps) */
    reset();
    {
        static project_t pj;
        static project_store_t w;
        int ok;
        trk[0].step[0] = (step_t){{60}, 1, ST_NOTE, 0, 96, 0, 0};
        project_capture(&pj);
        ok = proj_pack(&w, &pj) && put_all(2, &w, sizeof w, st_crc32(&w, sizeof w)) == 0 &&
             st_load(OBJ_PROJECT0, &st, sizeof st) == (int)sizeof w && !ts_has(0);
        ok &= put_all(0, &w, sizeof w, st_crc32(&w, sizeof w)) == 0 && trk[0].step[0].note[0] == 60u &&
              st_load(OBJ_PROJECT0, &st, sizeof st) == (int)PROJ_SKF->size && *(const uint32_t *)st.raw == PROJ_SKF->magic &&
              *(const uint32_t *)proj_slot[0].raw == PROJ_SKF->magic && ts_has(0);
        trk[0].step[0].note[0] = 50u;
        project_load(0);
        ok &= trk[0].step[0].note[0] == 60u;
        bad += check("(phase 5) an older archive: its projects as they came, (P10) SKF5 and a set once its music is in", ok);
    }
    /* (phase 6) project objects of 1280 (SKF2), 2048 (SKF3) and 3840 bytes (a retained slot holding one): each kept in
     * its format */
    reset();
    {
        static project_t pj;
        static project_store_t w2, w3;
        int ok;
        project_capture(&pj);
        ok = proj_pack2(&w2, &pj, &PROJ_SKF2) && put_all(3, &w2, PROJ_STORE2, st_crc32(&w2, PROJ_STORE2)) == 0 &&
             st_load(OBJ_PROJECT0 + 1u, &st, sizeof st) == (int)PROJ_STORE2 && *(const uint32_t *)st.raw == PROJ_MAGIC2 &&
             put_all(4, &w2, sizeof w2, st_crc32(&w2, sizeof w2)) == 0 &&
             st_load(OBJ_PROJECT0 + 2u, &st, sizeof st) == (int)PROJ_STORE2;
        ok &= proj_pack2(&w3, &pj, &PROJ_SKF3) && put_all(5, &w3, PROJ_STORE3, st_crc32(&w3, PROJ_STORE3)) == 0 &&
              st_load(OBJ_PROJECT0 + 3u, &st, sizeof st) == (int)PROJ_STORE3 && *(const uint32_t *)st.raw == PROJ_MAGIC3 &&
              put_all(5, &w3, sizeof w3, st_crc32(&w3, sizeof w3)) == 0 &&
              st_load(OBJ_PROJECT0 + 3u, &st, sizeof st) == (int)PROJ_STORE3 &&
              put_all(0, &w3, PROJ_STORE3, st_crc32(&w3, PROJ_STORE3)) == 0;
        bad += check("(phase 6) project objects of 1280, 2048 and 3840 bytes: SKF2 and SKF3 kept as they are", ok);
    }
#else
    /* (2.0) project objects of 2048 (SKF3) and 3840 bytes (1.x's backups: a slot's whole RAM copy): kept as SKF3, and a
     * backup gives the record's own size (2048: 1.4.x restores it). (P10) OLD FORMAT: kept as they are; the music of
     * one (id 0) taken, the music as it was */
    reset();
    {
        static project_t pj;
        static project_store_t w3;
        static uint8_t w3x[PROJ_STORE_SIZE];
        int ok;
        project_capture(&pj);
        ok = proj_pack2(&w3, &pj, &PROJ_SKF3) && put_all(5, &w3, PROJ_STORE3, st_crc32(&w3, PROJ_STORE3)) == 0 &&
             st_load(OBJ_PROJECT0 + 3u, &st, sizeof st) == (int)PROJ_STORE3 && *(const uint32_t *)st.raw == PROJ_MAGIC3;
        memset(w3x, 0, sizeof w3x);
        memcpy(w3x, &w3, PROJ_STORE3);
        ok &= put_all(5, w3x, sizeof w3x, st_crc32(w3x, sizeof w3x)) == 0 &&
              st_load(OBJ_PROJECT0 + 3u, &st, sizeof st) == (int)PROJ_STORE3 &&
              list(5, &len, &crc) == 0 && len == PROJ_STORE3 && crc == st_crc32(&w3, PROJ_STORE3) &&
              put_all(0, &w3, PROJ_STORE3, st_crc32(&w3, PROJ_STORE3)) == 0;
        ok &= proj_old(3);
        bad += check("(2.0) project objects of 2048 and 3840 bytes (SKF3): kept as they are (P10: OLD FORMAT), listed at "
                     "2048", ok);
    }
#endif
    printf("backup test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
