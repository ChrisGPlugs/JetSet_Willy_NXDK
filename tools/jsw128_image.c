/*
 * jsw128_image.c - write the 64K CPU view the JSW128 HL9 tape loader leaves
 * (bank 0 at 0xC000), the input tools/z80c/z80c.py translates.
 *
 *   jsw128_image <tape> <out.bin>
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include "../src/jsw128.h"

static jsw128_image_t img;
static unsigned char buf[1 << 20];

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: jsw128_image <tape> <out.bin>\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    long n = (long)fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (jsw128_load(buf, n, &img)) { fprintf(stderr, "%s: not JSW128 HL9\n", argv[1]); return 1; }
    f = fopen(argv[2], "wb");
    fwrite(img.ram, 1, sizeof img.ram, f);
    fclose(f);
    return 0;
}
