/* Fit a decoded PNG onto the 466 circle. Pure C so the host test can run it. */
#ifndef SPLASH_FIT_H
#define SPLASH_FIT_H

#include <stdint.h>

#define SPLASH_PANEL 466

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t *dst;
    uint32_t  src_w, src_h;
    int       wrote;
    int       bad;
} splash_fit_t;

/* 0 when the picture is empty or larger than 4096 on a side. */
int splash_fit_begin(splash_fit_t *f, uint16_t *dst, uint32_t src_w, uint32_t src_h);

/* One source pixel, or a rectangle of them (interlaced PNGs). */
void splash_fit_pixels(splash_fit_t *f, uint32_t x, uint32_t y,
                       uint32_t w, uint32_t h, const uint8_t rgba[4]);

/* Black outside the round glass. */
void splash_fit_mask(splash_fit_t *f);

#ifdef __cplusplus
}
#endif

#endif
