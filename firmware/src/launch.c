/* SPDX-License-Identifier: GPL-3.0-only */
/* Skiff: the arm table (the PERFORM layer's keys). A track's SEQ RUN (seq_run: its step sequencer plays only while set
 * and playing) or MUTE turned while playing is armed: the main loop sets `at` (song_ticks) first, `op` last; the audio
 * ISR applies it at the track's first step boundary from `at` on (seq_tick) and clears op. Stopped it applies at once.
 * Included by seq.c (before seq_tick); arm_apply runs in the ISR, only with something armed, not inlined. */
enum { ARM_NONE, ARM_RUN_ON, ARM_RUN_OFF, ARM_MUTE, ARM_UNMUTE };
typedef struct {
    volatile uint8_t op;
    uint32_t at;
} arm_t;
static arm_t arm[NTRK];
static uint8_t seq_run = (uint8_t)((1u << NTRK) - 1u);   /* a bit a track; RAM only (default all on) */

static int seq_running(uint32_t k) { return (seq_run >> k) & 1u; }
/* when a press now lands: the next GRID line (the swung one, as the tape's), OFF: the next step */
static uint32_t arm_when(uint32_t grid)
{
    int32_t swing = song.g[G_SWING];
    if (!grid)
        return song_ticks;
    return grid_next(grid, swing < 0 ? 0u : (uint32_t)swing, song_ticks);
}
/* track k's armed op now (the ISR at a step boundary, or the main loop stopped) */
static void arm_do(track_t *t, uint32_t op)
{
    uint32_t k = trk_index(t);
    if (op == ARM_RUN_ON) {
        seq_run |= (uint8_t)(1u << k);
    } else if (op == ARM_RUN_OFF) {
        seq_run &= (uint8_t)~(1u << k);
        seq_release(t);
        t->nud = 0;
        t->rat_left = 0;
    } else if (op == ARM_MUTE || op == ARM_UNMUTE) {
        t->p[P_MUTE] = (int16_t)(op == ARM_MUTE);
    }
}
/* track t's playing step has just ended: its armed op, when due. Not inlined: seq_tick's step loop keeps its shape
 * (tests/target_budget.py) */
static __attribute__((noinline)) void arm_apply(track_t *t)
{
    arm_t *a = &arm[trk_index(t)];
    if ((int32_t)(song_ticks - a->at) < 0)
        return;
    arm_do(t, a->op);
    a->op = ARM_NONE;
}
/* OCT- in PERFORM, a project load: nothing armed comes in after all: 1 there was some */
static int arm_cancel(void)
{
    uint32_t k;
    int was = 0;
    for (k = 0; k < NTRK; k++) {
        was |= arm[k].op != ARM_NONE;
        arm[k].op = ARM_NONE;
    }
    return was;
}
/* each frame (ui_input): an op armed when the transport stopped comes in now, as while stopped */
static void arm_poll(void)
{
    uint32_t k, op;
    if (song.playing)
        return;
    for (k = 0; k < NTRK; k++)
        if ((op = arm[k].op) != ARM_NONE) {
            arm[k].op = ARM_NONE;
            fm1_irq_off();
            arm_do(&trk[k], op);
            fm1_irq_on();
        }
}
