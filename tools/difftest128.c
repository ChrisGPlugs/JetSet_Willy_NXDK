/*
 * difftest128.c - check the translated JSW128 code against the original.
 *
 * Like difftest.c, for Jet Set Willy 128 (HL9) on a 128K machine: paging,
 * the AY, and IM2 interrupts (the music player runs from them). At every
 * pass through the main loop it prints a hash of all RAM (the 64K view plus
 * every bank), the T-states since the previous pass, and a hash of the AY
 * register writes since then (with their T-states).
 *
 *   difftest128 ref|port <script> <passes> [trace_file]
 *
 * ref with a trace_file also records every instruction address executed
 * (as "bank:address" for 0xC000 and up) for tools/z80c's code map.
 *
 * scripts: "title" (no input: title screen, animation and tune for a while,
 * then ENTER), "idle" (start and stand still), "play:<n>" (random play;
 * cheat poked on at pass 0, teleport through all 256 room numbers),
 * "finale" (all items collected at pass 5, teleport to the Master Bedroom,
 * walk left to the bed, then the run to the toilet),
 * "walk:<n>" (random play without the cheat: deaths, game over, restarts),
 * "keys:<n>" (random play that also presses every non-movement key: pause,
 * tune toggle, the cheat-mode keys).
 *
 * Environment: JSW128_TAPE (default "Data files/JetSetWilly128.tzx"),
 * JSW_CONTEND=1 (port only: ULA contention on, for timing figures).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/zx.h"
#include "../src/jsw128.h"
#include "z80ref/z80.h"
#ifdef PORT
#include "../src/z80c.h"
#endif

static jsw128_image_t img;
static long    pass_no = -1, max_passes;
static int     play, idle, title_only, allkeys, walk, finale;
static uint32_t rng;
static uint64_t last_t, title_t0;
static uint64_t ay_hash = 1469598103934665603ull;

static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }
static int hold_left, hold_right, hold_jump, hold_joy, hold_enter;

static void set_inputs(long pass)
{
    zx_keys_clear();
    zx_kempston = 0;
    if (pass < 0) { if (!title_only) zx_key(ZX_ROW_ENTER_H, 0, 1); return; }
    if (idle || !play) return;
    if (finale && pass >= 5 && pass < 7) {           /* teleport to room 35 */
        zx_key(ZX_ROW_0_6, 1, 1);
        zx_key(ZX_ROW_1_5, 0, 1); zx_key(ZX_ROW_1_5, 1, 1); zx_key(ZX_ROW_0_6, 2, 1);   /* 1+2+8 = 35 */
        return;
    }
    if (finale && pass >= 7) {
        if (mem[34271] < 2) zx_key(ZX_ROW_Q_T, 0, 1);  /* Q: left to the bed; then the game drives */
        return;
    }
    long phase = pass % 300;
    if (!walk && (phase == 150 || phase == 151)) {               /* teleport: 9 + 8 bits */
        int room = (int)((pass / 300 * 37) & 255);
        zx_key(ZX_ROW_0_6, 1, 1);
        for (int b = 0; b < 5; b++) if (room & (1 << b)) zx_key(ZX_ROW_1_5, b, 1);
        if (room & 32)  zx_key(ZX_ROW_0_6, 2, 1);     /* 8 */
        if (room & 64)  zx_key(ZX_ROW_0_6, 3, 1);     /* 7 */
        if (room & 128) zx_key(ZX_ROW_0_6, 4, 1);     /* 6 */
        return;
    }
    if (hold_left == 0 && hold_right == 0 && (rnd() % 4) == 0) {
        if (rnd() & 1) hold_left = 1 + rnd() % 16; else hold_right = 1 + rnd() % 16;
    }
    if (hold_jump == 0 && (rnd() % 6) == 0) hold_jump = 1 + rnd() % 2;
    if (hold_joy == 0 && (rnd() % 60) == 0) hold_joy = 1 + rnd() % 10;
    if (hold_enter == 0 && (rnd() % 150) == 0) hold_enter = 1;
    if (hold_left)  { hold_left--;  zx_key(ZX_ROW_P_Y, 1, 1); }
    if (hold_right) { hold_right--; zx_key(ZX_ROW_P_Y, 0, 1); }
    if (hold_jump)  { hold_jump--;  zx_key(ZX_ROW_SPACE_B, 0, 1); }
    if (hold_enter) { hold_enter--; zx_key(ZX_ROW_ENTER_H, 0, 1); }
    if (hold_joy)   { hold_joy--;   zx_kempston = (uint8_t)(rnd() & 0x13); }
    if ((rnd() % 500) == 0) { zx_key(ZX_ROW_A_G, 0, 1); zx_key(ZX_ROW_P_Y, 0, 1); }
    if (allkeys && (rnd() % 40) == 0) {
        /* any key at all, one at a time */
        int row = (int)(rnd() % 8), bit = (int)(rnd() % 5);
        if (!(row == ZX_ROW_SHIFT_V && bit == 0) && row != ZX_ROW_A_G) zx_key(row, bit, 1);
    }
}

