#!/usr/bin/env python3
"""
z80c.py - static Z80 -> C translator for the Jet Set Willy port.

Reads a SkoolKit control/skool file to learn which addresses hold code (and
the labels and comments for them), decodes the real bytes of the game image
at those addresses, and writes one C function in which every Z80
instruction becomes a few lines of C:

  * registers are C locals, flags are computed exactly;
  * the Z80 stack lives in mem[] at SP, so CALL/RET/PUSH/POP and the
    game's stack tricks (BEGIN's RET into a pushed address, KILLWILLY's
    POPs of return addresses) behave exactly as on the Spectrum;
  * every instruction charges its T-states machine cycle by machine cycle,
    with ULA contention applied at each memory or I/O access (zx_contend),
    so the game runs at the speed of a real 48K Spectrum;
  * immediate operands are read from mem[] at run time, so the game's
    self-modifying operands (36154/36166) work;
  * JP/JR/CALL targets are static gotos; RET and JP (HL) dispatch through a
    switch over all code addresses.

Usage: z80c.py <jsw.skool> <image.bin> <origin> <out.c> [--variant ADDR:BYTE ...]

A --variant names an alternative opcode byte at ADDR (e.g. a POKE): the
generated code tests mem[ADDR] at run time and runs whichever decoding is
present, so the front end can apply or remove such POKEs.

SPDX-License-Identifier: MIT (the generated output: CC BY-SA 3.0, see its header)
"""
import re
import sys

R8 = ['B', 'C', 'D', 'E', 'H', 'L', None, 'A']
R16 = ['BC', 'DE', 'HL', 'SP']
R16AF = ['BC', 'DE', 'HL', 'AF']
CC = ['!(F&FZ)', '(F&FZ)', '!(F&FC)', '(F&FC)', '!(F&FP)', '(F&FP)', '!(F&FS)', '(F&FS)']
CCN = ['NZ', 'Z', 'NC', 'C', 'PO', 'PE', 'P', 'M']
ALU = ['add8', 'adc8', 'sub8', 'sbc8', 'and8', 'xor8', 'or8', 'cp8']
ALUN = ['ADD A,', 'ADC A,', 'SUB ', 'SBC A,', 'AND ', 'XOR ', 'OR ', 'CP ']
ROT = ['rlc', 'rrc', 'rl', 'rr', 'sla', 'sra', 'sll', 'srl']


def parse_skool(path):
    code = {}        # addr -> comment
    labels = {}
    entries = set()
    blk = None
    pending_label = None
    for ln in open(path, encoding='utf-8'):
        ln = ln.rstrip('\n')
        if ln.startswith('@label='):
            pending_label = ln[7:].strip()
            continue
        m = re.match(r'^([cbgtuwsi* ])(\d{5}) (.*)', ln)
        if not m:
            continue
        t, a, rest = m.groups()
        a = int(a)
        if t not in ' *':
            blk = t
        if pending_label:
            labels[a] = pending_label
            pending_label = None
        if blk == 'c':
            ins, _, com = rest.partition(';')
            code[a] = (ins.strip(), com.strip())
            if t in 'c*':
                entries.add(a)
    return code, labels, entries


