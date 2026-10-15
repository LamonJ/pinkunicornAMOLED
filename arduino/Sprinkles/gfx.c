#include "gfx.h"
#include <math.h>
#include <string.h>

rgb_t  gfx_fb[GFX_W * GFX_W];
rgb_t  gfx_alt[GFX_W * GFX_W];
rgb_t *gfx_dst = gfx_fb;

void gfx_use_alt(int on)
{
    if (on) {
        for (int i = 0; i < GFX_W * GFX_W; i++) gfx_alt[i] = GFX_EMPTY;
        gfx_dst = gfx_alt;
    } else {
        gfx_dst = gfx_fb;
    }
}

void gfx_rot_blit(double tx, double ty, double ang, double cx0, double cy0)
{
    double cs = cos(ang), sn = sin(ang);
    for (int y = 0; y < GFX_W; y++) {
        for (int x = 0; x < GFX_W; x++) {
            double dx = (x + 0.5) - tx, dy = (y + 0.5) - ty;
            double u =  cs * dx + sn * dy + cx0;
            double v = -sn * dx + cs * dy + cy0;
            int su = (int)floor(u), sv = (int)floor(v);
            if (su < 0 || sv < 0 || su >= GFX_W || sv >= GFX_W) continue;
            rgb_t c = gfx_alt[sv * GFX_W + su];
            if (c == GFX_EMPTY) continue;
            gfx_fb[y * GFX_W + x] = c;
        }
    }
}
double gfx_dim = 1.0;
rgb_t  gfx_silh = 0;
int    gfx_silh_on = 0;

int gfx_round(double x) { return (int)floor(x + 0.5); }

/* Dimming: red and green scale linearly, blue gets a 1.4x boost before
 * clamping. That boost is why lights-out reads as moonlight and not as grey. */
static rgb_t tone(rgb_t c)
{
    if (gfx_silh_on) return gfx_silh;
    if (gfx_dim >= 1.0) return c;
    double b_mul = gfx_dim * 1.4;
    if (b_mul > 1.0) b_mul = 1.0;
    int r = gfx_round(((c >> 16) & 0xFF) * gfx_dim);
    int g = gfx_round(((c >>  8) & 0xFF) * gfx_dim);
    int b = gfx_round(( c        & 0xFF) * b_mul);
    return (rgb_t)((r << 16) | (g << 8) | b);
}

void gfx_clear(rgb_t c)
{
    rgb_t v = c;
    for (int i = 0; i < GFX_W * GFX_W; i++) gfx_dst[i] = v;
}

void gfx_px(int x, int y, rgb_t c)
{
    if (x < 0 || y < 0 || x >= GFX_W || y >= GFX_W) return;
    gfx_dst[y * GFX_W + x] = tone(c);
}

/* Matches canvas fillRect: truncate toward zero, clip to the surface. */
void gfx_rect(double x, double y, double w, double h, rgb_t c)
{
    int xi = (int)x, yi = (int)y, wi = (int)w, hi = (int)h;
    rgb_t t = tone(c);
    for (int j = 0; j < hi; j++) {
        int yy = yi + j;
        if (yy < 0 || yy >= GFX_W) continue;
        for (int i = 0; i < wi; i++) {
            int xx = xi + i;
            if (xx < 0 || xx >= GFX_W) continue;
            gfx_dst[yy * GFX_W + xx] = t;
        }
    }
}

/* Integer scanline ellipse, hard edges, no anti-aliasing anywhere. */
void gfx_ellipse(double cx, double cy, double rx, double ry, rgb_t c)
{
    rgb_t t = tone(c);
    int y0 = (int)ceil(cy - ry), y1 = (int)floor(cy + ry);
    for (int y = y0; y <= y1; y++) {
        double k = 1.0 - ((y + 0.5 - cy) * (y + 0.5 - cy)) / (ry * ry);
        if (k <= 0) continue;
        double dx = sqrt(k) * rx;
        int xa = gfx_round(cx - dx), xb = gfx_round(cx + dx);
        int w = xb - xa; if (w < 1) w = 1;
        if (y < 0 || y >= GFX_W) continue;
        for (int i = 0; i < w; i++) {
            int xx = xa + i;
            if (xx < 0 || xx >= GFX_W) continue;
            gfx_dst[y * GFX_W + xx] = t;
        }
    }
}

void gfx_egg(double cx, double cy, double rx, double ry, double taper, rgb_t c)
{
    rgb_t t = tone(c);
    int y0 = (int)ceil(cy - ry), y1 = (int)floor(cy + ry);
    for (int y = y0; y <= y1; y++) {
        double v = (y + 0.5 - cy) / ry;              /* -1 top .. +1 bottom */
        double k = 1.0 - v * v;
        if (k <= 0) continue;
        double half = sqrt(k) * rx * (1.0 + taper * v);
        if (half < 0.5) continue;
        int xa = gfx_round(cx - half), xb = gfx_round(cx + half);
        int w = xb - xa; if (w < 1) w = 1;
        if (y < 0 || y >= GFX_W) continue;
        for (int i = 0; i < w; i++) {
            int xx = xa + i;
            if (xx < 0 || xx >= GFX_W) continue;
            gfx_dst[y * GFX_W + xx] = t;
        }
    }
}

void gfx_ring(double cx, double cy, double r, rgb_t c)
{
    for (int a = 0; a < 360; a += 3) {
        double d = a * M_PI / 180.0;
        gfx_px((int)(cx + cos(d) * r), (int)(cy + sin(d) * r), c);
    }
}

