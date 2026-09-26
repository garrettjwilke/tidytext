#include <genesis.h>
#include "resources.h"
#include "../inc/tidyText.h"

// Font selection: Set this to choose which font to use
// Available options: &tidyText_font_01_short, &tidyText_font_01_tall
static const TileSet* selectedFont = &tidyText_font_01_short;

// this is the amount of pixels in between each character
static const u8 characterPadding = 1;

// after doing a reset, should we erase the tiles from vram
static bool eraseTilesAfterReset = FALSE;

// Maximum number of cached tiles
#define MAX_TILE_CACHE     512

typedef struct {
    u16 charIndex;      // Character ASCII code
    u16 position;       // Position in string (used to calculate tile/offset)
    u16 tileIndex;      // VRAM tile index
    u8 used;
    u32 accessCount;    // Track how often this tile is used (for LRU if needed)
} TileCacheEntry;

static TileCacheEntry tileCache[MAX_TILE_CACHE];
static u16 tilesAllocated = 0;  // Track how many tiles we've allocated
static u16 cacheSize = 0;       // Current number of entries in cache


#define MAX_CHAR_ASCII 127 

// A zero entry is 8 pixels wide. No character is wider than one tile.
static const u8 charWidthLookup[MAX_CHAR_ASCII + 1] = {
    // Override with custom widths from charWidthMap
    [' '] = 2,   // space
    [','] = 3,   // comma
    ['.'] = 2,   // period
    ['?'] = 4,   // question mark
    [':'] = 2,   // colon
    [';'] = 2,   // semicolon
    ['\''] = 1,  // single quote
    ['"'] = 3,   // double quote
    ['`'] = 2,   // tick
    ['~'] = 5,   // tilde
    ['!'] = 2,   // exclamation mark
    ['@'] = 5,   // at symbol
    ['#'] = 5,   // pound/hash/number symbol
    ['$'] = 5,   // dollar sign
    ['%'] = 5,   // percentage
    ['^'] = 5,   // up bracket
    ['&'] = 5,   // ampersand
    ['*'] = 5,   // star/glob
    ['('] = 3,   // open parenthesis
    [')'] = 3,   // close parenthesis
    ['-'] = 4,   // dash/minus
    ['='] = 4,   // equals
    ['_'] = 4,   // underscore
    ['+'] = 5,   // plus
    ['|'] = 3,   // pipe
    ['/'] = 4,   // forward slash
    ['\\'] = 4,  // back slash
    ['<'] = 4,   // open tag bracket
    ['>'] = 4,   // close tag bracket
    ['['] = 3,   // open array bracket
    [']'] = 3,   // close array bracket
    ['{'] = 4,   // open squiggly bracket
    ['}'] = 4,   // close squiggly bracket
    ['0'] = 4,
    ['1'] = 3,
    ['2'] = 4,
    ['3'] = 4,
    ['4'] = 4,
    ['5'] = 4,
    ['6'] = 4,
    ['7'] = 4,
    ['8'] = 4,
    ['9'] = 4,
    ['A'] = 4,
    ['a'] = 4,
    ['B'] = 4,
    ['b'] = 4,
    ['C'] = 4,
    ['c'] = 3,
    ['D'] = 4,
    ['d'] = 4,
    ['E'] = 4,
    ['e'] = 4,
    ['F'] = 4,
    ['f'] = 4,
    ['G'] = 4,
    ['g'] = 4,
    ['H'] = 4,
    ['h'] = 4,
    ['I'] = 3,
    ['i'] = 3,
    ['J'] = 4,
    ['j'] = 3,
    ['K'] = 4,
    ['k'] = 4,
    ['L'] = 3,
    ['l'] = 3,
    ['M'] = 5,
    ['m'] = 5,
    ['N'] = 4,
    ['n'] = 4,
    ['O'] = 4,
    ['o'] = 4,
    ['P'] = 4,
    ['p'] = 4,
    ['Q'] = 5,
    ['q'] = 5,
    ['R'] = 4,
    ['r'] = 3,
    ['S'] = 4,
    ['s'] = 4,
    ['T'] = 5,
    ['t'] = 4,
    ['U'] = 4,
    ['u'] = 4,
    ['V'] = 5,
    ['v'] = 5,
    ['W'] = 5,
    ['w'] = 5,
    ['X'] = 4,
    ['x'] = 4,
    ['Y'] = 5,
    ['y'] = 4,
    ['Z'] = 4,
    ['z'] = 3
};

