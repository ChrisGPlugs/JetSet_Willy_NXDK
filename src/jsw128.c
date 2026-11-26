/*
 * jsw128.c - loader for Jet Set Willy 128 (John Elliott, v0.04 Hacklevel 9).
 *
 * The tape is a BASIC loader plus named CODE blocks. Rather than run the
 * loader, this rebuilds the state it leaves behind (lines 20-160 of the
 * BASIC program and the hiload.js7 routines it calls):
 *
 *   hiload.js7  -> 25000 (bank 5)
 *   rooms.js6/4/3/1 -> 32768, then copied by USR 32001 to 0xC000 in bank
 *                  6, 4, 3, 1
 *   title.js7   -> 32768, then 6912 bytes to 0xC000 in bank 7
 *   rtime.js2   -> 32768-65535 (bank 2 and, with bank 0 paged, bank 0)
 *   main.tun    -> 63232, cheat.tun -> 63746, tune.bin -> 64000 (bank 0)
 *   title.tun   -> 16384 (LOAD "title.tun" CODE 16384)
 *   USR 32039   copies 6698 bytes from 25000 and then 2774 bytes from 16384
 *               to 0xDB00 in bank 7, restores bank 0 / ROM 1 (BANKM 0x10)
 *               and jumps to 33792 (BEGIN).
 *
 * Jet Set Willy (C) 1984 Matthew Smith / Software Projects; JSW128 by John
 * Elliott. This file: SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "zx.h"
#include "tape.h"
#include "jsw128.h"

typedef struct {
    jsw128_image_t *img;
    char name[11];          /* from the last header */
    int  have_header;
    unsigned found;         /* bit per block below */
} load_state;

enum { B_HILOAD, B_ROOMS6, B_ROOMS4, B_ROOMS3, B_ROOMS1, B_TITLE, B_RTIME, B_MAINTUN, B_CHEATTUN, B_TUNEBIN, B_TITLETUN, B_COUNT };
static const struct { const char *name; long len; } blocks[B_COUNT] = {
    { "hiload.js7", 7597 }, { "rooms.js6 ", 16384 }, { "rooms.js4 ", 16384 }, { "rooms.js3 ", 16384 },
    { "rooms.js1 ", 16384 }, { "title.js7 ", 6912 }, { "rtime.js2 ", 32768 }, { "main.tun  ", 256 },
    { "cheat.tun ", 254 }, { "tune.bin  ", 1536 }, { "title.tun ", 2774 },
};

static int on_block(const uint8_t *blk, long n, void *u)
{
    load_state *st = u;
    if (!tape_block_ok(blk, n)) return 0;
    if (blk[0] == 0x00 && n == 19) {                  /* header */
        memcpy(st->name, blk + 2, 10);
        st->name[10] = 0;
        st->have_header = 1;
        return 0;
    }
    if (blk[0] != 0xFF || !st->have_header) return 0;
    st->have_header = 0;
    const uint8_t *p = blk + 1;
    long len = n - 2;
    jsw128_image_t *im = st->img;
    for (int b = 0; b < B_COUNT; b++) {
        if (memcmp(st->name, blocks[b].name, 10) || len != blocks[b].len) continue;
        st->found |= 1u << b;
        switch (b) {
        case B_HILOAD:   memcpy(im->ram + 25000, p, len); break;
        case B_ROOMS6:   memcpy(im->banks[6], p, len); break;
        case B_ROOMS4:   memcpy(im->banks[4], p, len); break;
        case B_ROOMS3:   memcpy(im->banks[3], p, len); break;
        case B_ROOMS1:   memcpy(im->banks[1], p, len); break;
        case B_TITLE:    memcpy(im->banks[7], p, len); break;
        case B_RTIME:    memcpy(im->ram + 32768, p, len); break;
        case B_MAINTUN:  memcpy(im->ram + 63232, p, len); break;
        case B_CHEATTUN: memcpy(im->ram + 63746, p, len); break;
        case B_TUNEBIN:  memcpy(im->ram + 64000, p, len); break;
        case B_TITLETUN: memcpy(im->ram + 16384, p, len); break;
        }
        break;
    }
    return 0;
}

int jsw128_load(const uint8_t *data, long len, jsw128_image_t *img)
{
    memset(img, 0, sizeof *img);
    load_state st;
    memset(&st, 0, sizeof st);
    st.img = img;
    if (tape_blocks(data, len, on_block, &st) < 0) return -1;
    if (st.found != (1u << B_COUNT) - 1) return -1;
    /* The main block must be the JSW engine (BEGIN at 33792). */
    static const uint8_t begin[] = { 0xF3, 0x21, 0xFF, 0x5B, 0x36, 0x86, 0x2B, 0x36, 0x9F };
    if (memcmp(img->ram + 33792, begin, sizeof begin)) return -1;
    /* USR 32039: bank 7 gets 25000.. (6698 bytes) then 16384.. (2774). */
    memcpy(img->banks[7] + (0xDB00 - 0xC000), img->ram + 25000, 6698);
    memcpy(img->banks[7] + (0xDB00 - 0xC000) + 6698, img->ram + 16384, 2774);
    /* Bank 0 is what sits at 0xC000 when the game starts. */
    memcpy(img->banks[0], img->ram + 0xC000, 0x4000);
    img->ram[23388] = 0x10;                           /* BANKM: bank 0, ROM 1 */
    return 0;
}

void jsw128_prepare(const jsw128_image_t *img)
{
    zx_reset();
    zx_set_model(ZX_MODEL_128K);
    memcpy(mem + 0x4000, img->ram + 0x4000, 0xC000);
    for (int b = 0; b < 8; b++) memcpy(zx_banks[b], img->banks[b], 0x4000);
    zx_bank_c000 = 0;
    zx_last_7ffd = 0x10;
    zx_im = 1;
}