void gfx_rays(double cx, double cy, double r0, double r1, rgb_t c)
{
    for (int i = 0; i < 8; i++) {
        double a = i * M_PI / 4.0 + 0.4;
        /* r starts at the fractional r0 and steps by 1 — matching the
         * reference exactly. Truncating r0 first shifts every sample. */
        for (double r = r0; r < r1; r += 1.0)
            gfx_px((int)(cx + cos(a) * r), (int)(cy + sin(a) * r), c);
    }
}

void gfx_ear(double bx, double yb, int h, int w, int d, rgb_t c)
{
    double apex = bx + d * gfx_round(w * 0.55);
    for (int j = 0; j < h; j++) {
        double t = (h > 1) ? (double)j / (h - 1) : 0.0;
        double l = apex + (bx - w / 2.0 - apex) * t;
        double r = apex + (bx + w / 2.0 - apex) * t;
        int x0 = gfx_round(l), x1 = gfx_round(r);
        int wid = x1 - x0 + 1; if (wid < 1) wid = 1;
        gfx_rect(x0, yb - h + 1 + j, wid, 1, c);
    }
}

static rgb_t pal_lookup(const gfx_pal *pal, char ch)
{
    for (const gfx_pal *p = pal; p->ch; p++) if (p->ch == ch) return p->col;
    return 0xFFFFFFFF;                       /* sentinel: not in palette */
}

void gfx_map(double x, double y, const char **rows, int n, const gfx_pal *pal)
{
    for (int j = 0; j < n; j++) {
        int len = (int)strlen(rows[j]);
        for (int i = 0; i < len; i++) {
            char ch = rows[j][i];
            if (ch == '.') continue;
            rgb_t c = pal_lookup(pal, ch);
            if (c != 0xFFFFFFFF) gfx_px((int)(x + i), (int)(y + j), c);
        }
    }
}

static int sprite_on(const char **rows, int n, const gfx_pal *pal, int i, int j)
{
    if (j < 0 || j >= n || i < 0) return 0;
    int len = (int)strlen(rows[j]);
    if (i >= len) return 0;
    char ch = rows[j][i];
    if (ch == '.') return 0;
    return pal_lookup(pal, ch) != 0xFFFFFFFF;
}

/* 1 px outline on every transparent pixel orthogonally touching a set one.
 * This is what lets props read against her pink coat. */
void gfx_mapo(double x, double y, const char **rows, int n, const gfx_pal *pal, rgb_t line)
{
    int wmax = 0;
    for (int j = 0; j < n; j++) { int l = (int)strlen(rows[j]); if (l > wmax) wmax = l; }
    for (int j = -1; j <= n; j++)
        for (int i = -1; i <= wmax; i++)
            if (!sprite_on(rows, n, pal, i, j) &&
                ( sprite_on(rows, n, pal, i - 1, j) || sprite_on(rows, n, pal, i + 1, j) ||
                  sprite_on(rows, n, pal, i, j - 1) || sprite_on(rows, n, pal, i, j + 1) ))
                gfx_px((int)(x + i), (int)(y + j), line);
    gfx_map(x, y, rows, n, pal);
}

void gfx_map2(double x, double y, const char **rows, int n, const gfx_pal *pal, rgb_t line)
{
    int wmax = 0;
    for (int j = 0; j < n; j++) { int l = (int)strlen(rows[j]); if (l > wmax) wmax = l; }
    for (int j = -1; j <= n; j++)
        for (int i = -1; i <= wmax; i++)
            if (!sprite_on(rows, n, pal, i, j) &&
                ( sprite_on(rows, n, pal, i - 1, j) || sprite_on(rows, n, pal, i + 1, j) ||
                  sprite_on(rows, n, pal, i, j - 1) || sprite_on(rows, n, pal, i, j + 1) ))
                gfx_rect(x + i * 2, y + j * 2, 2, 2, line);
    for (int j = 0; j < n; j++) {
        int len = (int)strlen(rows[j]);
        for (int i = 0; i < len; i++) {
            char ch = rows[j][i];
            if (ch == '.') continue;
            rgb_t c = pal_lookup(pal, ch);
            if (c != 0xFFFFFFFF) gfx_rect(x + i * 2, y + j * 2, 2, 2, c);
        }
    }
}

double gfx_top_at(double cy, double rx, double ry, double dx)
{
    double k = 1.0 - (dx / rx) * (dx / rx);
    if (k < 0) k = 0;
    return cy - ry * sqrt(k);
}

void gfx_drift(double minutes, int *dx, int *dy)
{
    *dx = gfx_round(sin(minutes * 2 * M_PI / 17.0) * 8);
    *dy = gfx_round(cos(minutes * 2 * M_PI / 23.0) * 8);
}

void gfx_blit565(uint16_t *panel, int drift_x, int drift_y)
{
    memset(panel, 0, (size_t)GFX_PANEL * GFX_PANEL * sizeof(uint16_t));
    for (int wy = 0; wy < GFX_W; wy++) {
        for (int wx = 0; wx < GFX_W; wx++) {
            rgb_t c = gfx_fb[wy * GFX_W + wx];
            uint16_t p = (uint16_t)((((c >> 16) & 0xF8) << 8) |
                                    (((c >>  8) & 0xFC) << 3) |
                                    (( c        & 0xF8) >> 3));
            int px0 = GFX_OFFSET + drift_x + wx * GFX_SCALE;
            int py0 = GFX_OFFSET + drift_y + wy * GFX_SCALE;
            for (int sy = 0; sy < GFX_SCALE; sy++) {
                int y = py0 + sy;
                if (y < 0 || y >= GFX_PANEL) continue;
                uint16_t *row = panel + (size_t)y * GFX_PANEL;
                for (int sx = 0; sx < GFX_SCALE; sx++) {
                    int x = px0 + sx;
                    if (x < 0 || x >= GFX_PANEL) continue;
                    row[x] = p;
                }
            }
        }
    }
}
