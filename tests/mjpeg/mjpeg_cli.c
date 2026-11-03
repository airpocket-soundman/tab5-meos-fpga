/* Host harness for drivers/mjpeg: mjpeg_cli IN.jpg SHIFT OUT.rgb565
 * Prints "W H" (output size) and writes the RGB565 pixels little-endian; "info" as SHIFT prints the image size. */
#include <stdio.h>
#include <stdlib.h>

#include "../../drivers/mjpeg/mjpeg.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: mjpeg_cli IN.jpg SHIFT|info [OUT.rgb565]\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)n);
    if (!data || fread(data, 1, (size_t)n, f) != (size_t)n)
        return 2;
    fclose(f);
    if (argv[2][0] == 'i') {
        mjpeg_info_t info;
        if (!mjpeg_info(data, (size_t)n, &info))
            return 1;
        printf("%d %d %d\n", info.width, info.height, info.components);
        return 0;
    }
    unsigned shift = (unsigned)atoi(argv[2]);
    size_t cap = 4096 * 4096;
    uint16_t *out = malloc(cap * sizeof *out);
    int w = 0, h = 0;
    if (!out || !mjpeg_decode_rgb565(data, (size_t)n, shift, out, cap, &w, &h))
        return 1;
    printf("%d %d\n", w, h);
    if (argc > 3) {
        FILE *o = fopen(argv[3], "wb");
        if (!o)
            return 2;
        fwrite(out, sizeof *out, (size_t)w * (size_t)h, o);
        fclose(o);
    }
    return 0;
}
