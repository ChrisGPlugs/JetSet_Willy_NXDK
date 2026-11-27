/*
 * jsw128.h - Jet Set Willy 128 (John Elliott, v0.04 Hacklevel 9).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef JSW128_H
#define JSW128_H

#include <stdint.h>

/* The machine as the tape loader leaves it. */
typedef struct {
    uint8_t ram[65536];        /* CPU view with bank 0 at 0xC000 (0x0000-0x3FFF unused) */
    uint8_t banks[8][16384];   /* 0..7 (5 and 2 unused: they are ram[0x4000..0xBFFF]) */
} jsw128_image_t;

/* Build the image from the JSW128 HL9 tape (TZX or TAP). 0 on success. */
int  jsw128_load(const uint8_t *tape, long len, jsw128_image_t *img);
/* Reset the 128K machine and install the image (bank 0 paged, IM 1). */
void jsw128_prepare(const jsw128_image_t *img);

#define JSW128_BEGIN    33792
#define JSW128_MAINLOOP 35245

#endif
