#include "asm_mac.i"

// Pack a whole string into 8x8 tiles.
// Pixel 0 stays in bits 31-28. Nibbles 1 and 2 remap to the two palette
// indices; every other nibble becomes transparent. The C side uploads the
// buffer with one DMA.
//
// Stack argument is a pointer to nine u32 fields:
//   font, widths, str, tileData, strLen, maxTiles, padding, primary, secondary

#define PIXEL_POS   -4
#define CUR_TILE    -8
#define STR_LEFT    -12
#define PAD_PX      -16
#define MAX_TILES   -20
#define COLORS_EQ   -24
#define GLYPH_W     -28
#define WIDTH_MASK  -32
#define SHIFT_N     -36
#define LEFT_MASK   -40
#define RIGHT_SHIFT -44
#define DEST2       -48
#define DEST_LEFT   -52
#define ROW_SAVE    -56

func tidyText_PackString
    movem.l %d2-%d7/%a2-%a6, -(%sp)
    move.l 48(%sp), %a0
    link %a6, #-64

    move.l (%a0)+, %a4
    move.l (%a0)+, %a3
    move.l (%a0)+, %a2
    move.l (%a0)+, %a5
    move.l (%a0)+, STR_LEFT(%a6)
    move.l (%a0)+, MAX_TILES(%a6)
    move.l (%a0)+, PAD_PX(%a6)

    move.l (%a0)+, %d0
    bsr replicate_nibble
    move.l %d0, %d4
    move.l (%a0)+, %d0
    bsr replicate_nibble
    move.l %d0, %d5

    move.l #0x11111111, %d6
    move.l #0x33333333, %d7
    cmp.l %d4, %d5
    seq %d0
    andi.l #1, %d0
    move.l %d0, COLORS_EQ(%a6)

    clr.l PIXEL_POS(%a6)
    clr.l CUR_TILE(%a6)
    move.l %a5, %a1
    bsr clear_a1

.char_loop:
    tst.l STR_LEFT(%a6)
    beq .finish
    move.l CUR_TILE(%a6), %d0
    cmp.l MAX_TILES(%a6), %d0
    bhs .finish

    subq.l #1, STR_LEFT(%a6)
    moveq #0, %d0
    move.b (%a2)+, %d0
    move.l %d0, %d1

    cmp.w #127, %d0
    bhi .width8
    moveq #0, %d2
    move.b (%a3, %d0.w), %d2
    tst.b %d2
    bne .width_ok
.width8:
    moveq #8, %d2
.width_ok:
    move.l %d2, GLYPH_W(%a6)

    move.l %d1, %d0
    cmp.b #33, %d0
    blo .font0
    cmp.b #126, %d0
    bhi .font0
    sub.w #32, %d0
    bra .font_ok
.font0:
    moveq #0, %d0
.font_ok:
    lsl.w #5, %d0
    move.l %a4, %a0
    add.w %d0, %a0

    move.l PIXEL_POS(%a6), %d0
    add.l GLYPH_W(%a6), %d0
    cmp.l #8, %d0
    bhi .span

    // Glyph fits in the current tile.
    moveq #8, %d0
    sub.l GLYPH_W(%a6), %d0
    lsl.l #2, %d0
    moveq #-1, %d1
    lsl.l %d0, %d1
    move.l %d1, WIDTH_MASK(%a6)
    move.l PIXEL_POS(%a6), %d0
    lsl.l #2, %d0
    move.l %d0, SHIFT_N(%a6)

    bsr load_tile_a1
    moveq #7, %d2
.fit_row:
    move.l (%a0)+, %d0
    move.l %d2, -(%sp)
    bsr remap_row
    and.l WIDTH_MASK(%a6), %d0
    move.l SHIFT_N(%a6), %d1
    lsr.l %d1, %d0
    or.l %d0, (%a1)+
    move.l (%sp)+, %d2
    dbra %d2, .fit_row

    move.l PIXEL_POS(%a6), %d0
    add.l GLYPH_W(%a6), %d0
    move.l %d0, PIXEL_POS(%a6)
    bra .after_glyph

.span:
    // Left piece stays on this tile. Right piece starts the next one.
    move.l PIXEL_POS(%a6), %d0
    lsl.l #2, %d0
    move.l %d0, SHIFT_N(%a6)
    moveq #-1, %d1
    lsl.l %d0, %d1
    move.l %d1, LEFT_MASK(%a6)

    moveq #8, %d0
    sub.l PIXEL_POS(%a6), %d0
    lsl.l #2, %d0
    move.l %d0, RIGHT_SHIFT(%a6)

    moveq #8, %d0
    sub.l GLYPH_W(%a6), %d0
    lsl.l #2, %d0
    moveq #-1, %d1
    lsl.l %d0, %d1
    move.l %d1, WIDTH_MASK(%a6)

    bsr load_tile_a1
    move.l %a1, DEST_LEFT(%a6)

    move.l CUR_TILE(%a6), %d0
    addq.l #1, %d0
    cmp.l MAX_TILES(%a6), %d0
    bhs .span_left_only

    move.l %d0, %d1
    lsl.w #5, %d1
    move.l %a5, %a1
    add.w %d1, %a1
    move.l %a1, DEST2(%a6)
    bsr clear_a1

    move.l DEST_LEFT(%a6), %a1
    move.l %a3, -(%sp)
    move.l DEST2(%a6), %a3
    moveq #7, %d2