typedef struct {
    u32 font;
    u32 widths;
    u32 str;
    u32 tileData;
    u32 strLen;
    u32 maxTiles;
    u32 padding;
    u32 primary;
    u32 secondary;
} TidyTextPackParams;

extern u16 tidyText_PackString(const TidyTextPackParams* params);


// Calculate the starting tile index (working backwards from font tiles)
static u16 tidyText_GetBaseTileIndex(void)
{
    u16 baseIndex = TILE_SPRITE_INDEX;
    return baseIndex;
}


// Maximum tiles we can build at once (supports strings up to ~85 characters)
#define MAX_TILES_PER_STRING 64


static u16 tidyText_BuildStringTiles(const char* str, u16 strLen, u16* outTileIndices, u8 primaryPaletteIndex, u8 secondaryPaletteIndex)
{
    if (primaryPaletteIndex > 15) {
        primaryPaletteIndex = 15;
    }
    if (secondaryPaletteIndex > 15) {
        secondaryPaletteIndex = 15;
    }

    static u32 tileData[MAX_TILES_PER_STRING << 3];

    TidyTextPackParams params;
    params.font = (u32) selectedFont->tiles;
    params.widths = (u32) charWidthLookup;
    params.str = (u32) str;
    params.tileData = (u32) tileData;
    params.strLen = strLen;
    params.maxTiles = MAX_TILES_PER_STRING;
    params.padding = characterPadding;
    params.primary = primaryPaletteIndex;
    params.secondary = secondaryPaletteIndex;

    u16 numTilesUsed = tidyText_PackString(&params);
    if (numTilesUsed == 0) {
        return 0;
    }

    u16 baseIndex = tidyText_GetBaseTileIndex();
    u16 start = baseIndex - tilesAllocated - numTilesUsed;
    tilesAllocated += numTilesUsed;

    for (u16 i = 0; i < numTilesUsed; i++) {
        outTileIndices[i] = start + i;
    }
    VDP_loadTileData(tileData, start, numTilesUsed, DMA);

    return numTilesUsed;
}

void tidyText_Reset()
{    
    u32 emptyTile[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (u16 i = 0; i < MAX_TILE_CACHE; i++) {
        if (eraseTilesAfterReset) {
            if (tileCache[i].used) {
                // Clear the VRAM tile by loading empty data
                VDP_loadTileData(emptyTile, tileCache[i].tileIndex, 1, DMA);
            }
        }
        // Clear cache metadata
        tileCache[i].used = FALSE;
        tileCache[i].accessCount = 0;
    }
    
    tilesAllocated = 0; // Reset allocation counter - will allocate from end of VRAM
    cacheSize = 0;      // Reset cache size
    
    // Wait for any pending DMA operations to complete
    VDP_waitDMACompletion();
}

void drawStrings(u8 x, u8 y, u8 plane, u8 palette, u8 primaryPaletteIndex, u8 secondaryPaletteIndex, const char* str)
{
    // Calculate string length
    u8 len = 0;
    const char* p = str;
    while (*p++) len++;
    
    if (len == 0) return;
    
    // Build all tiles for the string (with variable widths)
    u16 tileIndices[MAX_TILES_PER_STRING];
    u16 numTiles = tidyText_BuildStringTiles(str, len, tileIndices, primaryPaletteIndex, secondaryPaletteIndex);

    if (palette > 3) {
        palette = 3;
    }
    
    u16 rowTiles[MAX_TILES_PER_STRING];
    u16 attr = TILE_ATTR_FULL(palette, 0, 0, 0, 0);
    for (u16 tileNum = 0; tileNum < numTiles; tileNum++) {
        rowTiles[tileNum] = attr + tileIndices[tileNum];
    }
    VDP_setTileMapDataRow(plane, rowTiles, y, x, numTiles, CPU);
}

void tidyText_Single(u8 x, u8 y, u8 plane, u8 palette, u8 primaryPaletteIndex, u8 secondaryPaletteIndex, const char* format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsprintf(buffer, format, args);
    va_end(args);
    
    drawStrings(x, y, plane, palette, primaryPaletteIndex, secondaryPaletteIndex, buffer);
}


void tidyText_Multi(u8 x, u8 y, u8 plane, u8 palette, u8 primaryPaletteIndex, u8 secondaryPaletteIndex, const tidyTextStringStruct* tidyTextStrings)
{
    u16 i = 0;
    while (tidyTextStrings[i].str != NULL) {
        const tidyTextStringStruct* line = &tidyTextStrings[i];
        u8 currentY = y + i;
        drawStrings(x, currentY, plane, palette, primaryPaletteIndex, secondaryPaletteIndex, line->str);
        i++;
    }
}
