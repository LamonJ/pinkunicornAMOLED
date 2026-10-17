/* gfx.h — the 64x64 pixel world.
 *
 * Everything Sprinkles draws goes through here. The maths deliberately
 * reproduces the JavaScript prototype bit-for-bit, including its rounding
 * quirks, so the golden frames in golden-frames/ are an exact test.
 *
 * Colour is stored as 0x00RRGGBB, not RGB565. That costs 16 KB for the
 * world buffer and buys exact comparability with the reference frames;
 * conversion to RGB565 happens once, in the blit. Do not move the
 * quantisation earlier.
 */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GFX_W 64                      /* world is GFX_W x GFX_W        */
#define GFX_SCALE 7                   /* -> 448 px on the panel        */
#define GFX_PANEL 466                 /* Waveshare 1.75" AMOLED        */
/* 466 is even, so the circle is centred on pixel 233 and the last row
 * is one pixel short of a perfect diameter. That is the panel, not a crop. */
#define GFX_CIRCLE_C (GFX_PANEL / 2)  /* 233 */
#define GFX_CIRCLE_R (GFX_PANEL / 2)  /* radius 233, diameter 466       */
#define GFX_OFFSET ((GFX_PANEL - GFX_W * GFX_SCALE) / 2)   /* == 9 */

typedef uint32_t rgb_t;               /* 0x00RRGGBB */

extern rgb_t  gfx_fb[GFX_W * GFX_W];

/* Secondary target, used to render her flat before spinning the bitmap.
 * GFX_EMPTY marks untouched pixels — the equivalent of canvas transparency. */
#define GFX_EMPTY 0xFF000000u
extern rgb_t  gfx_alt[GFX_W * GFX_W];
extern rgb_t *gfx_dst;                /* where primitives write */
void gfx_use_alt(int on);             /* switch target, clearing alt on entry */

/* Nearest-neighbour rotation of the alt buffer into the main one. Reproduces
 * canvas translate/rotate/drawImage with smoothing off: each destination
 * pixel centre is mapped back through the inverse transform and floored. */
void gfx_rot_blit(double tx, double ty, double ang, double cx0, double cy0);
extern double gfx_dim;                /* 1.0 = full brightness       */
extern rgb_t  gfx_silh;               /* !=0 forces every pixel to it */
extern int    gfx_silh_on;

typedef struct { char ch; rgb_t col; } gfx_pal;   /* terminated by ch==0 */

/* JS Math.round: floor(x + 0.5). Differs from C round() on negatives,
 * which matters for lean/rotation offsets. */
int gfx_round(double x);

void gfx_clear(rgb_t c);
void gfx_px(int x, int y, rgb_t c);
void gfx_rect(double x, double y, double w, double h, rgb_t c);
void gfx_ellipse(double cx, double cy, double rx, double ry, rgb_t c);

/* Ovoid: an ellipse whose half-width is skewed down the vertical axis, so it
 * is narrow at the top and broad below centre. A plain ellipse reads as an
 * egg standing on its fat end - which is to say, upside down.
 * taper 0 == gfx_ellipse; 0.28 looks like an egg. */
void gfx_egg(double cx, double cy, double rx, double ry, double taper, rgb_t c);
void gfx_ring(double cx, double cy, double r, rgb_t c);
void gfx_rays(double cx, double cy, double r0, double r1, rgb_t c);

/* Pointed cat ear. Apex sits at bx + d*round(w*0.55) — the outward lean is
 * what reads as "cat" rather than "pig". Do not symmetrise it. */
void gfx_ear(double bx, double yb, int h, int w, int d, rgb_t c);

/* Origins are doubles on purpose. Truncation happens per pixel, matching the
 * reference's `x|0` inside its plot call. Truncating the origin first differs
 * for negative coordinates — which is exactly what happens when an effect
 * drifts off the left edge. */
void gfx_map (double x, double y, const char **rows, int n, const gfx_pal *pal);
void gfx_mapo(double x, double y, const char **rows, int n, const gfx_pal *pal, rgb_t line);
void gfx_map2(double x, double y, const char **rows, int n, const gfx_pal *pal, rgb_t line);

/* y of the body outline at horizontal offset dx. Anchors ears/horn/forelock. */
double gfx_top_at(double cy, double rx, double ry, double dx);

/* Burn-in drift: two mismatched slow cycles through the 9 px of slack. */
void gfx_drift(double minutes, int *dx, int *dy);

/* Nearest-neighbour 7x upscale into the round RGB565 panel.
 * panel must hold GFX_PANEL*GFX_PANEL uint16_t.
 *
 * The world is a square. The glass is the Waveshare circle. Pixels outside
 * that circle are left off (the corners are not on the panel). The gap
 * between the 448 px square and the bezel is filled with the colour last
 * passed to gfx_clear, so the scene meets the round edge instead of sitting
 * in a smaller square. */
void gfx_blit565(uint16_t *panel, int drift_x, int drift_y);

#ifdef __cplusplus
}
#endif

#endif
