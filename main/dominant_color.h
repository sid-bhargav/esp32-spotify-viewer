#pragma once
#include <stdint.h>
#include <stdbool.h>

// Finds the `n` most dominant colors in a packed 3-bytes-per-pixel buffer via
// histogram bucketing (4 bits per channel, 4096 buckets, 16 KB heap).
//
// pixels   - flat array of n_pixels * 3 bytes
// n_pixels - total pixel count (width * height)
// out      - caller-allocated array of at least `n` uint32_t values
// n        - how many dominant colors to find (e.g. 3)
// bgr      - true if the buffer is BGR order (e.g. TJpgDec output), false for RGB
//
// Returns the number of colors written to out[] (may be < n for tiny images).
// Output colors are always 0xRRGGBB regardless of input channel order.
int dominant_colors(const uint8_t *pixels, int n_pixels, uint32_t *out, int n, bool bgr);
