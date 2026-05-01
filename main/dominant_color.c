#include "dominant_color.h"
#include <stdlib.h>

#define HIST_BITS 4
#define HIST_SZ   (1 << HIST_BITS)               // 16 levels per channel
#define HIST_LEN  (HIST_SZ * HIST_SZ * HIST_SZ)  // 4096 buckets total

int dominant_colors(const uint8_t *pixels, int n_pixels, uint32_t *out, int n, bool bgr)
{
    // 4096 buckets × 4 bytes = 16 KB
    uint32_t *hist = calloc(HIST_LEN, sizeof(uint32_t));
    if (!hist) return 0;

    int ri = bgr ? 2 : 0;
    int bi = bgr ? 0 : 2;

    for (int i = 0; i < n_pixels; i++) {
        const uint8_t *p = pixels + i * 3;
        uint32_t r = p[ri] >> (8 - HIST_BITS);
        uint32_t g = p[1]  >> (8 - HIST_BITS);
        uint32_t b = p[bi] >> (8 - HIST_BITS);
        hist[r * HIST_SZ * HIST_SZ + g * HIST_SZ + b]++;
    }

    int found = 0;
    for (int k = 0; k < n; k++) {
        uint32_t best = 0;
        int idx = 0;
        for (int i = 0; i < HIST_LEN; i++) {
            if (hist[i] > best) { best = hist[i]; idx = i; }
        }
        if (best == 0) break;

        // Reconstruct channel midpoints from bucket index
        uint8_t r = (uint8_t)(((idx >> (HIST_BITS * 2))             * 255) / (HIST_SZ - 1));
        uint8_t g = (uint8_t)((((idx >>  HIST_BITS) & (HIST_SZ - 1)) * 255) / (HIST_SZ - 1));
        uint8_t b = (uint8_t)(( (idx               & (HIST_SZ - 1))  * 255) / (HIST_SZ - 1));

        out[found++] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        hist[idx] = 0;  // zero so the next pass finds the next highest
    }

    free(hist);
    return found;
}
