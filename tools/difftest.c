/*
 * difftest.c - check the translated code against the original Z80 code.
 *
 * Runs the game image on a Z80 emulator ("ref") or runs the C translation
 * ("port") with the same scripted keyboard/joystick input, and prints a hash
 * of all RAM (display file, buffers, stack, variables, room data) plus the
 * T-states since the previous pass at every pass through the main loop
 * (MAINLOOP, 35245). Diff the two outputs: the first differing line is the
 * first pass where the game state diverged.
 *
 *   difftest ref|port <script> <passes>
 *
 * script: "idle"      start a game and stand still (stopwatch / timing check)
 *         "finale"    as play:1, but at pass 5 all items count as collected
 *                     and Willy is sent to the Master Bedroom, holding left
 *                     for the bed: runs Maria leaving, the run to the
 *                     bathroom and the toilet ending
 *         "codes"     code card kept (JSW_OPT_CODECARD): keys 1-4 and ENTER
 *                     are pressed on a T-state schedule at READCODE (34620)
 *         "play:<n>"  random play with seed n: the WRITETYPER cheat is
 *                     poked on, and Willy teleports to a new room every 300
 *                     passes; deaths, game over and restarts all happen.
 *
 * Environment: JSW_TAPE (default "Data files/JetSetWilly.tzx"),
 * JSW_CONTEND=1 (ULA contention on; the reference core has none),
 * JSW_ORIG=1 (no bug-fix POKEs).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/zx.h"
#include "../src/zx_font.h"
#include "../src/jsw.h"
#include "z80ref/z80.h"

static uint8_t image[JSW_IMAGE_SIZE];
static long    pass_no = -1, max_passes;
static int     play, idle, finale, codes;
static uint32_t rng;
static uint64_t last_t;

static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static uint8_t *cur_mem;
static int mem_mode(void) { return cur_mem[JSW_MODE]; }

static int hold_left, hold_right, hold_jump, hold_joy, hold_enter;

/* Keys used by JSW: left Q E T U O, right W R Y I P, jump the bottom row,
 * ENTER starts, A-G pauses, H-ENTER toggles the tune, 9+1..6 teleports. */
static void set_inputs(long pass)
{
    zx_keys_clear();
    zx_kempston = 0;
    if (pass < 0 || idle) {
        if (pass < 0) zx_key(ZX_ROW_ENTER_H, 0, 1);
        return;
    }
    if (!play) return;
    if (finale && pass >= 10 && mem_mode() >= 2) return;   /* the game runs Willy to the toilet */
    if (finale && pass >= 10) {
        zx_key(ZX_ROW_Q_T, 0, 1);                    /* Q = left */
        return;
    }
    if (finale && pass >= 5) {                       /* teleport to room 35 */
        zx_key(ZX_ROW_0_6, 1, 1);
        zx_key(ZX_ROW_1_5, 0, 1); zx_key(ZX_ROW_1_5, 1, 1); zx_key(ZX_ROW_0_6, 4, 1);
        return;
    }
    long phase = pass % 300;
    if (phase == 150 || phase == 151) {              /* teleport: 9 + room bits */
        int room = (int)((pass / 300 * 7) % 61);
        zx_key(ZX_ROW_0_6, 1, 1);
        for (int b = 0; b < 5; b++)
            if (room & (1 << b)) zx_key(ZX_ROW_1_5, b, 1);
        if (room & 32) zx_key(ZX_ROW_0_6, 4, 1);
        return;
    }
    if (hold_left == 0 && hold_right == 0 && (rnd() % 4) == 0) {
        if (rnd() & 1) hold_left = 1 + rnd() % 16; else hold_right = 1 + rnd() % 16;
    }
    if (hold_jump == 0 && (rnd() % 6) == 0) hold_jump = 1 + rnd() % 2;
    if (hold_joy == 0 && (rnd() % 60) == 0) hold_joy = 1 + rnd() % 10;
    if (hold_enter == 0 && (rnd() % 150) == 0) hold_enter = 1;
    if (hold_left)  { hold_left--;  zx_key(ZX_ROW_P_Y, 1, 1); }          /* O */
    if (hold_right) { hold_right--; zx_key(ZX_ROW_P_Y, 0, 1); }          /* P */
    if (hold_jump)  { hold_jump--;  zx_key(ZX_ROW_SPACE_B, 0, 1); }      /* SPACE */
    if (hold_enter) { hold_enter--; zx_key(ZX_ROW_ENTER_H, 0, 1); }      /* ENTER (tune toggle / start) */
    if (hold_joy)   { hold_joy--;   zx_kempston = (uint8_t)(rnd() & 0x13); }
    /* Pause (A) together with P: the pause loop sees P and resumes at once. */
    if ((rnd() % 500) == 0) { zx_key(ZX_ROW_A_G, 0, 1); zx_key(ZX_ROW_P_Y, 0, 1); }
}