static uint64_t mem_hash(void)
{
    uint64_t h = 1469598103934665603ull;
    for (uint32_t a = 0x4000; a < 0x10000; a++) h = (h ^ mem[a]) * 1099511628211ull;
    for (int b = 0; b < 8; b++) {
        if (b == 2 || b == 5 || b == zx_bank_c000) continue;
        for (int i = 0; i < 0x4000; i++) h = (h ^ zx_banks[b][i]) * 1099511628211ull;
    }
    return h;
}

static void at_mainloop(uint64_t t)
{
    pass_no++;
    if (pass_no == 0 && play && !walk) mem[34275] = 10;   /* WRITETYPER typed */
    if (pass_no == 5 && finale) { mem[34270] = 0; mem[34271] = 1; }   /* all items collected */
    printf("%ld %016llx %llu %016llx room %d lives %d bank %d 7ffd %d wt %d mode %d\n", pass_no,
           (unsigned long long)mem_hash(), (unsigned long long)(t - last_t),
           (unsigned long long)ay_hash, mem[33824], mem[34252], zx_bank_c000, zx_last_7ffd, mem[34275], mem[34271]);
    last_t = t;
    if (pass_no >= max_passes) { fflush(stdout); exit(0); }
    set_inputs(pass_no);
}

static void at_title(uint64_t t)
{
    if (title_only) {
        /* Stay on the title for 40 s of emulated time, then start. */
        if (!title_t0) title_t0 = t;
    }
    if (!title_only || (t - title_t0) > 40ull * 70908 * 50) set_inputs(-1);
}

static void check_stall(uint64_t t)
{
    if (title_only && pass_no < 0 && title_t0 && t - title_t0 > 40ull * 70908 * 50)
        zx_key(ZX_ROW_ENTER_H, 0, 1);
    if (t - last_t > 180ull * 50 * 70908) { printf("STALL after pass %ld\n", pass_no); fflush(stdout); exit(3); }
}

static void log_ay(void)
{
    for (int i = 0; i < zx_ay_count; i++) {
        ay_hash = (ay_hash ^ (7u * zx_ay_log[i].reg + 1031u * zx_ay_log[i].val)) * 1099511628211ull;
    }
    zx_ay_count = 0;
}

/* ---- reference ---- */
static uint8_t  rb(void *u, uint16_t a) { (void)u; return mem[a]; }
static void     wb(void *u, uint16_t a, uint8_t v) { (void)u; if (a >= 0x4000) mem[a] = v; }
static uint8_t  pin(z80 *z, uint16_t port) { (void)z; return zx_in(port); }
static void     pout(z80 *z, uint16_t port, uint8_t v) { zx_t = z->cyc; zx_out(port, v); }

static void ref_before_int(z80 *z)
{
    zx_t = z->cyc;
    while (zx_t >= zx_next_frame) {
        log_ay();
        zx_next_frame += zx_t_per_frame;
        zx_int_pending = 1;
        check_stall(zx_t);
    }
    z->int_pending = zx_int_active() ? 1 : 0;
    z->int_data = 0xFF;
}

