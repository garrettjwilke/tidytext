#ifndef TIDYTEXT_H
#define TIDYTEXT_H

typedef struct {
    const char* str;
} tidyTextStringStruct;

void tidyText_Reset();

void tidyText_Single(u8 x, u8 y, u8 plane, u8 palette, u8 primaryPaletteIndex, u8 secondaryPaletteIndex, const char* format, ...);
void tidyText_Multi(u8 x, u8 y, u8 plane, u8 palette, u8 primaryPaletteIndex, u8 secondaryPaletteIndex, const tidyTextStringStruct* tidyTextStrings);

#endif // TIDYTEXT_H
