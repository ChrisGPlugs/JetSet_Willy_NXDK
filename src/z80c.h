/*
 * z80c.h - interface of the translated game code (src/jsw_code.c, generated
 * by tools/z80c/z80c.py from the SkoolKit disassembly).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef Z80C_H
#define Z80C_H

#include <stdint.h>

/* Run the translated code from address pc. The game never returns of its
 * own accord; the host gets control every emulated frame via zx_frame_hook
 * and at the hook addresses via z80c_hook. z80c_run returns only if the code
 * jumps somewhere that isn't translated game code (JP 0 after a wrong
 * code-card entry resets the machine); z80c_bad_jump is told first. */
void z80c_run(uint16_t pc);
/* The Jet Set Willy 128 engine (src/jsw128_code.c), the same way. */
void jsw128_z80_run(uint16_t pc);

/* Called at each hook address chosen at generation time (--hook). */
extern void (*z80c_hook)(uint16_t addr);
/* Called with the target address of a jump outside the translated code. */
void z80c_bad_jump(uint16_t addr);
/* Z80C_VERIFY builds: the opcode in RAM at addr isn't the translated one. */
void z80c_bad_opcode(uint16_t addr);

/* Register snapshot taken just before each hook call. */
typedef struct { uint16_t af, bc, de, hl, ix, iy, sp; } z80c_regs_t;
extern z80c_regs_t z80c_regs;
/* Set by z80c_bad_jump / z80c_bad_opcode. */
extern uint16_t z80c_bad_addr;
extern int      z80c_bad_kind;

#endif
