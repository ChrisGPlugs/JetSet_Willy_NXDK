/*
 * zx_font.h - install the character set at 0x3D00.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef ZX_FONT_H
#define ZX_FONT_H

/* rom16k: a 16384-byte Spectrum ROM image, or NULL for the built-in font. */
void zx_install_font(const unsigned char *rom16k);

#endif
