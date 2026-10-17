/* test_blit.c — the square world mapped onto the round 466 panel.
 *
 * The Waveshare ESP32-S3-Touch-AMOLED-1.75 is a circle. These checks pin
 * three things the eye would miss until the glass is in a hand:
 *   - a world pixel lands on the same panel pixels the 7x scale always used
 *   - the caps between that square and the bezel are the clear colour
 *   - the square's corners, which are not on the glass, stay off
 */
#include "../sprinkles/gfx.h"
#include <stdio.h>
#include <stdlib.h>

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { \
    fails++; printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t rgb565(unsigned c)
{
    return (uint16_t)((((c >> 16) & 0xF8) << 8) |
                      (((c >>  8) & 0xFC) << 3) |
                      (( c        & 0xF8) >> 3));
}

static int on_glass(int x, int y)
{
    int dx = x - GFX_CIRCLE_C, dy = y - GFX_CIRCLE_C;
    return dx * dx + dy * dy <= GFX_CIRCLE_R * GFX_CIRCLE_R;
}

static void test_round_mapping(void)
{
    const unsigned bg = 0x05060A;     /* C_VOID */
    const unsigned red = 0xFF0000;
    const unsigned grn = 0x00FF00;
    const unsigned blu = 0x0000FF;
    uint16_t *panel = calloc((size_t)GFX_PANEL * GFX_PANEL, sizeof(uint16_t));
    CHECK(panel != NULL, "panel buffer");
    if (!panel) return;

    gfx_clear(bg);
    gfx_px(32, 32, red);              /* just right of centre */
    gfx_px(0, 32, grn);               /* left edge of the world */
    gfx_px(0, 0, blu);                /* corner, off the glass */
    gfx_blit565(panel, 0, 0);

    /* World (32,32) opens at panel (9+224, 9+224) = (233,233)
     * and owns a 7x7 block. */
    int px = GFX_OFFSET + 32 * GFX_SCALE;
    int py = GFX_OFFSET + 32 * GFX_SCALE;
    CHECK(px == 233 && py == 233, "anchor moved to %d,%d", px, py);
    CHECK(panel[py * GFX_PANEL + px] == rgb565(red), "world pixel missed");
    CHECK(panel[(py + 6) * GFX_PANEL + (px + 6)] == rgb565(red),
          "7x block clipped short");
    CHECK(panel[py * GFX_PANEL + (px + 7)] != rgb565(red),
          "7x block spilled into the next world pixel");

    /* Left edge of the square is world x=0. One pixel further left is
     * still on the glass and must be the background, not black and not
     * a smear of the edge pixel. */
    int edge = GFX_OFFSET;            /* 9 */
    CHECK(panel[233 * GFX_PANEL + edge] == rgb565(grn), "left edge pixel");
    CHECK(on_glass(edge - 1, 233), "expected the cap to be on the glass");
    CHECK(panel[233 * GFX_PANEL + (edge - 1)] == rgb565(bg),
          "cap between square and bezel is not the clear colour");

    /* Top of the circle, above the square: background, not off.
     * The left and right rims are the same idea along the diameter. */
    CHECK(on_glass(233, 0), "top centre should be on the glass");
    CHECK(panel[0 * GFX_PANEL + 233] == rgb565(bg), "top cap went dark");
    CHECK(on_glass(0, 233) && on_glass(GFX_PANEL - 1, 233), "diameter rims");
    CHECK(panel[233 * GFX_PANEL + 0] == rgb565(bg), "left rim not filled");
    CHECK(panel[233 * GFX_PANEL + (GFX_PANEL - 1)] == rgb565(bg),
          "right rim not filled");

    /* Corner of the framebuffer is not on the round panel. */
    CHECK(!on_glass(0, 0), "framebuffer corner should be off the glass");
    CHECK(panel[0] == 0, "corner pixel was lit");

    /* The world's own corner maps to panel (9,9), which is outside the
     * circle, so that blue pixel must not light anything. */
    CHECK(!on_glass(9, 9), "world corner should fall off the glass");
    CHECK(panel[9 * GFX_PANEL + 9] == 0, "off-glass world pixel was drawn");

    /* Drift still moves the square; the cap follows it. */
    gfx_blit565(panel, 3, 0);
    CHECK(panel[233 * GFX_PANEL + (edge + 3)] == rgb565(grn),
          "drift did not move the world");
    CHECK(panel[233 * GFX_PANEL + (edge + 2)] == rgb565(bg),
          "gap left by drift was not filled");

    free(panel);
}

int main(void)
{
    struct { const char *name; void (*fn)(void); } ts[] = {
        { "round panel mapping", test_round_mapping },
    };
    for (unsigned i = 0; i < sizeof ts / sizeof ts[0]; i++) {
        int before = fails;
        ts[i].fn();
        printf("%s  %s\n", (fails == before) ? "pass" : "FAIL", ts[i].name);
    }
    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
