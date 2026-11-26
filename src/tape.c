/*
 * tape.c - walk the data blocks of a TZX or TAP tape image.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "tape.h"

static int tzx_blocks(const uint8_t *d, long len, tape_block_fn fn, void *u)
{
    long i = 10;
    while (i < len) {
        uint8_t id = d[i++];
        long n;
        switch (id) {
        case 0x10:
            if (i + 4 > len) return -1;
            n = d[i + 2] | d[i + 3] << 8;
            if (i + 4 + n > len) return -1;
            if (fn(d + i + 4, n, u)) return 1;
            i += 4 + n;
            break;
        case 0x11:
            if (i + 18 > len) return -1;
            n = d[i + 15] | d[i + 16] << 8 | (long)d[i + 17] << 16;
            if (i + 18 + n > len) return -1;
            if (fn(d + i + 18, n, u)) return 1;
            i += 18 + n;
            break;
        case 0x14: if (i + 10 > len) return -1; i += 10 + (d[i + 7] | d[i + 8] << 8 | (long)d[i + 9] << 16); break;
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
        case 0x35: if (i + 20 > len) return -1;
                   i += 20 + (d[i + 16] | d[i + 17] << 8 | (long)d[i + 18] << 16 | (long)d[i + 19] << 24); break;
        case 0x5A: i += 9; break;
        default:   return -1;
        }
    }
    return 0;
}

static int tap_blocks(const uint8_t *d, long len, tape_block_fn fn, void *u)
{
    long i = 0;
    while (i + 2 <= len) {
        long n = d[i] | d[i + 1] << 8;
        if (i + 2 + n > len) return -1;
        if (fn(d + i + 2, n, u)) return 1;
        i += 2 + n;
    }
    return 0;
}

int tape_is_tzx(const uint8_t *d, long len)
{
    return len >= 10 && !memcmp(d, "ZXTape!\x1A", 8);
}

int tape_blocks(const uint8_t *d, long len, tape_block_fn fn, void *u)
{
    return tape_is_tzx(d, len) ? tzx_blocks(d, len, fn, u) : tap_blocks(d, len, fn, u);
}

int tape_block_ok(const uint8_t *blk, long n)
{
    if (n < 2) return 0;
    uint8_t x = 0;
    for (long i = 0; i < n; i++) x ^= blk[i];
    return x == 0;
}
