/*
 * jsw.c - loader and glue for the translated Jet Set Willy code.
 *
 * Jet Set Willy (C) 1984 Matthew Smith / Software Projects. This file:
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "zx.h"
#include "z80c.h"
#include "jsw.h"

void (*z80c_hook)(uint16_t addr);
z80c_regs_t z80c_regs;
void (*jsw_hook)(uint16_t addr);
int jsw_in_title;

/* ---- tape images ---- */

/* Is this the 32768-byte block at 32768 that holds the game? BEGIN starts
 * with DI and pokes the address of ENTERCODES onto the stack. */
static int is_jsw_block(const uint8_t *b, long n)
{
    if (n != JSW_IMAGE_SIZE) return 0;
    const uint8_t *p = b + (JSW_BEGIN - JSW_ORIGIN);
    static const uint8_t sig[] = { 0xF3, 0x21, 0xFF, 0x5B, 0x36, 0x86, 0x2B, 0x36, 0x9F };
    return memcmp(p, sig, sizeof sig) == 0;
}

/* A data block as stored on tape: flag, payload, checksum. */
static int take_block(const uint8_t *blk, long n, uint8_t *image)
{
    if (n < 2 || blk[0] != 0xFF) return 0;
    uint8_t x = 0;
    for (long i = 0; i < n; i++) x ^= blk[i];
    if (x) return 0;
    if (!is_jsw_block(blk + 1, n - 2)) return 0;
    memcpy(image, blk + 1, JSW_IMAGE_SIZE);
    return 1;
}

static int load_tzx(const uint8_t *d, long len, uint8_t *image)
{
    long i = 10;
    while (i < len) {
        uint8_t id = d[i++];
        long n;
        switch (id) {
        case 0x10: if (i + 4 > len) return -1; n = d[i + 2] | d[i + 3] << 8;
                   if (i + 4 + n > len) return -1;
                   if (take_block(d + i + 4, n, image)) { return 0; }
                   i += 4 + n; break;
        case 0x11: if (i + 18 > len) return -1; n = d[i + 15] | d[i + 16] << 8 | (long)d[i + 17] << 16;
                   if (i + 18 + n > len) return -1;
                   if (take_block(d + i + 18, n, image)) { return 0; }
                   i += 18 + n; break;
        case 0x14: if (i + 10 > len) return -1; n = d[i + 7] | d[i + 8] << 8 | (long)d[i + 9] << 16;
                   i += 10 + n; break;
        case 0x12: i += 4; break;
        case 0x13: if (i >= len) return -1; i += 1 + 2 * d[i]; break;
        case 0x20: case 0x23: case 0x24: i += 2; break;
        case 0x21: case 0x30: if (i >= len) return -1; i += 1 + d[i]; break;
        case 0x22: case 0x25: case 0x27: break;
        case 0x26: if (i + 2 > len) return -1; i += 2 + 2 * (d[i] | d[i + 1] << 8); break;
        case 0x28: case 0x32: if (i + 2 > len) return -1; i += 2 + (d[i] | d[i + 1] << 8); break;
        case 0x2A: i += 4; break;
        case 0x2B: i += 5; break;
        case 0x31: if (i + 2 > len) return -1; i += 2 + d[i + 1]; break;
        case 0x33: if (i >= len) return -1; i += 1 + 3 * d[i]; break;
        case 0x35: if (i + 20 > len) return -1; i += 20 + (d[i + 16] | d[i + 17] << 8 | (long)d[i + 18] << 16 | (long)d[i + 19] << 24); break;
        case 0x5A: i += 9; break;
        default:   return -1;
        }
    }
    return -1;
}

static int load_tap(const uint8_t *d, long len, uint8_t *image)
{
    long i = 0;
    while (i + 2 <= len) {
        long n = d[i] | d[i + 1] << 8;
        if (i + 2 + n > len) return -1;
        if (take_block(d + i + 2, n, image)) return 0;
        i += 2 + n;
    }
    return -1;
}

int jsw_load(const uint8_t *data, long len, uint8_t *image)
{
    if (len >= 10 && !memcmp(data, "ZXTape!\x1A", 8)) return load_tzx(data, len, image);
    if (is_jsw_block(data, len)) { memcpy(image, data, JSW_IMAGE_SIZE); return 0; }
    return load_tap(data, len, image);
}

/* ---- running ---- */

/* The official Software Projects POKEs (see the disassembly's pokes page):
 * a nasty removed from Conservatory Roof, the unreachable item in Swimming
 * Pool moved, the Attic's misplaced arrow (the Attic bug that made the game
 * impossible to finish), and a wall block in Rescue Esmerelda made floor. */
static const struct { uint16_t addr; uint8_t val; } sp_fixes[] = {
    { 60231, 0 }, { 42183, 11 }, { 59901, 82 }, { 56876, 4 },
};

void jsw_prepare(const uint8_t *image, int options)
{
    zx_reset();
    zx_set_model(ZX_MODEL_48K);
    memset(mem + 0x4000, 0, 0x4000);
    memcpy(mem + JSW_ORIGIN, image, JSW_IMAGE_SIZE);
    if (options & JSW_OPT_FIXES)
        for (unsigned i = 0; i < sizeof sp_fixes / sizeof sp_fixes[0]; i++)
            mem[sp_fixes[i].addr] = sp_fixes[i].val;
    /* Bypass the code entry screen: BEGIN's LD (HL),134 / LD (HL),159 put
     * ENTERCODES (34463) on the stack; 135/202 make it TITLESCREEN (34762). */
    if (!(options & JSW_OPT_CODECARD)) {
        mem[33797] = JSW_TITLESCREEN >> 8;
        mem[33800] = JSW_TITLESCREEN & 0xFF;
    }
    jsw_in_title = 0;
}

static void hook(uint16_t addr)
{
    if (addr == JSW_TITLESCREEN) jsw_in_title = 1;
    else if (addr == JSW_STARTGAME) jsw_in_title = 0;
    if (jsw_hook) jsw_hook(addr);
}

static int bad_jump;
void z80c_bad_jump(uint16_t addr) { bad_jump = addr; }

void jsw_run(void)
{
    z80c_hook = hook;
    z80c_run(JSW_BEGIN);
}