/* Trace: one bit per (bank, address); banks only matter at 0xC000 up. */
static uint8_t seen[8][65536 / 8];
static void mark(uint16_t pc)
{
    int b = pc >= 0xC000 ? zx_bank_c000 : 0;
    seen[b][pc >> 3] |= (uint8_t)(1u << (pc & 7));
}
static const char *trace_file;
static void write_trace(void)
{
    if (!trace_file) return;
    FILE *f = fopen(trace_file, "w");
    for (int b = 0; b < 8; b++)
        for (uint32_t a = 0; a < 65536; a++)
            if (seen[b][a >> 3] & (1u << (a & 7))) fprintf(f, "%d:%u\n", b, a);
    fclose(f);
}

#ifdef PORT
/* ---- port ---- */
static void port_hook(uint16_t a)
{
    if (a == JSW128_MAINLOOP) { log_ay(); at_mainloop(zx_t); }
    else if (a == 34762) at_title(zx_t);
}
static void port_frame(void) { log_ay(); check_stall(zx_t); }
#endif

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: difftest128 ref|port script passes [trace]\n"); return 2; }
    const char *tape = getenv("JSW128_TAPE") ? getenv("JSW128_TAPE") : "Data files/JetSetWilly128.tzx";
    FILE *f = fopen(tape, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", tape); return 2; }
    static uint8_t buf[1 << 20];
    long n = (long)fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (jsw128_load(buf, n, &img)) { fprintf(stderr, "%s: not JSW128 HL9\n", tape); return 2; }
    if (!strcmp(argv[2], "idle")) idle = 1;
    else if (!strcmp(argv[2], "title")) title_only = 1;
    else if (!strncmp(argv[2], "play:", 5)) { play = 1; rng = (uint32_t)atoi(argv[2] + 5); }
    else if (!strcmp(argv[2], "finale")) { play = 1; finale = 1; rng = 1; }
    else if (!strncmp(argv[2], "walk:", 5)) { play = 1; walk = 1; rng = (uint32_t)atoi(argv[2] + 5); }
    else if (!strncmp(argv[2], "keys:", 5)) { play = 1; allkeys = 1; rng = (uint32_t)atoi(argv[2] + 5); }
    else { fprintf(stderr, "unknown script\n"); return 2; }
    max_passes = atol(argv[3]);
    if (argc >= 5) trace_file = argv[4];
    atexit(write_trace);

    jsw128_prepare(&img);
    zx_contention = (getenv("JSW_CONTEND") && *getenv("JSW_CONTEND") == '1') ? 1 : 0;
    set_inputs(-1);
    if (title_only) zx_keys_clear();

    if (!strcmp(argv[1], "port")) {
#ifdef PORT
        z80c_hook = port_hook;
        zx_frame_hook = port_frame;
        jsw128_z80_run(JSW128_BEGIN);
        printf("LEFT CODE: kind %d at %04X (bank %d) after pass %ld\n", z80c_bad_kind, z80c_bad_addr, zx_bank_c000, pass_no);
        return 1;
#else
        fprintf(stderr, "built without the port\n");
        return 2;
#endif
    }
    z80 z;
    z80_init(&z);
    z.read_byte = rb; z.write_byte = wb; z.port_in = pin; z.port_out = pout;
    z.before_int = ref_before_int;
    z.pc = JSW128_BEGIN;
    z.i = 0x3F;
    z.interrupt_mode = 1;
    z.iff1 = z.iff2 = 1;
    for (;;) {
        uint16_t pc = z.pc;
        if (!z.halted) {
            if (pc == JSW128_MAINLOOP) { zx_t = z.cyc; log_ay(); at_mainloop(z.cyc); }
            else if (pc == 34762) at_title(z.cyc);
            if (pc < 0x4000) { printf("ROM %04X after pass %ld\n", pc, pass_no); return 4; }
            mark(pc);
        }
        z80_step(&z);
        if (zx_paging_alias) { printf("ALIAS after pass %ld\n", pass_no); return 5; }
        if (zx_int_pending && !z.int_pending) zx_int_pending = 0;   /* accepted */
    }
}
