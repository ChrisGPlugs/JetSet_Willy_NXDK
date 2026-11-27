/*
 * z80c_glue.c - globals shared by the translated engines (z80c.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include "z80c.h"

void (*z80c_hook)(uint16_t addr);
z80c_regs_t z80c_regs;
uint16_t z80c_bad_addr;
int      z80c_bad_kind;           /* 1 jump outside the code, 2 opcode mismatch */

void z80c_bad_jump(uint16_t addr)   { z80c_bad_addr = addr; z80c_bad_kind = 1; }
void z80c_bad_opcode(uint16_t addr) { z80c_bad_addr = addr; z80c_bad_kind = 2; }
