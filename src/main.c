#include <genesis.h>
#include "resources.h"
#include "../inc/tidyText.h"

const tidyTextStringStruct multiLines[] = {
    {"tidytext allows way more than 40 characters. I can fit so much more now"},
    {"1234567890-="},
    {"!@#$%^&*()_+"},
    {"qwertyuiop[]\\"},
    {"QWERTYUIOP{}|"},
    {"asdfghjkl;'"},
    {"ASDFGHJKL:"},
    {"zxcvbnm,./"},
    {"ZXCVBNM<>?"},
    {""},
    {"{test <inside> brackets (should work)|just fine}"},
    {""},
    {NULL}
};

int main() {
    PAL_setColors(0, testPalette01.data, 64, DMA);

    tidyText_Reset();

    VDP_drawTextBG(BG_A, "this is the default 8x8 font.", 0, 3);
    VDP_drawTextBG(BG_A, "Default font has 40 character max limit!", 0, 4);
    tidyText_Multi(0, 5, BG_A, PAL1, 9, 9, multiLines);

    u16 currentScore = 12345;
    tidyText_Single(10, 20, BG_A, PAL2, 8, 8, "score: %u", currentScore);

    while(1) {
        SYS_doVBlankProcess();
    }
    return 0;
}