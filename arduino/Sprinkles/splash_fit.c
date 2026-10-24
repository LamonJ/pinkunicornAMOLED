#include "splash_fit.h"

#include <string.h>

int splash_fit_begin(splash_fit_t *f, uint16_t *dst, uint32_t src_w, uint32_t src_h)
{
    f->dst = dst;
    f->src_w = src_w;
    f->src_h = src_h;
    f->wrote = 0;
    f->bad = 0;
    if (!dst || src_w == 0 || src_h == 0 || src_w > 4096 || src_h > 4096) {
        f->bad = 1;
        return 0;
    }
    memset(dst, 0, (size_t)SPLASH_PANEL * SPLASH_PANEL * sizeof(uint16_t));
    return 1;
}

static uint16_t pack565(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (a == 0) return 0;
    if (a != 255) {
        r = (uint8_t)((r * a) / 255);
        g = (uint8_t)((g * a) / 255);
        b = (uint8_t)((b * a) / 255);
    }
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

void splash_fit_pixels(splash_fit_t *f, uint32_t x, uint32_t y,
                       uint32_t w, uint32_t h, const uint8_t rgba[4])
{
    if (!f || f->bad || !f->dst) return;
    uint32_t x1 = x + w;
    uint32_t y1 = y + h;
    if (x >= f->src_w || y >= f->src_h) return;
    if (x1 > f->src_w) x1 = f->src_w;
    if (y1 > f->src_h) y1 = f->src_h;
    uint16_t p = pack565(rgba[0], rgba[1], rgba[2], rgba[3]);
    if (p == 0) return;

    for (uint32_t yy = y; yy < y1; yy++) {
        uint32_t dy0 = (uint32_t)(((uint64_t)yy * SPLASH_PANEL) / f->src_h);
        uint32_t dy1 = (uint32_t)(((uint64_t)(yy + 1) * SPLASH_PANEL) / f->src_h);
        if (dy0 >= SPLASH_PANEL) continue;
        if (dy1 > SPLASH_PANEL) dy1 = SPLASH_PANEL;
        for (uint32_t xx = x; xx < x1; xx++) {
            uint32_t dx0 = (uint32_t)(((uint64_t)xx * SPLASH_PANEL) / f->src_w);
            uint32_t dx1 = (uint32_t)(((uint64_t)(xx + 1) * SPLASH_PANEL) / f->src_w);
            if (dx0 >= SPLASH_PANEL) continue;
            if (dx1 > SPLASH_PANEL) dx1 = SPLASH_PANEL;
            /* A source pixel that lands inside a dest pixel already covered
             * has an empty span. Skip it. An upscale has a span of 1 or more. */
            if (dx1 <= dx0 || dy1 <= dy0) continue;
            for (uint32_t dy = dy0; dy < dy1; dy++) {
                uint16_t *row = f->dst + (size_t)dy * SPLASH_PANEL;
                for (uint32_t dx = dx0; dx < dx1; dx++)
                    row[dx] = p;
            }
            f->wrote++;
        }
    }
}

void splash_fit_mask(splash_fit_t *f)
{
    if (!f || !f->dst) return;
    const int c = SPLASH_PANEL / 2;
    const int r2 = c * c;
    for (int y = 0; y < SPLASH_PANEL; y++) {
        int dy = y - c;
        uint16_t *row = f->dst + (size_t)y * SPLASH_PANEL;
        for (int x = 0; x < SPLASH_PANEL; x++) {
            int dx = x - c;
            if (dx * dx + dy * dy > r2) row[x] = 0;
        }
    }
}
