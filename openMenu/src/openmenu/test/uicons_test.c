/* Renders the five glyphs to a PPM contact sheet (argv[1]) on a blue background and checks that every glyph has pixels. */
#include <stdio.h>
#include <stdlib.h>

#include "../src/backend/uicons.h"

int
main(int argc, char** argv) {
    enum { Z = 4, PAD = 8 };
    const int W = UICON_COUNT * (UICON_SIZE * Z + PAD) + PAD, H = 2 * (UICON_SIZE * Z + PAD) + PAD;
    unsigned char* img = malloc((size_t)W * H * 3);
    for (int i = 0; i < W * H; i++) { img[i * 3] = 60; img[i * 3 + 1] = 140; img[i * 3 + 2] = 200; }
    for (int row = 0; row < 2; row++) {
        for (int id = 0; id < UICON_COUNT; id++) {
            uint16_t px[UICON_SIZE * UICON_SIZE];
            uint32_t colour = row == 0 ? 0xFF000000u : 0xFF1C2850u;
            int on = 0;
            uicon_render(id, colour, px);
            for (int i = 0; i < UICON_SIZE * UICON_SIZE; i++) {
                on += (px[i] >> 12) != 0;
            }
            if (on < 150) {
                fprintf(stderr, "glyph %d is nearly empty (%d pixels)\n", id, on);
                return 1;
            }
            for (int y = 0; y < UICON_SIZE * Z; y++) {
                for (int x = 0; x < UICON_SIZE * Z; x++) {
                    uint16_t p = px[(y / Z) * UICON_SIZE + x / Z];
                    int a = p >> 12, c[3] = {(p >> 8) & 15, (p >> 4) & 15, p & 15};
                    unsigned char* o = img + (((size_t)row * (UICON_SIZE * Z + PAD) + PAD + y) * W + PAD + id * (UICON_SIZE * Z + PAD) + x) * 3;
                    for (int k = 0; k < 3; k++) {
                        o[k] = (unsigned char)((c[k] * 17 * a + o[k] * (15 - a)) / 15);
                    }
                }
            }
        }
    }
    if (argc > 1) {
        FILE* f = fopen(argv[1], "wb");
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        fwrite(img, 1, (size_t)W * H * 3, f);
        fclose(f);
    }
    free(img);
    puts("uicons ok");
    return 0;
}
