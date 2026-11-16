/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff W3) the editor's pattern pool (EDITOR_PROTOCOL.md "Pool"): SAVE > PATTERNS; INFO 57 01 caps bit 2. It acts as
 * the device's own control does, through the same functions (bank.c), and says so on the device. Included by editor.c.
 *   POOL: [1, id: CLEAR], ((2.0) [2, id: EMPTY] (its steps, NUDGEs, automation; it stays), [3: CLEAR UNUSED]) -> op,
 *     rc (0 done, 1 nothing to clear / empty / unused, 2 in use, 3 playing, 4 failed), then each pattern: its LEN (0
 *     empty), what uses it (bank.c pool_refs, at most 127) */
enum { ED_POOL = 78 };
#define ED_SONG2 1u                                 /* (2.0) INFO 58 01: bit 0 POOL's 2 3 */

static int ed_song_args_ok(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    (void)cmd;
    if (!n)
        return 1;
    return (n == 2u && a[0] >= 1u && a[0] <= 2u && a[1] < NPAT) || (n == 1u && a[0] == 3u);
}

static int ed_song_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t rc = 0, k, r;
    if (cmd != ED_POOL)
        return 0;
    if (n && a[0] == 3u) {                         /* ((2.0) CLEAR UNUSED) */
        if (!pool_unused()) rc = 1;
        else if (transport_busy()) rc = 3;
        else { pool_clear_unused(); rc = pool_unused() ? 4u : 0u; }
    } else if (n) {
        if (!pool_used(a[1])) rc = 1;
        else if (a[0] == 1u && pool_refs(a[1])) rc = 2;
        else if (transport_busy()) rc = 3;
        else if (a[0] == 2u) pool_empty(a[1]);   /* ((2.0) EMPTY: in use too) */
        else { pool_clear(a[1]); rc = pool_used(a[1]) ? 4u : 0u; }
    }
    ed_b(n ? a[0] : 0u);
    ed_b(rc);
    for (k = 0; k < NPAT; k++) {
        r = pool_refs(k);
        ed_b(pool_used(k) ? pool_len(k) : 0u);
        ed_b(r > 127u ? 127u : r);
    }
    return 1;
}
