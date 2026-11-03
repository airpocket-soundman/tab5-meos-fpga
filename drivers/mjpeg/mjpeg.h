#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Baseline JPEG (Motion-JPEG frame) decoder to RGB565, with decode-time reduction.
 *
 * Handles 8-bit baseline/extended-sequential Huffman JPEG with 1 or 3 components (4:4:4, 4:2:2, 4:4:0, 4:2:0),
 * restart intervals, and frames without a DHT segment (the standard tables are used, as UVC MJPEG allows).
 * Progressive, arithmetic-coded and 12-bit images are rejected.
 *
 * `shift` reduces the output by 1<<shift (0..3) inside the inverse DCT: half and quarter use the low-frequency 4x4
 * and 2x2 coefficients, 1/8 only the DC term, so a reduced decode is much faster than a full one.  The output is
 * ceil(width*N/8) x ceil(height*N/8) with N = 8 >> shift.
 *
 * Not reentrant: the decoder keeps its tables in static storage, one decode at a time. */
typedef struct {
    int width, height;  /* of the encoded image */
    int components;     /* 1 (grey) or 3 (YCbCr) */
} mjpeg_info_t;

/* Reads the headers only. */
bool mjpeg_info(const uint8_t *data, size_t len, mjpeg_info_t *info);

/* Decodes into out (row-major, out_pixels capacity); returns the output size in *out_w / *out_h. */
bool mjpeg_decode_rgb565(const uint8_t *data, size_t len, unsigned shift, uint16_t *out, size_t out_pixels,
                         int *out_w, int *out_h);

/* Smallest shift (0..3) whose output still fits max_w x max_h; 3 if none does. */
unsigned mjpeg_shift_for(int width, int height, int max_w, int max_h);