class Dec:
    """Decodes one instruction at addr; returns (length, text, c_lines, kind, targets)."""

    def __init__(self, img, org):
        self.img, self.org = img, org

    def b(self, a):
        return self.img[(a - self.org) & 0xFFFF] if 0 <= a - self.org < len(self.img) else 0

    def w(self, a):
        return self.b(a) | (self.b(a + 1) << 8)

    def decode(self, pc, opbyte=None):
        op = self.b(pc) if opbyte is None else opbyte
        n = f'mem[(uint16_t)0x{(pc+1)&0xFFFF:04X}]'        # runtime immediate (SMC-safe)
        nn = f'RD16IMM(0x{(pc+1)&0xFFFF:04X})'
        nxt = pc + 1
        L = []
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        F4 = f'FETCH(0x{pc:04X});'
        if op in (0xDD, 0xFD):
            return self.decode_index(pc, 'IX' if op == 0xDD else 'IY')
        if op == 0xCB:
            return self.decode_cb(pc)
        if op == 0xED:
            return self.decode_ed(pc)
        if x == 1:
            if op == 0x76:
                return (1, 'HALT', [F4, f'HALT_(0x{(pc+1)&0xFFFF:04X});'], 'ret', [])
            if z == 6:
                return (1, f'LD {R8[y]},(HL)', [F4, f'{R8[y]} = RD(HL);'], 'next', [])
            if y == 6:
                return (1, f'LD (HL),{R8[z]}', [F4, f'WR(HL, {R8[z]});'], 'next', [])
            return (1, f'LD {R8[y]},{R8[z]}', [F4, f'{R8[y]} = {R8[z]};'], 'next', [])
        if x == 2:
            if z == 6:
                return (1, f'{ALUN[y]}(HL)', [F4, f'{ALU[y]}(RD(HL));'], 'next', [])
            return (1, f'{ALUN[y]}{R8[z]}', [F4, f'{ALU[y]}({R8[z]});'], 'next', [])
        if x == 0:
            if z == 0:
                if y == 0:
                    return (1, 'NOP', [F4], 'next', [])
                if y == 1:
                    return (1, "EX AF,AF'", [F4, 'EXAF();'], 'next', [])
                d = self.b(pc + 1)
                tgt = (pc + 2 + (d - 256 if d > 127 else d)) & 0xFFFF
                if y == 2:
                    return (2, f'DJNZ {tgt}', [F4, 'T(1); IMM(0x%04X);' % (pc + 1),
                            f'if (--B) {{ JRTAKEN(0x{pc+1:04X}); goto L{tgt:04X}; }}'], 'cond', [tgt])
                if y == 3:
                    return (2, f'JR {tgt}', [F4, 'IMM(0x%04X); JRTAKEN(0x%04X);' % (pc + 1, pc + 1),
                            f'goto L{tgt:04X};'], 'jump', [tgt])
                c = CC[y - 4]
                return (2, f'JR {CCN[y-4]},{tgt}', [F4, 'IMM(0x%04X);' % (pc + 1),
                        f'if ({c}) {{ JRTAKEN(0x{pc+1:04X}); goto L{tgt:04X}; }}'], 'cond', [tgt])
            if z == 1:
                rp = R16[y >> 1]
                if not (y & 1):
                    return (3, f'LD {rp},{self.w(pc+1)}', [F4, f'IMM(0x{pc+1:04X}); IMM(0x{pc+2:04X}); {rp} = {nn};'], 'next', [])
                return (1, f'ADD HL,{rp}', [F4, 'IR(7);', f'HL = add16(HL, {rp});'], 'next', [])
            if z == 2:
                a16 = self.w(pc + 1)
                if y == 0: return (1, 'LD (BC),A', [F4, 'WR(BC, A);'], 'next', [])
                if y == 1: return (1, 'LD A,(BC)', [F4, 'A = RD(BC);'], 'next', [])
                if y == 2: return (1, 'LD (DE),A', [F4, 'WR(DE, A);'], 'next', [])
                if y == 3: return (1, 'LD A,(DE)', [F4, 'A = RD(DE);'], 'next', [])
                ld = f'IMM(0x{pc+1:04X}); IMM(0x{pc+2:04X}); uint16_t ea = {nn};'
                if y == 4: return (3, f'LD ({a16}),HL', [F4, '{ ' + ld, 'WR(ea, L); WR((uint16_t)(ea + 1), H); }'], 'next', [])
                if y == 5: return (3, f'LD HL,({a16})', [F4, '{ ' + ld, 'L = RD(ea); H = RD((uint16_t)(ea + 1)); }'], 'next', [])
                if y == 6: return (3, f'LD ({a16}),A', [F4, '{ ' + ld, 'WR(ea, A); }'], 'next', [])
                return (3, f'LD A,({a16})', [F4, '{ ' + ld, 'A = RD(ea); }'], 'next', [])
            if z == 3:
                rp = R16[y >> 1]
                return (1, ('INC ' if not (y & 1) else 'DEC ') + rp,
                        [F4, 'IR(2);', f'{rp} = (uint16_t)({rp} {"+" if not (y & 1) else "-"} 1);'], 'next', [])
            if z in (4, 5):
                f = 'inc8' if z == 4 else 'dec8'
                if y == 6:
                    return (1, f'{f[:3].upper()} (HL)', [F4, f'{{ uint8_t v = RD(HL); IC(HL, 1); WR(HL, {f}(v)); }}'], 'next', [])
                return (1, f'{f[:3].upper()} {R8[y]}', [F4, f'{R8[y]} = {f}({R8[y]});'], 'next', [])
            if z == 6:
                if y == 6:
                    return (2, f'LD (HL),{self.b(pc+1)}', [F4, f'IMM(0x{pc+1:04X}); WR(HL, {n});'], 'next', [])
                return (2, f'LD {R8[y]},{self.b(pc+1)}', [F4, f'IMM(0x{pc+1:04X}); {R8[y]} = {n};'], 'next', [])
            if z == 7:
                ops = ['rlca();', 'rrca();', 'rla();', 'rra();', 'daa();', 'cpl();', 'scf();', 'ccf();']
                return (1, ['RLCA', 'RRCA', 'RLA', 'RRA', 'DAA', 'CPL', 'SCF', 'CCF'][y], [F4, ops[y]], 'next', [])
        # x == 3
        if z == 0:
            return (1, f'RET {CCN[y]}', [F4, 'IR(1);', f'if ({CC[y]}) {{ POPPC(); goto dispatch; }}'], 'cond', [])
        if z == 1:
            if not (y & 1):
                rp = R16AF[y >> 1]
                return (1, f'POP {rp}', [F4, f'{{ uint8_t lo = RD(SP); SP++; uint8_t hi = RD(SP); SP++; {rp} = (uint16_t)(lo | (hi << 8)); }}'], 'next', [])
            if y == 1: return (1, 'RET', [F4, 'POPPC(); goto dispatch;'], 'ret', [])
            if y == 3: return (1, 'EXX', [F4, 'EXX_();'], 'next', [])
            if y == 5: return (1, 'JP (HL)', [F4, 'pc = HL; goto dispatch;'], 'ret', [])
            return (1, 'LD SP,HL', [F4, 'IR(2); SP = HL;'], 'next', [])
        if z == 2:
            tgt = self.w(pc + 1)
            return (3, f'JP {CCN[y]},{tgt}', [F4, f'IMM(0x{pc+1:04X}); IMM(0x{pc+2:04X});',
                    f'if ({CC[y]}) goto L{tgt:04X};'], 'cond', [tgt])
        if z == 3:
            if y == 0:
                tgt = self.w(pc + 1)
                return (3, f'JP {tgt}', [F4, f'IMM(0x{pc+1:04X}); IMM(0x{pc+2:04X});', f'goto L{tgt:04X};'], 'jump', [tgt])
            if y == 2:
                return (2, f'OUT ({self.b(pc+1)}),A', [F4, f'IMM(0x{pc+1:04X}); OUTP((uint16_t)(({n}) | (A << 8)), A);'], 'next', [])
            if y == 3:
                return (2, f'IN A,({self.b(pc+1)})', [F4, f'IMM(0x{pc+1:04X}); A = INP((uint16_t)(({n}) | (A << 8)));'], 'next', [])
            if y == 4:
                return (1, 'EX (SP),HL', [F4, '{ uint8_t lo = RD(SP), hi = RD((uint16_t)(SP + 1)); IC((uint16_t)(SP + 1), 1);',
                        'WR((uint16_t)(SP + 1), H); WR(SP, L); IC(SP, 2); HL = (uint16_t)(lo | (hi << 8)); }'], 'next', [])
            if y == 5: return (1, 'EX DE,HL', [F4, '{ uint16_t t = DE; DE = HL; HL = t; }'], 'next', [])
            if y == 6: return (1, 'DI', [F4, 'zx_iff = 0;'], 'next', [])
            return (1, 'EI', [F4, 'zx_iff = 1; ei_delay = 1;'], 'next', [])
        if z == 4:
            tgt = self.w(pc + 1)
            return (3, f'CALL {CCN[y]},{tgt}', [F4, f'IMM(0x{pc+1:04X}); IMM(0x{pc+2:04X});',
                    f'if ({CC[y]}) {{ IC(0x{pc+2:04X}, 1); PUSHW(0x{pc+3:04X}); goto L{tgt:04X}; }}'], 'cond', [tgt])
        if z == 5:
            if not (y & 1):
                rp = R16AF[y >> 1]
                return (1, f'PUSH {rp}', [F4, 'IR(1);', f'PUSHW({rp});'], 'next', [])
            if y == 1:
                tgt = self.w(pc + 1)
                return (3, f'CALL {tgt}', [F4, f'IMM(0x{pc+1:04X}); IMM(0x{pc+2:04X}); IC(0x{pc+2:04X}, 1);',
                        f'PUSHW(0x{pc+3:04X}); goto L{tgt:04X};'], 'call', [tgt])
        if z == 6:
            return (2, f'{ALUN[y]}{self.b(pc+1)}', [F4, f'IMM(0x{pc+1:04X}); {ALU[y]}({n});'], 'next', [])
        if z == 7:
            return (1, f'RST {y*8}', [F4, 'IR(1);', f'PUSHW(0x{pc+1:04X}); pc = 0x{y*8:04X}; goto dispatch;'], 'ret', [])
        raise ValueError(f'unhandled opcode {op:02X} at {pc}')

    def decode_cb(self, pc):
        op = self.b(pc + 1)
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        F = f'FETCH(0x{pc:04X}); FETCH(0x{pc+1:04X});'
        if z == 6:
            if x == 1:
                return (2, f'BIT {y},(HL)', [F, f'{{ uint8_t v = RD(HL); IC(HL, 1); bit({y}, v); }}'], 'next', [])
            if x == 0:
                e = f'{ROT[y]}(v)'
                t = ROT[y].upper()
                return (2, f'{t} (HL)', [F, f'{{ uint8_t v = RD(HL); IC(HL, 1); WR(HL, {e}); }}'], 'next', [])
            e = f'(uint8_t)(v & ~(1u << {y}))' if x == 2 else f'(uint8_t)(v | (1u << {y}))'
            return (2, f'{"RES" if x == 2 else "SET"} {y},(HL)', [F, f'{{ uint8_t v = RD(HL); IC(HL, 1); WR(HL, {e}); }}'], 'next', [])
        r = R8[z]
        if x == 0: return (2, f'{ROT[y].upper()} {r}', [F, f'{r} = {ROT[y]}({r});'], 'next', [])
        if x == 1: return (2, f'BIT {y},{r}', [F, f'bit({y}, {r});'], 'next', [])
        if x == 2: return (2, f'RES {y},{r}', [F, f'{r} = (uint8_t)({r} & ~(1u << {y}));'], 'next', [])
        return (2, f'SET {y},{r}', [F, f'{r} = (uint8_t)({r} | (1u << {y}));'], 'next', [])

    def decode_ed(self, pc):
        op = self.b(pc + 1)
        F = f'FETCH(0x{pc:04X}); FETCH(0x{pc+1:04X});'
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        if x == 1:
            if z == 0:
                tgt = 'F_' if y == 6 else R8[y]
                return (2, f'IN {R8[y] or "F"},(C)', [F, f'{{ uint8_t v = INP(BC); in_flags(v);' + ('' if y == 6 else f' {tgt} = v;') + ' }'], 'next', [])
            if z == 1:
                return (2, f'OUT (C),{R8[y] or 0}', [F, f'OUTP(BC, {R8[y] or 0});'], 'next', [])
            if z == 2:
                rp = R16[y >> 1]
                return (2, ('SBC' if not (y & 1) else 'ADC') + f' HL,{rp}', [F, 'IR(7);', ('sbc16' if not (y & 1) else 'adc16') + f'({rp});'], 'next', [])
            if z == 3:
                a16 = self.w(pc + 2)
                rp = R16[y >> 1]
                ld = f'IMM(0x{pc+2:04X}); IMM(0x{pc+3:04X}); uint16_t ea = RD16IMM(0x{pc+2:04X});'
                if not (y & 1):
                    return (4, f'LD ({a16}),{rp}', [F, '{ ' + ld, f'WR(ea, (uint8_t){rp}); WR((uint16_t)(ea + 1), (uint8_t)({rp} >> 8)); }}'], 'next', [])
                return (4, f'LD {rp},({a16})', [F, '{ ' + ld, f'uint8_t lo = RD(ea), hi = RD((uint16_t)(ea + 1)); {rp} = (uint16_t)(lo | (hi << 8)); }}'], 'next', [])
            if z == 4:
                return (2, 'NEG', [F, 'neg();'], 'next', [])
            if z == 6:
                return (2, f'IM {[0,0,1,2,0,0,1,2][y]}', [F, f'zx_im = {[0,0,1,2,0,0,1,2][y]};'], 'next', [])
            if z == 7 and y == 0:
                return (2, 'LD I,A', [F, 'IR(1); I_ = A;'], 'next', [])
            if z == 7 and y == 2:
                return (2, 'LD A,I', [F, 'IR(1); A = I_; F = (uint8_t)((F & FC) | sz53_[A] | (zx_iff ? FP : 0));'], 'next', [])
            if z == 5:
                return (2, 'RETI' if y == 1 else 'RETN', [F, 'POPPC(); goto dispatch;'], 'ret', [])
        if x == 2 and z <= 1 and y >= 4:
            name = {(4, 0): 'LDI', (5, 0): 'LDD', (6, 0): 'LDIR', (7, 0): 'LDDR',
                    (4, 1): 'CPI', (5, 1): 'CPD', (6, 1): 'CPIR', (7, 1): 'CPDR'}[(y, z)]
            fn = name.lower()
            if name in ('LDIR', 'LDDR', 'CPIR', 'CPDR'):
                d = '+1' if name[2] == 'I' else '-1'
                return (2, name, [F, f'if ({fn[:3]}_rep({d})) goto L{pc:04X};'], 'cond', [pc])
            d = '+1' if name[2] == 'I' else '-1'
            return (2, name, [F, f'{fn[:3]}_step({d});'], 'next', [])
        raise ValueError(f'unhandled ED {op:02X} at {pc}')

    def decode_index(self, pc, ir):
        op = self.b(pc + 1)
        IRH, IRL = ir + 'H', ir + 'L'
        F = f'FETCH(0x{pc:04X}); FETCH(0x{pc+1:04X});'
        d = self.b(pc + 2)
        ds = d - 256 if d > 127 else d
        ea = f'uint16_t ea = (uint16_t)({ir} + (int8_t)mem[0x{(pc+2)&0xFFFF:04X}]);'
        dtxt = f'{ir}{"+" if ds >= 0 else "-"}{abs(ds)}'
        disp = f'IMM(0x{pc+2:04X}); IC(0x{pc+2:04X}, 5);'
        if op == 0xCB:
            op2 = self.b(pc + 3)
            x, y, z = op2 >> 6, (op2 >> 3) & 7, op2 & 7
            pre = f'{F} IMM(0x{pc+2:04X}); IMM(0x{pc+3:04X}); IC(0x{pc+3:04X}, 2); {{ {ea} uint8_t v = RD(ea);'
            if x == 1:
                return (4, f'BIT {y},({dtxt})', [pre, f'IC(ea, 1); bit({y}, v); }}'], 'next', [])
            if x == 0:
                e = f'{ROT[y]}(v)'
                nm = f'{ROT[y].upper()} ({dtxt})'
            elif x == 2:
                e = f'(uint8_t)(v & ~(1u << {y}))'; nm = f'RES {y},({dtxt})'
            else:
                e = f'(uint8_t)(v | (1u << {y}))'; nm = f'SET {y},({dtxt})'
            if z != 6:
                raise ValueError('undocumented DDCB at %d' % pc)
            return (4, nm, [pre, f'IC(ea, 1); WR(ea, {e}); }}'], 'next', [])
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        if op == 0x21:
            return (4, f'LD {ir},{self.w(pc+2)}', [F, f'IMM(0x{pc+2:04X}); IMM(0x{pc+3:04X}); {ir} = RD16IMM(0x{pc+2:04X});'], 'next', [])
        if op == 0x23:
            return (2, f'INC {ir}', [F, f'IR(2); {ir} = (uint16_t)({ir} + 1);'], 'next', [])
        if op == 0x2B:
            return (2, f'DEC {ir}', [F, f'IR(2); {ir} = (uint16_t)({ir} - 1);'], 'next', [])
        if op in (0x09, 0x19, 0x29, 0x39):
            rp = ['BC', 'DE', ir, 'SP'][op >> 4]
            return (2, f'ADD {ir},{rp}', [F, f'IR(7); {ir} = add16({ir}, {rp});'], 'next', [])
        if op == 0xE5:
            return (2, f'PUSH {ir}', [F, f'IR(1); PUSHW({ir});'], 'next', [])
        if op == 0xE1:
            return (2, f'POP {ir}', [F, f'{{ uint8_t lo = RD(SP); SP++; uint8_t hi = RD(SP); SP++; {ir} = (uint16_t)(lo | (hi << 8)); }}'], 'next', [])
        if op == 0xE9:
            return (2, f'JP ({ir})', [F, f'pc = {ir}; goto dispatch;'], 'ret', [])
        if op == 0x36:
            return (4, f'LD ({dtxt}),{self.b(pc+3)}', [F, f'IMM(0x{pc+2:04X}); IMM(0x{pc+3:04X}); IC(0x{pc+3:04X}, 2); {{ {ea} WR(ea, mem[0x{pc+3:04X}]); }}'], 'next', [])
        if op in (0x34, 0x35):
            f = 'inc8' if op == 0x34 else 'dec8'
            return (3, f'{f[:3].upper()} ({dtxt})', [F, f'{disp} {{ {ea} uint8_t v = RD(ea); IC(ea, 1); WR(ea, {f}(v)); }}'], 'next', [])
        if x == 1 and (z == 6) != (y == 6):
            if z == 6:
                return (3, f'LD {R8[y]},({dtxt})', [F, f'{disp} {{ {ea} {R8[y]} = RD(ea); }}'], 'next', [])
            return (3, f'LD ({dtxt}),{R8[z]}', [F, f'{disp} {{ {ea} WR(ea, {R8[z]}); }}'], 'next', [])
        if x == 2 and z == 6:
            return (3, f'{ALUN[y]}({dtxt})', [F, f'{disp} {{ {ea} {ALU[y]}(RD(ea)); }}'], 'next', [])
        # IXH/IXL forms
        def rr(i):
            return {4: IRH, 5: IRL}.get(i, R8[i])
        if x == 1 and (y in (4, 5) or z in (4, 5)) and y != 6 and z != 6:
            return (2, f'LD {rr(y)},{rr(z)}', [F, f'{rr(y)} = {rr(z)};'], 'next', [])
        if x == 2 and z in (4, 5):
            return (2, f'{ALUN[y]}{rr(z)}', [F, f'{ALU[y]}({rr(z)});'], 'next', [])
        if x == 0 and z == 6 and y in (4, 5):
            return (3, f'LD {rr(y)},{self.b(pc+2)}', [F, f'IMM(0x{pc+2:04X}); {rr(y)} = mem[0x{pc+2:04X}];'], 'next', [])
        if x == 0 and z in (4, 5) and y in (4, 5):
            f = 'inc8' if z == 4 else 'dec8'
            return (2, f'{f[:3].upper()} {rr(y)}', [F, f'{rr(y)} = {f}({rr(y)});'], 'next', [])
        raise ValueError(f'unhandled {ir} {op:02X} at {pc}')


