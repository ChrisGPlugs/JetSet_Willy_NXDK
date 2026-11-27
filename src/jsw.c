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
#include "tape.h"

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
static int take_block(const uint8_t *blk, long n, void *u)
{
    if (blk[0] != 0xFF || !tape_block_ok(blk, n)) return 0;
    if (!is_jsw_block(blk + 1, n - 2)) return 0;
    memcpy(u, blk + 1, JSW_IMAGE_SIZE);
    return 1;
}

int jsw_load(const uint8_t *data, long len, uint8_t *image)
{
    if (is_jsw_block(data, len)) { memcpy(image, data, JSW_IMAGE_SIZE); return 0; }
    return tape_blocks(data, len, take_block, image) == 1 ? 0 : -1;
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

void jsw_run(void)
{
    z80c_hook = hook;
    z80c_run(JSW_BEGIN);
}
