/* SPDX-License-Identifier: GPL-3.0-only */
/* The global GRID: the lines that recording, live playing, the ARP's start and the sequencer's RUN toggles snap to.
 * Pure functions on tape ticks (480 to the quarter, 4/4: a bar is 1920); every grid divides a bar, so the lines repeat
 * every bar and every loop. Swing (the global SWING only, 0..100) moves the odd lines of the straight 1/8, 1/16 and
 * 1/32 later by swing * ticks / 250, as core.h swing_step_len does the steps. */
#define TAPE_TPQ 480u
#define TAPE_BAR (4u * TAPE_TPQ)
#define GRID_N 10u
#define GRID_LATE_MS_BPM 4u                /* the late window's 20 ms, in ticks: BPM * 4 / 25 (0.16 ticks a BPM) */

static const uint16_t GRID_TICKS[GRID_N] = {0, 1920, 960, 480, 240, 120, 60, 320, 160, 80};
static const uint8_t GRID_SWINGS[GRID_N] = {0, 0, 0, 0, 1, 1, 1, 0, 0, 0};

static uint32_t grid_ticks(uint32_t idx) { return idx < GRID_N ? GRID_TICKS[idx] : 0u; }

/* the tick of line n: odd lines of a swinging grid are later */
static uint32_t grid_line(uint32_t idx, uint32_t swing, uint32_t line)
{
    uint32_t ticks = grid_ticks(idx);
    swing = swing > SWING_MAX ? SWING_MAX : swing;
    return line * ticks + ((line & 1u) && idx < GRID_N && GRID_SWINGS[idx] ? swing * ticks / 250u : 0u);
}

/* the last line at or before tick (OFF: tick itself) */
static uint32_t grid_floor(uint32_t idx, uint32_t swing, uint32_t tick)
{
    uint32_t ticks = grid_ticks(idx), line;
    if (!ticks)
        return tick;
    line = tick / ticks;
    if (grid_line(idx, swing, line) > tick)
        line--;
    return grid_line(idx, swing, line);
}

/* the first line after tick (OFF: tick itself) */
static uint32_t grid_next(uint32_t idx, uint32_t swing, uint32_t tick)
{
    uint32_t ticks = grid_ticks(idx), line;
    if (!ticks)
        return tick;
    line = tick / ticks;
    if (grid_line(idx, swing, line) > tick)
        return grid_line(idx, swing, line);
    return grid_line(idx, swing, line + 1u);
}

/* how late after a line a note still counts as on it: an eighth of the grid, at most 20 ms */
static uint32_t grid_window(uint32_t idx, uint32_t bpm)
{
    uint32_t ticks = grid_ticks(idx) / 8u, ms20 = bpm * GRID_LATE_MS_BPM / 25u;
    return ticks < ms20 ? ticks : ms20;
}

/* the GRID setting (ui.c ui_grid: favorites.factory[15][26], through the unit's settings pointer; a value past the
 * table, or a build without the UI: OFF) */
static uint32_t grid_idx(void)
{
    uint32_t idx = MSET(15, 26);
    return idx < GRID_N ? idx : 0u;
}

/* tick (the head) is later than the line's window after its line, the GRID on: 1, *ahead the ticks to the next line */
static int grid_late(uint32_t tick, uint32_t *ahead)
{
    uint32_t idx = grid_idx(), swing = song.g[G_SWING] < 0 ? 0u : (uint32_t)song.g[G_SWING];
    if (!idx || tick - grid_floor(idx, swing, tick) <= grid_window(idx, (uint32_t)song.g[G_BPM]))
        return 0;
    *ahead = grid_next(idx, swing, tick) - tick;
    return 1;
}
