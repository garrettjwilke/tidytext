#include <genesis.h>
#include "resources.h"
#include "../inc/tidyText.h"

// Font selection: Set this to choose which font to use
// Available options: &tidyText_font_01_short, &tidyText_font_01_tall
static const TileSet* selectedFont = &tidyText_font_01_short;

// this is the amount of pixels in between each character
static const u8 characterPadding = 1;

// Default character width can be less than 8 but never more! characters must fit in a tile
#define DEFAULT_CHAR_WIDTH 8

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

// Only characters with custom widths are specified; others default to 0 (handled in lookup)
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

// Returns DEFAULT_CHAR_WIDTH for uninitialized entries (value 0)
static u8 tidyText_GetCharWidth(u8 asciiChar)
{
    if (asciiChar <= MAX_CHAR_ASCII) {
        u8 width = charWidthLookup[asciiChar];
        return (width != 0) ? width : DEFAULT_CHAR_WIDTH;
    }
    return DEFAULT_CHAR_WIDTH;
}

// Mask character data to only include pixels 0 to width-1
// This ensures pixels beyond the character width are explicitly cleared
static void tidyText_MaskCharData(u32* charData, u8 width)
{
    // Create a mask that includes only the first 'width' pixels (pixels 0 to width-1)
    // Each pixel is 4 bits, so pixel 0 is bits 31-28, pixel 1 is bits 27-24, etc.
    // For width pixels, we need bits 31 down to (32 - width*4)
    // Example: width=4 -> mask = 0xFFFF0000 (covers pixels 0-3, clears pixels 4-7)
    u32 mask = 0xFFFFFFFF << (32 - (width << 2));
    
    // Apply mask to each row to clear pixels beyond the character width
    for (u8 row = 0; row < 8; row++) {
        charData[row] &= mask;
    }
}

static void tidyText_RemapPaletteIndices(u32* tileData, u8 primaryPaletteIndex, u8 secondaryPaletteIndex)
{
    for (u8 row = 0; row < 8; row++) {
        u32 rowData = tileData[row];
        u32 remappedRow = 0;
        
        // Process each pixel (8 pixels per row)
        for (u8 pix = 0; pix < 8; pix++) {
            // Extract the 4-bit palette index for this pixel
            u8 pixelIndex = (rowData >> (28 - (pix << 2))) & 0xF;
            
            // Remap: index 1 -> primaryPaletteIndex, index 2 -> secondaryPaletteIndex, index 0 -> 0
            u8 remappedIndex;
            if (pixelIndex == 1) {
                remappedIndex = primaryPaletteIndex;
            } else if (pixelIndex == 2) {
                remappedIndex = secondaryPaletteIndex;
            } else if (pixelIndex == 0) {
                remappedIndex = 0;
            } else {
                remappedIndex = 0;  // remove all other pixels not in indexes 0-2
            }
            
            // Place the remapped index back into the row
            remappedRow |= ((u32)remappedIndex) << (28 - (pix << 2));
        }
        
        tileData[row] = remappedRow;
    }
}

// Calculate the starting tile index (working backwards from font tiles)
static u16 tidyText_GetBaseTileIndex(void)
{
    u16 baseIndex = TILE_SPRITE_INDEX;
    return baseIndex;
}

static void tidyText_GetCharTileData(u8 asciiCharIndex, u32* outTileData)
{
    const u32* tilesetData = (const u32*)selectedFont->tiles;
    
    // Map ASCII character to tile index
    u16 tileIndex;
    
    // Handle special characters that might be in different positions in the font
    if (asciiCharIndex >= '!' && asciiCharIndex <= '~') {
        tileIndex = asciiCharIndex - 32;
    } else {
        // Other characters - try to map or use a default
        tileIndex = 0;  // Default to space/empty
    }
    
    const u32* charTile = &tilesetData[tileIndex << 3];
    
    for (u8 i = 0; i < 8; i++) {
        outTileData[i] = charTile[i];
    }
}

static void tidyText_PlaceCharPixels(const u32* charData, u32* outTile, u8 charStartPixel, u8 numPixels, u8 tilePixelOffset)
{
    // Each u32 row: bits 31-28=pixel0, 27-24=pixel1, ..., 3-0=pixel7
    // Character has pixels 0-7 in bits 31-0 (0xFFFFFFFF)
    
    // Limit numPixels to ensure we don't exceed character bounds
    if (charStartPixel + numPixels > 8) {
        numPixels = 8 - charStartPixel;
    }
    if (numPixels == 0) return;
    
    for (u8 row = 0; row < 8; row++) {
        u32 charRow = charData[row];
        
        // Build mask for source pixels: charStartPixel to charStartPixel+numPixels-1
        // Only extract pixels that are within the character (0-7)
        u32 sourceMask = 0;
        for (u8 i = 0; i < numPixels; i++) {
            u8 pix = charStartPixel + i;
            if (pix < 8) {  // Character has pixels 0-7
                sourceMask |= (0xF << (28 - (pix << 2)));
            }
        }
        
        u32 extracted = charRow & sourceMask;
        
        // Shift left to move extracted pixels to position 0 (MSB)
        // If pixels are at positions charStartPixel..charStartPixel+numPixels-1,
        // shift left by charStartPixel*4 to get them to positions 0..numPixels-1
        extracted = extracted << (charStartPixel << 2);
        
        // Mask to keep only numPixels at MSB (ensure we don't include extra pixels)
        u32 numPixelsMask = 0xFFFFFFFF << (32 - (numPixels << 2));
        extracted &= numPixelsMask;
        
        // We need to shift right by tilePixelOffset*4 to place them correctly
        u32 result = extracted >> (tilePixelOffset << 2);
        
        // For 4 pixels at offset 0: destMask = 0xFFFF0000 (only pixels 0-3)
        u32 destMask = numPixelsMask >> (tilePixelOffset << 2);
        
        // This ensures we only write to the exact pixels we want
        outTile[row] = (outTile[row] & ~destMask) | (result & destMask);
    }
}

