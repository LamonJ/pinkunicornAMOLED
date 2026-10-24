/* Decode splash.png the way the board will, without the SD card. */
#include "../arduino_src/pngle.h"
#include "../arduino_src/splash_fit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static splash_fit_t Fit;

static void on_init(pngle_t *png, uint32_t w, uint32_t h)
{
    (void)png;
    if (!splash_fit_begin(&Fit, Fit.dst, w, h))
        fprintf(stderr, "reject %ux%u\n", w, h);
}

static void on_draw(pngle_t *png, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    const uint8_t rgba[4])
{
    (void)png;
    splash_fit_pixels(&Fit, x, y, w, h, rgba);
}

static int decode_limited(const char *path, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    pngle_t *png = pngle_new();
    if (!png) { fclose(f); return 1; }
    pngle_set_init_callback(png, on_init);
    pngle_set_draw_callback(png, on_draw);
    pngle_set_user_data(png, &Fit);
    uint8_t *buf = malloc(cap ? cap : 1);
    size_t have = 0;
    int ok = buf != NULL;
    while (ok) {
        size_t before = have;
        if (have < cap) {
            size_t n = fread(buf + have, 1, cap - have, f);
            have += n;
            if (n == 0 && have == 0) break;
        }
        int fed = pngle_feed(png, buf, have);
        if (fed < 0) {
            fprintf(stderr, "%s: %s\n", path, pngle_error(png));
            ok = 0;
            break;
        }
        if (fed == 0) {
            if (have > before && have < cap && !feof(f)) continue;
            ok = 0;
            break;
        }
        memmove(buf, buf + fed, have - (size_t)fed);
        have -= (size_t)fed;
        if (Fit.bad) { ok = 0; break; }
    }
    pngle_destroy(png);
    free(buf);
    fclose(f);
    if (ok) splash_fit_mask(&Fit);
    /* Any drawn pixel is a successful decode. The board asks for more than
     * a speck before it replaces the built-in picture. */
    return ok && !Fit.bad && Fit.wrote > 0 ? 0 : 1;
}

static int decode(const char *path)
{
    return decode_limited(path, 4096);
}

static int g_fails;

static void expect_eq(const char *what, int got, int want)
{
    if (got == want) return;
    fprintf(stderr, "FAIL %s: got %d want %d\n", what, got, want);
    g_fails++;
}

static uint16_t at(const uint16_t *dst, int x, int y)
{
    return dst[(size_t)y * SPLASH_PANEL + x];
}

/* Direct calls on the fitter, so the scale math does not depend on a PNG. */
static int test_fit(void)
{
    uint16_t *dst = calloc((size_t)SPLASH_PANEL * SPLASH_PANEL, sizeof(uint16_t));
    splash_fit_t f;
    const uint8_t red[4] = {255, 0, 0, 255};
    const uint8_t green[4] = {0, 255, 0, 255};
    const uint8_t blue[4] = {0, 0, 255, 255};
    const uint8_t clear[4] = {255, 0, 0, 0};
    const uint8_t half[4] = {255, 0, 0, 128};
    if (!dst) return 1;

    expect_eq("reject wide", splash_fit_begin(&f, dst, 4097, 10), 0);
    expect_eq("wide is bad", f.bad, 1);
    expect_eq("begin 100x50", splash_fit_begin(&f, dst, 100, 50), 1);
    splash_fit_pixels(&f, 10, 20, 1, 1, red);
    /* src (10,20) owns dest x 46..50 and y 186..194. */
    expect_eq("red origin", at(dst, 46, 186), 0xF800);
    expect_eq("red far", at(dst, 50, 194), 0xF800);
    expect_eq("left of cell", at(dst, 45, 186), 0);
    expect_eq("right of cell", at(dst, 51, 186), 0);
    expect_eq("above cell", at(dst, 46, 185), 0);
    expect_eq("below cell", at(dst, 46, 195), 0);

    expect_eq("begin 1000", splash_fit_begin(&f, dst, 1000, 466), 1);
    splash_fit_pixels(&f, 0, 0, 1, 1, blue);
    splash_fit_pixels(&f, 1, 0, 1, 1, blue);
    splash_fit_pixels(&f, 2, 0, 1, 1, green);
    /* 1000 across 466: source 0 and 1 have an empty span. Source 2 owns x=0. */
    expect_eq("downscale keeps later pixel", at(dst, 0, 0), 0x07E0);
    expect_eq("identity begin", splash_fit_begin(&f, dst, 466, 466), 1);
    splash_fit_pixels(&f, 100, 200, 1, 1, red);
    expect_eq("1:1 pixel", at(dst, 100, 200), 0xF800);
    expect_eq("1:1 neighbour", at(dst, 101, 200), 0);
    splash_fit_pixels(&f, 120, 200, 1, 1, clear);
    expect_eq("alpha 0 stays black", at(dst, 120, 200), 0);
    splash_fit_pixels(&f, 130, 200, 1, 1, half);
    /* 255*128/255 = 128, which is r5 = 16. */
    expect_eq("half alpha on black", at(dst, 130, 200), 0x8000);
    splash_fit_pixels(&f, 0, 0, 1, 1, red);
    splash_fit_mask(&f);
    expect_eq("corner masked", at(dst, 0, 0), 0);
    expect_eq("inside kept", at(dst, 100, 200), 0xF800);

    free(dst);
    if (g_fails) fprintf(stderr, "fit: %d failed\n", g_fails);
    else fprintf(stderr, "fit: ok\n");
    return g_fails ? 1 : 0;
}

static void write_ppm(const char *path, const uint16_t *src)
{
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", SPLASH_PANEL, SPLASH_PANEL);
    for (int i = 0; i < SPLASH_PANEL * SPLASH_PANEL; i++) {
        uint16_t p = src[i];
        unsigned r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
        unsigned char px[3] = {
            (unsigned char)((r << 3) | (r >> 2)),
            (unsigned char)((g << 2) | (g >> 4)),
            (unsigned char)((b << 3) | (b >> 2))
        };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (test_fit()) return 1;
    if (argc < 3) {
        fprintf(stderr, "usage: %s in.png out.ppm\n", argv[0]);
        return 2;
    }
    Fit.dst = calloc((size_t)SPLASH_PANEL * SPLASH_PANEL, sizeof(uint16_t));
    if (!Fit.dst) return 1;
    /* Three bytes cannot hold a PNG signature, and must fail instead of looping.
     * Sixteen bytes covers the 13-byte header and must match a big read,
     * including a picture the fitter rejects. */
    expect_eq("short buffer rejected", decode_limited(argv[1], 3), 1);
    int small = decode_limited(argv[1], 16);
    int wrote_small = Fit.wrote;
    int bad_small = Fit.bad;
    uint16_t mid = at(Fit.dst, 200, 180);
    int rc = decode(argv[1]);
    expect_eq("chunk result agrees", small, rc);
    expect_eq("chunk size agrees", wrote_small, Fit.wrote);
    expect_eq("chunk bad agrees", bad_small, Fit.bad);
    expect_eq("chunk pixel agrees", mid, at(Fit.dst, 200, 180));
    if (g_fails) rc = 1;
    if (rc == 0) write_ppm(argv[2], Fit.dst);
    fprintf(stderr, "%s wrote %d bad %d -> %s\n", argv[1], Fit.wrote, Fit.bad,
            rc ? "FAIL" : "ok");
    free(Fit.dst);
    return rc;
}
