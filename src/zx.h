/*
 * zx.h - the bits of a 48K ZX Spectrum that the Jet Set Willy port needs.
 *
 * The game code (src/jsw_code.c) works directly on a 64K memory array laid out
 * exactly like the Spectrum's: ROM at 0x0000, display file at 0x4000,
 * attributes at 0x5800, and the game itself from 0x5C00 upwards. It talks to
 * the "hardware" through zx_in()/zx_out() and advances a Z80 T-state clock
 * with zx_tick(), so the original timing (and the beeper music) is preserved.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef ZX_H
#define ZX_H

#include <stdint.h>

/* Machine timing (set by zx_set_model). */
typedef enum { ZX_MODEL_48K, ZX_MODEL_128K } zx_model_t;
extern uint32_t zx_cpu_hz;               /* 3500000 (48K) / 3546900 (128K) */
extern uint32_t zx_t_per_frame;          /* 69888 (48K) / 70908 (128K) */
extern uint32_t zx_contend_start;        /* first contended T-state of a frame */
extern uint32_t zx_t_per_line;           /* 224 (48K) / 228 (128K) */
#define ZX_CPU_HZ      zx_cpu_hz
#define ZX_T_PER_FRAME zx_t_per_frame
void zx_set_model(zx_model_t model);

/* Full 64K address space, original memory map. */
extern uint8_t mem[65536];

/* T-state clock. */
extern uint64_t zx_t;
extern uint64_t zx_next_frame;
void zx_frame(void);                     /* called every ZX_T_PER_FRAME T-states */

/* Signed so the translation can express "JR not taken" style corrections. */
static inline void zx_tick(int32_t t)
{
    zx_t = (uint64_t)((int64_t)zx_t + t);
    if (zx_t >= zx_next_frame)
        zx_frame();
}

/* ULA memory contention. While the ULA fetches the screen (the first 128
 * T-states of each of the 192 display lines), a CPU access to
 * 0x4000-0x7FFF waits 6,5,4,3,2,1,0,0 T-states depending on where in the
 * 8-T fetch cycle it lands. Manic Miner keeps its screen and attribute
 * buffers in that region, so this slows the real game by about a quarter.
 * The comparison tests switch it off (zx_contention = 0) because the
 * reference Z80 core doesn't model it. */
extern int      zx_contention;
extern uint64_t zx_contended_t;          /* total delay added (diagnostics) */
static inline void zx_contend_now(void)
{
    uint32_t ft = (uint32_t)(zx_t - (zx_next_frame - zx_t_per_frame));
    if (ft < zx_contend_start) return;
    uint32_t d = ft - zx_contend_start;
    uint32_t line = d / zx_t_per_line;
    if (line >= 192) return;
    uint32_t x = d - line * zx_t_per_line;
    if (x >= 128) return;
    static const uint8_t pattern[8] = { 6, 5, 4, 3, 2, 1, 0, 0 };
    uint8_t delay = pattern[x & 7];
    if (delay) { zx_contended_t += delay; zx_tick(delay); }
}
/* Exact contention at the current T-state: used where the translation
 * advances the clock cycle by cycle (LDIR, port I/O). */
/* 128K paging: the bank at 0xC000 (zx_paging.c). Odd banks are contended
 * on the 128K, like 0x4000-0x7FFF. */
extern int zx_bank_c000;
static inline int zx_is_contended(uint16_t addr)
{
    return (addr & 0xC000) == 0x4000 || ((addr & 0xC000) == 0xC000 && (zx_bank_c000 & 1));
}
static inline void zx_contend(uint16_t addr)
{
    if (zx_contention && zx_is_contended(addr)) zx_contend_now();
}
/* Expected contention: the translated routines make their memory accesses
 * in bursts and charge the instruction T-states afterwards, so the exact
 * pattern would let every access after the first in a burst through for
 * free. Instead, an access made while the ULA is fetching the screen costs
 * the pattern's average, 21/8 T-states, carried as a fraction. */
extern uint32_t zx_contend_frac;
static inline void zx_contend_avg(uint16_t addr)
{
    if (!zx_contention || !zx_is_contended(addr)) return;
    uint32_t ft = (uint32_t)(zx_t - (zx_next_frame - zx_t_per_frame));
    if (ft < zx_contend_start) return;
    uint32_t d = ft - zx_contend_start;
    uint32_t line = d / zx_t_per_line;
    if (line >= 192 || d - line * zx_t_per_line >= 128) return;
    zx_contend_frac += 21;
    uint32_t delay = zx_contend_frac >> 3;
    zx_contend_frac &= 7;
    if (delay) { zx_contended_t += delay; zx_tick((int32_t)delay); }
}
/* A data access to memory, as made by the game code: contention, then the
 * byte itself. */
static inline uint8_t *zx_mem(uint16_t addr)
{
    zx_contend_avg(addr);
    return &mem[addr];
}
/* I/O to the ULA (even port): contended like a memory access. */
static inline void zx_contend_io(uint16_t port)
{
    if (zx_contention && (!(port & 1) || zx_is_contended(port))) zx_contend_now();
}

