/*
 * jsw.h - Jet Set Willy (48K) on top of the translated Z80 code.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef JSW_H
#define JSW_H

#include <stdint.h>

/* Addresses in the original memory map (SkoolKit disassembly labels). */
#define JSW_ORIGIN      0x8000      /* the tape's code block loads at 32768 */
#define JSW_IMAGE_SIZE  0x8000
#define JSW_BEGIN       33792       /* BEGIN */
#define JSW_ROOM        33824       /* current room number */
#define JSW_LIVES       34252
#define JSW_WILLY_Y     34255
#define JSW_ITEMS       34270       /* items collected (2's complement count) */
#define JSW_MODE        34271       /* game mode (0 normal, 1 Maria, 2 to bed, 3 toilet) */
#define JSW_TELEPORT    34275       /* WRITETYPER key counter (10 = cheat on) */
#define JSW_MSG_CURTIME 34175       /* " 7:00a" clock text */
#define JSW_ENTERCODES  34463
#define JSW_READCODE    34620
#define JSW_TITLESCREEN 34762
#define JSW_STARTGAME   35068
#define JSW_MAINLOOP    35245
#define JSW_TELECHECK   35696       /* teleport key check at the end of the main loop */
#define JSW_GAMEOVER    35914

/* Load the game from a TZX or TAP image of the original tape, or from a raw
 * 32768-byte dump of 32768-65535. Returns 0 and fills image[32768] on
 * success; -1 if the data isn't a Jet Set Willy tape. */
int jsw_load(const uint8_t *data, long len, uint8_t *image);

/* Options for jsw_prepare(). */
#define JSW_OPT_FIXES     1   /* the four official Software Projects bug-fix POKEs */
#define JSW_OPT_CODECARD  2   /* keep the colour-code card check at start-up */

/* Reset the machine and put the game image in mem[] with the chosen options. */
void jsw_prepare(const uint8_t *image, int options);
/* Run the game (from BEGIN). Returns only if the game resets the machine
 * (JP 0 after a second wrong code-card entry). */
void jsw_run(void);

/* Hook called at the addresses below (from the translated code). */
extern void (*jsw_hook)(uint16_t addr);
/* Set from TITLESCREEN until STARTGAME (title screen and tune). */
extern int jsw_in_title;

#endif