static uint64_t mem_hash(const uint8_t *m)
{
    uint64_t h = 1469598103934665603ull;
    for (uint32_t a = 0x4000; a < 0x10000; a++)
        h = (h ^ m[a]) * 1099511628211ull;
    return h;
}

static void at_mainloop(uint8_t *m, uint64_t t)
{
    pass_no++;
    cur_mem = m;
    if (pass_no == 0 && play) m[JSW_TELEPORT] = 10;      /* the "cheat mode cheat" POKE */
    if (pass_no == 5 && finale) { m[JSW_ITEMS] = 0; m[JSW_MODE] = 1; }
    /* pass, hash, T-states since last pass, room, lives, items, mode */
    printf("%ld %016llx %llu %d %d %d %d\n", pass_no, (unsigned long long)mem_hash(m),
           (unsigned long long)(t - last_t), m[JSW_ROOM], m[JSW_LIVES], m[JSW_ITEMS], m[JSW_MODE]);
    last_t = t;
    if (pass_no >= max_passes) { fflush(stdout); exit(0); }
    set_inputs(pass_no);
}

/* Code card: a new input state every 10 frames, chosen at READCODE. */
static void code_inputs(uint64_t t)
{
    long k = (long)(t / (10ull * 69888));
    zx_keys_clear();
    if (k % 20 == 19) { zx_key(ZX_ROW_ENTER_H, 0, 1); return; }
    if (k % 2) zx_key(ZX_ROW_1_5, (int)((k / 2 + k / 7) % 4), 1);
}

static void at_reset(const uint8_t *m)
{
    printf("RESET %016llx\n", (unsigned long long)mem_hash(m));
    exit(1);
}

static void check_stall(uint64_t t)
{
    if (t - last_t > 120ull * 50 * 69888) {
        printf("STALL after pass %ld\n", pass_no);
        exit(3);
    }
}

/* ---- port ---- */
static void port_hook(uint16_t a)
{
    if (a == JSW_MAINLOOP) at_mainloop(mem, zx_t);
    else if (a == JSW_TITLESCREEN) set_inputs(-1);
    else if ((a == 34620 || a == 34631) && codes) code_inputs(zx_t);
}
static void port_frame(void) { check_stall(zx_t); }

/* ---- reference ---- */
static uint8_t refmem[65536];
static uint8_t rb(void *u, uint16_t a) { (void)u; return refmem[a]; }
static void    wb(void *u, uint16_t a, uint8_t v) { (void)u; if (a >= 0x4000) refmem[a] = v; }
static uint8_t pin(z80 *z, uint16_t port) { (void)z; return zx_in(port); }
static void    pout(z80 *z, uint16_t port, uint8_t v) { (void)z; (void)port; (void)v; }

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: difftest ref|port idle|play:<seed> passes\n"); return 2; }
    const char *tape = getenv("JSW_TAPE") ? getenv("JSW_TAPE") : "Data files/JetSetWilly.tzx";
    FILE *f = fopen(tape, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", tape); return 2; }
    static uint8_t buf[1 << 20];
    long n = (long)fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (jsw_load(buf, n, image)) { fprintf(stderr, "%s: not a Jet Set Willy tape\n", tape); return 2; }
    if (!strcmp(argv[2], "idle")) idle = 1;
    else if (!strncmp(argv[2], "play:", 5)) { play = 1; rng = (uint32_t)atoi(argv[2] + 5); }
    else if (!strcmp(argv[2], "finale")) { play = 1; finale = 1; rng = 1; }
    else if (!strcmp(argv[2], "codes")) codes = 1;
    else { fprintf(stderr, "unknown script %s\n", argv[2]); return 2; }
    max_passes = atol(argv[3]);

    int orig = getenv("JSW_ORIG") && *getenv("JSW_ORIG") == '1';
    jsw_prepare(image, (orig ? 0 : JSW_OPT_FIXES) | (codes ? JSW_OPT_CODECARD : 0));
    zx_contention = (getenv("JSW_CONTEND") && *getenv("JSW_CONTEND") == '1') ? 1 : 0;
    zx_install_font(NULL);
    set_inputs(-1);

    if (!strcmp(argv[1], "port")) {
        jsw_hook = port_hook;
        zx_frame_hook = port_frame;
        jsw_run();
        at_reset(mem);
    }
    memcpy(refmem, mem, sizeof refmem);
    z80 z;
    z80_init(&z);
    z.read_byte = rb; z.write_byte = wb; z.port_in = pin; z.port_out = pout;
    z.pc = JSW_BEGIN;
    for (;;) {
        if (z.pc == JSW_MAINLOOP) at_mainloop(refmem, z.cyc);
        else if (z.pc == JSW_TITLESCREEN) set_inputs(-1);
        if ((z.pc == 34620 || z.pc == 34631) && codes) code_inputs(z.cyc);
        if (z.pc == 0) at_reset(refmem);
        if ((z.cyc & 0xFFFFF) < 24) check_stall(z.cyc);
        z80_step(&z);
    }
}
