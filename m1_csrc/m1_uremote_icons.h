/* See COPYING.txt for license details. */

/*
*
* m1_uremote_icons.h
*
* Hand-drawn 8x8 XBM glyphs for the Universal Remotes icon panel. Same byte
* layout as the other vendored glyphs in m1_display_data.c (one byte per row,
* LSB = leftmost pixel), drawn with u8g2_DrawXBMP() in transparent mode.
*
* v1 covers exactly the functions the shipped category tables reference:
*   Power, Vol_Up, Vol_Down, Ch_Up, Ch_Down, Mute, Source, Menu.
*
* Definitions live in m1_display_data.c (alongside every other glyph) so there
* is a single copy in flash; this header only declares them plus a tiny name
* -> glyph lookup for the category tables.
*
* Glyph legend (for review):
*   Power    power symbol (I-bar over an open ring)
*   Vol_Up   speaker + '+'
*   Vol_Down speaker + '-'
*   Ch_Up    up arrow with stem
*   Ch_Down  down arrow with stem
*   Mute     speaker + 'x'
*   Source   monitor/screen outline on a stand
*   Menu     three stacked lines (hamburger)
*
* M1 Project
*
*/

#ifndef M1_UREMOTE_ICONS_H_
#define M1_UREMOTE_ICONS_H_

#include <stdint.h>

extern const uint8_t uremote_power_8x8[];
extern const uint8_t uremote_vol_up_8x8[];
extern const uint8_t uremote_vol_down_8x8[];
extern const uint8_t uremote_ch_up_8x8[];
extern const uint8_t uremote_ch_down_8x8[];
extern const uint8_t uremote_mute_8x8[];
extern const uint8_t uremote_source_8x8[];
extern const uint8_t uremote_menu_8x8[];

#endif /* M1_UREMOTE_ICONS_H_ */