def main():
    args = sys.argv[1:]
    variants = {}
    hooks = []
    while '--variant' in args:
        i = args.index('--variant')
        a, v = args[i + 1].split(':')
        variants.setdefault(int(a), []).append(int(v))
        del args[i:i + 2]
    while '--hook' in args:
        i = args.index('--hook')
        hooks.append(int(args[i + 1]))
        del args[i:i + 2]
    def opt(name, default=None):
        if name in args:
            i = args.index(name)
            v = args[i + 1]
            del args[i:i + 2]
            return v
        return default
    func = opt('--func', 'z80c_run')
    mapfile = opt('--map')
    skool48 = opt('--skool48')        # with --map: comments from the 48K disassembly
    image48 = opt('--image48')        # ... where the bytes match this 48K image
    interrupts = '--interrupts' in args
    if interrupts: args.remove('--interrupts')
    jumptables = []
    while '--jumptable' in args:              # ADDR:COUNT table of code addresses (seeds)
        i = args.index('--jumptable')
        a, c = args[i + 1].split(':')
        jumptables.append((int(a), int(c)))
        del args[i:i + 2]
    descend = '--descend' in args
    if descend: args.remove('--descend')
    skool, image, org, out = args[0], args[1], int(args[2]), args[3]
    img = open(image, 'rb').read()
    dec = Dec(img, org)
    if mapfile:
        code, labels, entries = {}, {}, set()
        for ln in open(mapfile):
            ln = ln.strip()
            if not ln:
                continue
            bank, _, a = ln.partition(':')
            code[int(a)] = ('', '')
            entries.add(int(a))
        traced = set(code)
        for ta, tc in jumptables:
            for k in range(tc):
                t = dec.w(ta + 2 * k)
                if t not in code:
                    code[t] = ('', '')
        if descend and skool48 and image48:
            # Seed the descent with the 48K game's code wherever its bytes are
            # unchanged: every path of the original engine, run or not.
            c48s, _, _ = parse_skool(skool48)
            i48s = open(image48, 'rb').read()
            seeded = 0
            for a in c48s:
                if not (32768 <= a < 65536) or a in code:
                    continue
                try:
                    n1 = dec.decode(a)[0]
                except Exception:
                    continue
                if img[a - org:a - org + n1] == i48s[a - 32768:a - 32768 + n1]:
                    code[a] = ('', '')
                    seeded += 1
            print(f'seeded {seeded} unchanged 48K code addresses', file=sys.stderr)
        if descend:
            work = list(code)
            while work:
                a = work.pop()
                try:
                    n, text, lines, kind, tg = dec.decode(a)
                except Exception:
                    continue
                nxt = [] if kind in ('jump', 'ret') else [a + n]
                for t in nxt + tg:
                    if 0x4000 <= t < 0x10000 and t not in code:
                        try:
                            dec.decode(t)
                        except Exception:
                            continue
                        code[t] = ('', '')
                        work.append(t)
            print(f'descent added {len(code) - len(traced)} addresses', file=sys.stderr)
        if skool48 and image48:
            c48, l48, _ = parse_skool(skool48)
            i48 = open(image48, 'rb').read()
            d48 = Dec(i48, 32768)
            for a in list(code):
                if a in c48:
                    n1 = dec.decode(a)[0]
                    if img[a - org:a - org + n1] == i48[a - 32768:a - 32768 + n1]:
                        code[a] = ('', c48[a][1])
                        if a in l48:
                            labels[a] = l48[a]
                    else:
                        code[a] = ('', '(JSW128) ' )
                else:
                    code[a] = ('', '(JSW128)')
            for a in l48:
                if a in code and a not in labels and code[a][1] and not code[a][1].startswith('(JSW128)'):
                    labels[a] = l48[a]
    else:
        code, labels, entries = parse_skool(skool)
    for a in [a for a in code if not (org <= a < org + len(img))]:
        del code[a]
    entries &= set(code)
    addrs = sorted(code)
    decoded = {}
    targets = set(entries)
    for a in addrs:
        n, text, lines, kind, tg = dec.decode(a)
        decoded[a] = (n, text, lines, kind)
        targets.update(tg)
        if kind in ('call', 'cond', 'next'):
            targets.add(a + n)
        if a in variants:
            for vb in variants[a]:
                vn, vt, vl, vk, vtg = dec.decode(a, vb)
                if vn != n:
                    raise SystemExit(f'variant at {a} changes length')
                targets.update(vtg)
    # Check: decoded lengths agree with the skool's instruction addresses.
    errors = 0
    for i, a in enumerate(addrs[:-1]):
        n = decoded[a][0]
        if a + n != addrs[i + 1] and decoded[a][3] not in ('jump', 'ret'):
            print(f'warning: {a} {decoded[a][1]} len {n} -> next skool addr {addrs[i+1]}', file=sys.stderr)
            errors += 1
    for t in sorted(targets):
        if t not in code and t != 0:
            print(f'warning: target {t} is not a known code address', file=sys.stderr)
    # Compare our mnemonics with the skool's (numbers normalised).
    def norm(s):
        s = s.upper().replace(' ', '')
        return re.sub(r'\$[0-9A-F]+|\d+', '#', s)
    mism = 0
    for a in addrs:
        sk = code[a][0]
        if mapfile:
            break
        if norm(sk) != norm(decoded[a][1]):
            mism += 1
            print(f'mnemonic differs at {a}: skool "{sk}" vs decoded "{decoded[a][1]}"', file=sys.stderr)
    o = []
    o.append('/*')
    o.append(' * Jet Set Willy, translated from Z80 to C.')
    o.append(' * Generated by tools/z80c/z80c.py from the game image and the SkoolKit')
    o.append(' * disassembly; do not edit (regenerate with Makefile.gen).')
    o.append(' *')
    o.append(' * The labels and per-instruction comments are from "Jet Set Willy: a')
    o.append(' * disassembly", (C) Richard Dymond, https://github.com/skoolkid/jetsetwilly,')
    o.append(' * licensed under CC BY-SA 3.0 (https://creativecommons.org/licenses/by-sa/3.0/).')
    o.append(' * This generated file is therefore also licensed under CC BY-SA 3.0.')
    o.append(' * Jet Set Willy (C) 1984 Matthew Smith / Software Projects.')
    o.append(' */')
    if interrupts:
        o.append('#define Z80C_INTERRUPTS 1')
    o.append('#include "z80c_rt.h"')
    o.append('')
    o.append(f'void {func}(uint16_t entry)')
    o.append('{')
    o.append('    Z80C_LOCALS;')
    o.append('    pc = entry;')
    if interrupts:
        o.append('    I_ = 0x3F; ei_delay = 0;')
    o.append('    goto dispatch;')
    o.append('dispatch:')
    o.append('    switch (pc) {')
    for a in addrs:
        if a >= 0xC000 and mapfile:
            o.append(f'    case 0x{a:04X}: if (zx_bank_c000) break; goto L{a:04X};')
        else:
            o.append(f'    case 0x{a:04X}: goto L{a:04X};')
    o.append('    default: break;')
    o.append('    }')
    o.append('    z80c_bad_jump(pc);')
    o.append('    return;')
    for a in addrs:
        n, text, lines, kind = decoded[a]
        if a in labels:
            o.append(f'\n/* ---- {labels[a]} ---- */')
        com = code[a][1].replace('*/', '* /')
        o.append(f'L{a:04X}: /* {a} {text:<16} {com} */')
        if interrupts:
            o.append(f'    INTCHECK(0x{a:04X});')
        o.append(f'    VERIFY(0x{a:04X}, 0x{dec.b(a):02X});')
        if a in hooks:
            o.append(f'    Z80C_HOOK(0x{a:04X});')
        if a in variants:
            o.append(f'    switch (mem[0x{a:04X}]) {{')
            for vb in variants[a]:
                vn, vt, vl, vk, _ = dec.decode(a, vb)
                o.append(f'    case 0x{vb:02X}: /* variant: {vt} */')
                o.append('        ' + ' '.join(vl))
                if vk in ('next', 'cond', 'call'):
                    o.append(f'        goto L{a+vn:04X};')
            o.append(f'    default: break; /* original: {text} */')
            o.append('    }')
        for ln in lines:
            o.append('    ' + ln)
        if kind in ('next', 'cond', 'call') and (a + n) not in code:
            o.append(f'    pc = 0x{(a+n)&0xFFFF:04X}; goto dispatch;')
    o.append('}')
    # Jumps to addresses outside the translated code go through the dispatcher
    # (which reports them to the host).
    def fix(m):
        t = int(m.group(1), 16)
        return m.group(0) if t in code else f'{{ pc = 0x{t:04X}; goto dispatch; }}'
    o = [re.sub(r'goto L([0-9A-F]{4});', fix, ln) for ln in o]
    open(out, 'w').write('\n'.join(o) + '\n')
    print(f'{len(addrs)} instructions, {len(targets)} targets, {errors} length warnings, {mism} mnemonic mismatches', file=sys.stderr)


if __name__ == '__main__':
    main()