.span_row:
    move.l (%a0)+, %d0
    move.l %d2, -(%sp)
    bsr remap_row
    move.l %d0, ROW_SAVE(%a6)
    and.l LEFT_MASK(%a6), %d0
    move.l SHIFT_N(%a6), %d1
    lsr.l %d1, %d0
    or.l %d0, (%a1)+
    move.l ROW_SAVE(%a6), %d0
    and.l WIDTH_MASK(%a6), %d0
    move.l RIGHT_SHIFT(%a6), %d1
    lsl.l %d1, %d0
    or.l %d0, (%a3)+
    move.l (%sp)+, %d2
    dbra %d2, .span_row
    move.l (%sp)+, %a3

    move.l CUR_TILE(%a6), %d0
    addq.l #1, %d0
    move.l %d0, CUR_TILE(%a6)
    move.l GLYPH_W(%a6), %d0
    add.l PIXEL_POS(%a6), %d0
    subq.l #8, %d0
    move.l %d0, PIXEL_POS(%a6)
    bra .after_glyph

.span_left_only:
    move.l DEST_LEFT(%a6), %a1
    moveq #7, %d2
.span_left_row:
    move.l (%a0)+, %d0
    move.l %d2, -(%sp)
    bsr remap_row
    and.l LEFT_MASK(%a6), %d0
    move.l SHIFT_N(%a6), %d1
    lsr.l %d1, %d0
    or.l %d0, (%a1)+
    move.l (%sp)+, %d2
    dbra %d2, .span_left_row
    move.l CUR_TILE(%a6), %d0
    addq.l #1, %d0
    move.l %d0, CUR_TILE(%a6)
    clr.l PIXEL_POS(%a6)
    cmp.l MAX_TILES(%a6), %d0
    bhs .finish

.after_glyph:
    tst.l STR_LEFT(%a6)
    beq .char_loop
    move.l PIXEL_POS(%a6), %d0
    add.l PAD_PX(%a6), %d0
    move.l %d0, PIXEL_POS(%a6)
.pad_while:
    cmp.l #8, PIXEL_POS(%a6)
    blo .char_loop
    move.l CUR_TILE(%a6), %d0
    addq.l #1, %d0
    move.l %d0, CUR_TILE(%a6)
    move.l PIXEL_POS(%a6), %d1
    subq.l #8, %d1
    move.l %d1, PIXEL_POS(%a6)
    cmp.l MAX_TILES(%a6), %d0
    bhs .finish
    bsr load_tile_a1
    bsr clear_a1
    bra .pad_while

.finish:
    tst.l PIXEL_POS(%a6)
    beq .exact_tile
    move.l CUR_TILE(%a6), %d0
    addq.l #1, %d0
    bra .cap_tiles
.exact_tile:
    move.l CUR_TILE(%a6), %d0
    bne .cap_tiles
    moveq #1, %d0
.cap_tiles:
    cmp.l MAX_TILES(%a6), %d0
    bls .leave
    move.l MAX_TILES(%a6), %d0
.leave:
    unlk %a6
    movem.l (%sp)+, %d2-%d7/%a2-%a6
    rts

// d0 = palette index 0-15. Returns 0xPPPPPPPP in d0. Clobbers d1.
replicate_nibble:
    andi.l #0x0F, %d0
    move.l %d0, %d1
    lsl.l #4, %d0
    or.l %d0, %d1
    move.l %d1, %d0
    lsl.l #8, %d0
    or.l %d0, %d1
    move.l %d1, %d0
    swap %d0
    or.l %d1, %d0
    rts

// a1 = tile start. Clobbers d0.
clear_a1:
    moveq #0, %d0
    move.l %d0, (%a1)
    move.l %d0, 4(%a1)
    move.l %d0, 8(%a1)
    move.l %d0, 12(%a1)
    move.l %d0, 16(%a1)
    move.l %d0, 20(%a1)
    move.l %d0, 24(%a1)
    move.l %d0, 28(%a1)
    rts

// a1 = tileData + currentTile * 32. Clobbers d0.
load_tile_a1:
    move.l CUR_TILE(%a6), %d0
    lsl.w #5, %d0
    move.l %a5, %a1
    add.w %d0, %a1
    rts

// d0 = font row in, remapped row out. Clobbers d1-d3. Keeps a0/a1.
remap_row:
    move.l %d0, %d1
    and.l %d7, %d0
    eor.l %d0, %d1
    beq 1f
    move.l %d0, -(%sp)
    move.l %d1, %d0
    lsr.l #2, %d1
    lsr.l #3, %d0
    or.l %d0, %d1
    and.l %d6, %d1
    not.l %d1
    and.l %d6, %d1
    move.l (%sp)+, %d0
    bra 2f
1:
    move.l %d6, %d1
2:
    move.l %d0, %d2
    lsr.l #1, %d2
    and.l %d6, %d0
    and.l %d6, %d2
    and.l %d1, %d0
    and.l %d1, %d2
    move.l %d0, %d1
    and.l %d2, %d1
    eor.l %d1, %d0
    eor.l %d1, %d2
    tst.l COLORS_EQ(%a6)
    beq 3f
    or.l %d2, %d0
    move.l %d0, %d1
    lsl.l #1, %d1
    or.l %d1, %d0
    move.l %d0, %d1
    lsl.l #2, %d1
    or.l %d1, %d0
    and.l %d4, %d0
    rts
3:
    move.l %d0, %d1
    lsl.l #1, %d1
    or.l %d1, %d0
    move.l %d0, %d1
    lsl.l #2, %d1
    or.l %d1, %d0
    and.l %d4, %d0
    move.l %d0, %d3
    move.l %d2, %d0
    move.l %d0, %d1
    lsl.l #1, %d1
    or.l %d1, %d0
    move.l %d0, %d1
    lsl.l #2, %d1
    or.l %d1, %d0
    and.l %d5, %d0
    or.l %d3, %d0
    rts
