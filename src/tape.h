/*
 * tape.h - walk the data blocks of a TZX or TAP tape image.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TAPE_H
#define TAPE_H

#include <stdint.h>

/* Called with each standard or turbo data block as stored on tape (flag
 * byte, payload, checksum). Return non-zero to stop. */
typedef int (*tape_block_fn)(const uint8_t *blk, long n, void *u);

/* Returns 1 if fn stopped the walk, 0 at the end of the tape, -1 if the
 * image is malformed. */
int tape_blocks(const uint8_t *d, long len, tape_block_fn fn, void *u);
int tape_is_tzx(const uint8_t *d, long len);
/* Is the block's XOR checksum right? */
int tape_block_ok(const uint8_t *blk, long n);

#endif
