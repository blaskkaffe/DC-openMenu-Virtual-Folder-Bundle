/* Writes src/backend/uicons_alpha.inc: the coverage (4 bits, two pixels a byte) of the Internet screen's five glyphs as uicon_render
 * draws them, so the console does not render them (half a second of signed-distance maths on the SH4). Run after changing uicons.c:
 *   gcc -O2 -DUICONS_GEN tools/gen_uicons.c src/backend/uicons.c -lm -o gen_uicons && ./gen_uicons > src/backend/uicons_alpha.inc
 * test/uicons_test.c checks the table against the renderer. */
#include <stdio.h>

#include "../src/backend/uicons.h"

int
main(void) {
    printf("/* Made by tools/gen_uicons.c from uicons.c: do not edit. */\n");
    printf("static const uint8_t uicon_alpha_table[UICON_COUNT][UICON_SIZE * UICON_SIZE / 2] = {\n");
    for (int id = 0; id < UICON_COUNT; id++) {
        uint16_t px[UICON_SIZE * UICON_SIZE];
        uicon_render(id, 0xFF000000u, px);
        printf("    {");
        for (int i = 0; i < UICON_SIZE * UICON_SIZE; i += 2) {
            printf("%s0x%02X", i ? (i % 32 ? "," : ",\n     ") : "", (unsigned)((px[i] >> 12) | ((px[i + 1] >> 12) << 4)));
        }
        printf("},\n");
    }
    printf("};\n");
    return 0;
}