// Maximum tiles we can build at once (supports strings up to ~85 characters)
#define MAX_TILES_PER_STRING 64

// Returns the number of tiles actually used
static u16 tidyText_BuildStringTiles(const char* str, u16 strLen, u16* outTileIndices, u8 primaryPaletteIndex, u8 secondaryPaletteIndex)
{
    // Clamp palette indices to valid range (0-15)
    if (primaryPaletteIndex > 15) {
        primaryPaletteIndex = 15;
    }
    if (secondaryPaletteIndex > 15) {
        secondaryPaletteIndex = 15;
    }
    
    // Use static buffer for tile data (avoid malloc)
    static u32 tileData[MAX_TILES_PER_STRING << 3];
    
    // Initialize all tiles to zero
    for (u16 i = 0; i < (MAX_TILES_PER_STRING << 3); i++) {
        tileData[i] = 0;
    }
    
    // Track current position in tiles
    u16 currentTile = 0;
    u8 currentPixelPos = 0;  // Current pixel position within the current tile (0-7)
    u16 numTilesUsed = 0;
    
    // Process each character and place it at the appropriate position
    for (u16 pos = 0; pos < strLen; pos++) {
        u8 asciiChar = str[pos];
        
        // Safety check
        if (currentTile >= MAX_TILES_PER_STRING) break;
        
        // Get character width (original, without padding)
        u8 charWidth = tidyText_GetCharWidth(asciiChar);
        u32 charData[8];
        tidyText_GetCharTileData(asciiChar, charData);
        
        // Remap palette indices: index 15 -> primaryPaletteIndex, index 14 -> secondaryPaletteIndex, index 0 -> 0
        tidyText_RemapPaletteIndices(charData, primaryPaletteIndex, secondaryPaletteIndex);
        
        // Mask character data to only include pixels 0 to charWidth-1
        tidyText_MaskCharData(charData, charWidth);
        
        // Check if character fits in current tile
        if (currentPixelPos + charWidth <= 8) {
            // Character fits entirely in current tile
            tidyText_PlaceCharPixels(charData, &tileData[currentTile << 3], 0, charWidth, currentPixelPos);
            currentPixelPos += charWidth;
        } else {
            // Character spans across two tiles
            u8 pixelsInFirstTile = 8 - currentPixelPos;
            u8 pixelsInSecondTile = charWidth - pixelsInFirstTile;
            
            // Place first part in current tile
            if (currentTile < MAX_TILES_PER_STRING) {
                tidyText_PlaceCharPixels(charData, &tileData[currentTile << 3], 0, pixelsInFirstTile, currentPixelPos);
            }
            
            // Move to next tile
            currentTile++;
            if (currentTile > numTilesUsed) {
                numTilesUsed = currentTile;
            }
            
            // Place second part in next tile
            if (currentTile < MAX_TILES_PER_STRING && pixelsInSecondTile > 0) {
                tidyText_PlaceCharPixels(charData, &tileData[currentTile << 3], pixelsInFirstTile, pixelsInSecondTile, 0);
                currentPixelPos = pixelsInSecondTile;
            } else {
                currentPixelPos = 0;
            }
        }
        
        // Add padding after the character (except for the last character)
        if (pos < strLen - 1) {  // Don't add padding after the last character
            currentPixelPos += characterPadding;
            
            // Handle padding that spans tiles
            while (currentPixelPos >= 8) {
                currentTile++;
                currentPixelPos -= 8;
                if (currentTile > numTilesUsed) {
                    numTilesUsed = currentTile;
                }
                if (currentTile >= MAX_TILES_PER_STRING) break;
            }
        }
    }
    
    // Calculate actual number of tiles used (add one if current tile has content)
    if (currentPixelPos > 0 || numTilesUsed == 0) {
        numTilesUsed = currentTile + 1;
    } else {
        numTilesUsed = currentTile;
    }
    
    if (numTilesUsed > MAX_TILES_PER_STRING) {
        numTilesUsed = MAX_TILES_PER_STRING;
    }
    
    // Allocate VRAM tiles and load them
    u16 baseIndex = tidyText_GetBaseTileIndex();
    for (u16 i = 0; i < numTilesUsed; i++) {
        u16 tileIndex = baseIndex - tilesAllocated - 1;
        tilesAllocated++;
        outTileIndices[i] = tileIndex;
        VDP_loadTileData(&tileData[i << 3], tileIndex, 1, DMA);
    }
    //VDP_waitDMACompletion();
    
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
    
    for (u16 tileNum = 0; tileNum < numTiles; tileNum++) {
        VDP_setTileMapXY(
            plane,
            TILE_ATTR_FULL(palette, 0, 0, 0, tileIndices[tileNum]),
            x + tileNum,
            y
        );
    }
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