/* Port I/O, as seen by the game. */
uint8_t zx_in(uint16_t port);
/* Optional filter on every IN (used by the comparison test). */
extern uint8_t (*zx_in_filter)(uint16_t port, uint8_t value);
void    zx_out(uint16_t port, uint8_t value);

/* Keyboard matrix: 8 half-rows, bits 0-4, 0 = pressed (like the real ULA). */
extern uint8_t zx_keys[8];
/* Kempston joystick: bit0 right, 1 left, 2 down, 3 up, 4 fire (1 = active). */
extern uint8_t zx_kempston;
extern int     zx_kempston_present;

/* Half-row indices for zx_keys[] (address line A8..A15 that selects them). */
enum {
    ZX_ROW_SHIFT_V = 0,   /* 0xFEFE: SHIFT Z X C V   */
    ZX_ROW_A_G     = 1,   /* 0xFDFE: A S D F G       */
    ZX_ROW_Q_T     = 2,   /* 0xFBFE: Q W E R T       */
    ZX_ROW_1_5     = 3,   /* 0xF7FE: 1 2 3 4 5       */
    ZX_ROW_0_6     = 4,   /* 0xEFFE: 0 9 8 7 6       */
    ZX_ROW_P_Y     = 5,   /* 0xDFFE: P O I U Y       */
    ZX_ROW_ENTER_H = 6,   /* 0xBFFE: ENTER L K J H   */
    ZX_ROW_SPACE_B = 7    /* 0x7FFE: SPACE SYM M N B */
};
void zx_key(int row, int bit, int down);
void zx_keys_clear(void);

/* Border colour (last value written to port 0xFE, bits 0-2). */
extern uint8_t zx_border;

/* Beeper: speaker level changes recorded with T-state offsets within the
 * current frame. The host turns them into PCM in its frame hook. */
#define ZX_BEEPER_MAX 32768
typedef struct { uint32_t t; uint8_t level; } zx_beep_t;
extern zx_beep_t zx_beeps[ZX_BEEPER_MAX];
extern int       zx_beep_count;
extern uint8_t   zx_beep_level;          /* level at start of frame / now */

/* Render the display file + border into a 320x240 ARGB8888 buffer
 * (32-pixel border left/right, 24-pixel border top/bottom). flash_phase
 * selects the inverted half of the FLASH cycle. */
#define ZX_SCREEN_W 320
#define ZX_SCREEN_H 240
void zx_render(uint32_t *pixels, int pitch_pixels, int flash_phase);

/* Hook the host installs: called once per emulated frame (50 Hz), at the
 * end of the frame (beeper/AY events for the frame are complete). */
extern void (*zx_frame_hook)(void);

/* Maskable interrupt, raised at the start of every frame. The game code
 * keeps zx_iff/zx_im up to date at its DI/EI/IM instructions; the game
 * installs zx_int_hook to run its interrupt handler (Manic Miner 128's IM2
 * music player). The original Manic Miner runs with interrupts off. */
extern int  zx_iff, zx_im;
extern void (*zx_int_hook)(void);

/* AY-3-8912 (128K sound chip): register writes recorded with T-state
 * offsets within the current frame, plus the current register file. */
#define ZX_AY_MAX 4096
typedef struct { uint32_t t; uint8_t reg, val; } zx_ay_t;
extern zx_ay_t zx_ay_log[ZX_AY_MAX];
extern int     zx_ay_count;
extern uint8_t zx_ay_regs[16];
extern uint8_t zx_ay_sel;              /* selected AY register (0xFFFD) */
void zx_ay_write(uint8_t reg, uint8_t val);

/* ---- 128K paging (port 0x7FFD) ----
 * mem[] always holds what the CPU sees. Paging copies the outgoing bank's
 * 16K out of 0xC000 into zx_banks[] and the incoming one in, so the game
 * code keeps using mem[] directly. Banks 5 and 2 live permanently at
 * 0x4000 and 0x8000; paging either of them in at 0xC000 (which the games
 * never do) sets zx_paging_alias. */
extern int     zx_128k;                  /* 0x7FFD and the AY are decoded */
extern uint8_t zx_banks[8][16384];       /* banks not currently at 0xC000 */
extern uint8_t zx_last_7ffd;
extern int     zx_paging_alias;
void     zx_page_7ffd(uint8_t v);
uint8_t *zx_bank_ptr(int bank);          /* the bank's current home */

/* ---- maskable interrupt line ----
 * /INT goes active at the start of each frame and stays active for
 * zx_int_len T-states (32 on the 48K, 36 on the 128K); an interrupt is
 * accepted at the end of an instruction while it is active and IFF1 is set.
 * zx_int_pending is set by the clock at the frame start; zx_int_active()
 * drops it once the window has passed. */
extern int      zx_int_pending;
extern uint32_t zx_int_len;
static inline int zx_int_active(void)
{
    if (!zx_int_pending) return 0;
    if ((uint32_t)(zx_t - (zx_next_frame - zx_t_per_frame)) >= zx_int_len) { zx_int_pending = 0; return 0; }
    return 1;
}

void zx_reset(void);

#endif
