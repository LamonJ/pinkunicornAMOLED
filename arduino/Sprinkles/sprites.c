#include "sprites.h"
#include <math.h>

const rgb_t RAIN[6] = { C_R1, C_R2, C_R3, C_R4, C_R5, C_R6 };

const feat_t FEAT[ST_COUNT] = {
    /*                rx  ry ear horn tail fore mane whisk sprout eye */
    /* egg     */ {    0,  0,  0,  0,   0,   0,   0,    0,    0,   0 },
    /* blob    */ {    9,  8,  0,  0,   1,   0,   0,    0,    1,   3 },
    /* kitten  */ {   12, 10,  4,  4,   3,   0,   0,    1,    0,   3 },
    /* unicorn */ {   16, 13,  5,  6,   5,   5,   4,    1,    0,   4 },
    /* royal   */ {   19, 15,  6,  8,   6,   5,   6,    1,    0,   5 },
};

/* ---- string sprites --------------------------------------------------- */
static const char *EYE_UP[]   = { ".#.", "#.#" };     /* ^ ^  happy/asleep */
static const char *EYE_DOWN[] = { "#.#", ".#." };     /* v v  sad          */
static const char *MOUTH_W[]  = { ".#.#.", "#.#.#" }; /* cat mouth         */

static const char *ICON_HEART[] = { ".#.#.", "#####", ".###.", "..#.." };
static const char *ICON_CAKE[]  = { "..#..", ".###.", "#####", ".###.", ".###." };
static const char *ICON_MOON[]  = { "..##.", ".##..", ".#...", ".##..", "..##." };

static const char *CUPCAKE[] = {
    "....c....", "...fFf...", "..fFFFf..", ".fFFFFFf.", "fFFFFFFFf",
    ".fffffff.", "wWwWwWwWw", ".wWwWwWw.", ".wWwWwWw.", "..wWwWw..", "...www..."
};
static const char *ICON_CUP[] = {
    "....c....", "..fFFFf..", ".fFFFFFf.", ".wWwWwWw.", ".wWwWwWw.",
    "..wWwWw..", "...www..."
};
static const char *BROOM[] = {
    "......hh", ".....hh.", "....hh..", "...hh...", "..bbbb..",
    ".bbbbbb.", "bbbbbbbb", "b.bb.bb.", "b..b..b."
};
static const char *POOP[] = {
    "...##...", "..####..", ".####...", "..#####.", ".#######",
    "########", ".######."
};
static const char *SPARK[] = { ".#.", "###", ".#." };
static const char *ZED[]   = { "###", "..#", ".#.", "###" };
static const char *NOTE[]  = { ".##", ".##", ".#.", "##." };

static const gfx_pal PAL_CUP[] = {
    { 'c', C_CHERRY }, { 'f', C_FROST }, { 'F', C_FROSTHI },
    { 'w', C_CUP },    { 'W', C_CUPHI }, { 0, 0 }
};
static const gfx_pal PAL_BROOM[] = {
    { 'h', C_R6 }, { 'b', C_STAR }, { 0, 0 }
};

static gfx_pal one(char ch, rgb_t c) { gfx_pal p = { ch, c }; return p; }
static void map1(double x, double y, const char **rows, int n, rgb_t c)
{
    gfx_pal pal[2]; pal[0] = one('#', c); pal[1] = one(0, 0);
    gfx_map(x, y, rows, n, pal);
}
static void mapo1(double x, double y, const char **rows, int n, rgb_t c, rgb_t line)
{
    gfx_pal pal[2]; pal[0] = one('#', c); pal[1] = one(0, 0);
    gfx_mapo(x, y, rows, n, pal, line);
}

/* ---- the unicorn cat -------------------------------------------------- */
cat_geom_t spr_cat(stage_t st, mood_t m, double t,
                   double lean, double hop, int wig)
{
    const feat_t *F = &FEAT[st];
    int rx = F->rx, ry = F->ry;
    double cx = 32 + lean, cy = PET_CY - hop;
    int droop = (m == MOOD_SAD) ? 1 : 0;

    /* tail: a nub at first, a full rainbow plume by the end */
    static const int TAIL[6][2] = { {0,0},{3,-1},{5,-3},{7,-6},{7,-9},{6,-12} };
    int sw = gfx_round(sin(t / 320.0) * 2);
    for (int i = 0; i < F->tail; i++) {
        rgb_t c = (F->tail == 1) ? C_COATSH : RAIN[i];
        gfx_rect(cx + rx - 3 + TAIL[i][0] + (i > 3 ? sw : 0),
                 cy + 2 + TAIL[i][1], 3, 3, c);
    }

    /* horn, behind the head so its base tucks in */
    if (F->horn) {
        int hb = gfx_round(gfx_top_at(cy, rx, ry, 0)) + 2;
        for (int j = 0; j < F->horn; j++) {
            double f = (double)j / F->horn;
            int w = (f < 0.25) ? 4 : (f < 0.5) ? 3 : (f < 0.8) ? 2 : 1;
            if (F->horn <= 4 && w > 3) w = 3;
            gfx_rect(cx - (w >> 1), hb - j, w, 1, C_HORN);
            if (j % 2 == 1) {
                int m2 = (w < 1) ? 1 : w;
                gfx_px((int)(cx - (w >> 1) + ((j >> 1) % m2)), hb - j, C_HORNSH);
            }
        }
    }

    /* body */
    gfx_ellipse(cx, cy, rx, ry, C_COATSH);
    gfx_ellipse(cx, cy - 1, rx - 1, ry - 1, C_COAT);

    /* the blob's sprout — a promise of the horn to come */
    if (F->sprout) {
        int sb = gfx_round(gfx_top_at(cy, rx, ry, 0)) + 1;
        gfx_rect(cx, sb - 3, 1, 3, C_R4);
        map1(cx - 1, sb - 5, SPARK, 3, C_R1);
    }

    /* cat ears, over the body so the full triangle reads */
    if (F->ear) {
        int eo = gfx_round(rx * 0.55);
        int H = F->ear + 2, Wd = F->ear;
        for (int s = 0; s < 2; s++) {
            int d = s ? 1 : -1;
            double bx = cx + d * eo + (d > 0 ? wig : -wig);
            double yb = gfx_round(gfx_top_at(cy, rx, ry, eo)) + 2 + droop;
            gfx_ear(bx, yb,     H,     Wd,     d, C_COATSH);
            gfx_ear(bx, yb - 1, H - 2, Wd - 2, d, C_BLUSH);
        }
    }

    /* rainbow forelock between the ears */
    for (int i = 0; i < F->fore; i++) {
        double fx = cx - (F->fore - 1) + i * 2;
        double fy = gfx_round(gfx_top_at(cy, rx - 1, ry - 1, fabs(fx - cx))) - 1;
        gfx_rect(fx, fy, 2, 3 + ((i + 1) % 2), RAIN[i]);
    }
    /* mane sweeping down the left */
    for (int i = 0; i < F->mane; i++) {
        double ang = M_PI * 0.72 + i * 0.10;
        gfx_rect(cx + cos(ang) * (rx - 2), cy - 1 - sin(ang) * (ry - 2), 3, 2, RAIN[i]);
    }

    /* paws */
    int po = gfx_round(rx * 0.50);
    for (int s = 0; s < 2; s++) {
        int d = s ? po : -po;
        gfx_ellipse(cx + d, cy + ry - 1, rx > 10 ? 4 : 3, rx > 10 ? 3 : 2, C_COATSH);
        gfx_rect(cx + d - 1, cy + ry - 2, 3, 1, C_COATHI);
    }

    /* face */
    int g = gfx_round(rx * 0.30); if (g < 3) g = 3;
    int ey = (int)(cy - gfx_round(ry * 0.18));
    int eh = (F->eye < 4) ? 2 : 3;
    int blink = fmod(t, 4600.0) < 130.0;

    if (m == MOOD_SLEEP || blink) {
        map1(cx - g - 1, ey + 1, EYE_UP, 2, C_DARK);
        map1(cx + g - 1, ey + 1, EYE_UP, 2, C_DARK);
    } else if (m == MOOD_HAPPY) {
        map1(cx - g - 1, ey, EYE_UP, 2, C_DARK);
        map1(cx + g - 1, ey, EYE_UP, 2, C_DARK);
    } else if (m == MOOD_SAD) {
        map1(cx - g - 1, ey, EYE_DOWN, 2, C_DARK);
        map1(cx + g - 1, ey, EYE_DOWN, 2, C_DARK);
    } else {
        gfx_rect(cx - g - 1, ey, 2, eh, C_DARK);
        gfx_rect(cx + g,     ey, 2, eh, C_DARK);
        gfx_px((int)(cx - g - 1), ey, C_WHITE);
        gfx_px((int)(cx + g),     ey, C_WHITE);
    }

    int bo = gfx_round(rx * 0.60);
    gfx_rect(cx - bo - 1, ey + eh, 3, 1, C_BLUSH);
    gfx_rect(cx + bo - 1, ey + eh, 3, 1, C_BLUSH);

    int my = (int)(cy + gfx_round(ry * 0.20));
    gfx_px((int)cx, my - 1, C_CHERRY);
    if (m == MOOD_EAT) {
        gfx_rect(cx - 2, my, 5, 3, C_DARK);
        gfx_rect(cx - 1, my + 1, 3, 1, C_CHERRY);
    } else if (m != MOOD_SLEEP) {
        map1(cx - 2, my, MOUTH_W, 2, C_DARK);
    }

    if (F->whisk) {
        int wo = gfx_round(rx * 0.66);
        gfx_rect(cx - wo - 3, my, 3, 1, C_COATHI);
        gfx_rect(cx + wo + 1, my, 3, 1, C_COATHI);
    }
    if (m == MOOD_SAD) {
        int dy = (int)fmod(t / 380.0, 6.0);
        gfx_rect(cx - g - 2, ey + 2 + dy, 1, 2, C_R5);
    }

    cat_geom_t out = { cx, cy, rx, ry };
    return out;
}

/* ---- egg -------------------------------------------------------------- */
static const char *CRACK1[] = { "..#", ".#.", ".#.", "#..", ".#.", ".#." };
static const char *CRACK2[] = { ".#.", "#..", "##.", ".#.", "..#" };
static const char *CRACK3[] = { "#..", ".#.", ".##", "..#", ".#." };

void spr_egg(double p, double t, int nudged)
{
    double rock = 1 + p * 2.2 + (nudged ? 2.5 : 0);
    double ex = 32 + gfx_round(sin(t / (nudged ? 90.0 : 260.0)) * rock);
    double cy = 36 + gfx_round(sin(t / 900.0));

/* Taper chosen by sweeping it and looking, with a measurement as the check:
 * the widest row lands 59% of the way down with an 8px rounded crown.
 *
 * Both bounds matter. Below about 0.30 the widest point creeps back ABOVE
 * centre and it reads as an egg balanced on its fat end. Above about 0.45
 * the crown sharpens to a point and it reads as a water droplet. The
 * measurement alone will happily walk you into the droplet. */
#define EGG_TAPER 0.35
    gfx_egg(ex, cy,     10, 14, EGG_TAPER, C_EGGB);
    gfx_egg(ex, cy - 1, 10, 13, EGG_TAPER, C_EGGA);
    gfx_ellipse(ex - 3, cy - 5, 2, 3, C_EGGHI);

    static const int saw[4] = { 0, -1, -2, -1 };
    for (int i = -9; i <= 9; i++) {
        gfx_px((int)ex + i, (int)cy + 3 + saw[(i + 12) % 4], C_R6);
        gfx_px((int)ex + i, (int)cy + 4 + saw[(i + 12) % 4], C_R5);
    }
    map1(ex + 4, cy - 10, SPARK, 3, C_HORN);
    map1(ex - 8, cy + 6,  SPARK, 3, C_HORN);

    if (p > 0.30) map1(ex + 2, cy - 12, CRACK1, 6, C_DARK);
    if (p > 0.58) map1(ex - 8, cy - 3,  CRACK2, 5, C_DARK);
    if (p > 0.82) map1(ex + 1, cy + 6,  CRACK3, 5, C_DARK);
}

/* ---- meters ----------------------------------------------------------- */
static void pips(int x, int y, int n, rgb_t col, int charge, double t)
{
    for (int i = 0; i < 4; i++) {
        rgb_t c = (i < n) ? col : C_DIM;
        if (charge && i == n) c = (sin(t / 520.0) > 0) ? C_DIM : col;
        gfx_rect(x + i * 4, y, 3, 3, c);
    }
}

void spr_hud(const pet_t *p, int energy_only, double t)
{
    if (p->stage == ST_EGG) return;
    if (energy_only) {
        /* napping: the energy bar alone, centred, so she can watch it fill */
        map1(21, 9, ICON_MOON, 5, C_R5);
        pips(27, 10, p->energy, C_R5, 1, t);
        return;
    }
    map1(21, 2, ICON_HEART, 4, C_R1);  pips(27, 2,  p->happy,  C_R1, 0, t);
    map1(10, 9, ICON_CAKE,  5, C_R4);  pips(16, 10, p->hunger, C_R4, 0, t);
    map1(33, 9, ICON_MOON,  5, C_R5);  pips(39, 10, p->energy, C_R5, 0, t);
}

/* ---- props ------------------------------------------------------------ */
void spr_mess(const pet_t *p)
{
    for (int i = 0; i < p->mess; i++) {
        int x = p->mess_x[i], y = 53;
        mapo1(x - 4, y - 6, POOP, 7, C_POO, C_DARK);
        gfx_rect(x - 1, y - 6, 2, 1, C_POOHI);
        gfx_rect(x - 2, y - 4, 2, 1, C_POOHI);
        gfx_rect(x - 3, y - 1, 2, 1, C_POOHI);
        gfx_rect(x - 3, y,     6, 1, C_POOLO);
        map1(x + 4, y - 7, SPARK, 3, C_STAR);   /* still a bit sparkly */
        gfx_px(x - 6, y - 3, C_R1);
    }
}

void spr_cupcake(double x, double y, int bites)
{
    if (bites < 0) bites = 0;
    if (bites > 2) bites = 2;
    gfx_mapo(x, y + bites * 2, CUPCAKE + bites * 2,
             11 - bites * 2, PAL_CUP, C_DARK);
}

void spr_broom(double x, double y)
{
    gfx_mapo(x, y, BROOM, 9, PAL_BROOM, C_DARK);
}

void spr_buttons(const pet_t *p, int64_t now_ms, double t,
                 int pressed_feed, int pressed_clean)
{
    struct { int x; int want; int hot; int feed; } b[2] = {
        { BTN_FEED_X,
          (p->hunger <= 1 || pet_pending_grow(p, now_ms) != ST_COUNT),
          pressed_feed, 1 },
        { BTN_CLEAN_X, p->mess > 0, pressed_clean, 0 }
    };
    for (int i = 0; i < 2; i++) {
        int bob = b[i].want ? gfx_round(sin(t / 300.0) * 1) : 0;
        double y = BTN_Y + bob;
        /* Always full brightness — only the bob marks the wanted one. */
        gfx_ellipse(b[i].x, y,     12, 12, C_DARK);              /* rim   */
        gfx_ellipse(b[i].x, y,     11, 11, C_COATSH);            /* under */
        gfx_ellipse(b[i].x, y - 1, 10, 10, b[i].hot ? C_BLUSH : C_COAT);
        gfx_ellipse(b[i].x - 5, y - 7, 3, 2, C_COATHI);          /* gloss */
        if (b[i].feed)
            gfx_mapo(b[i].x - 4, y - 3, ICON_CUP, 7, PAL_CUP, C_DARK);
        else
            gfx_mapo(b[i].x - 4, y - 4, BROOM, 9, PAL_BROOM, C_DARK);
    }
}

/* ---- effects ---------------------------------------------------------- */
void spr_zzz(double t, cat_geom_t g)
{
    int n = (int)fmod(floor(t / 700.0), 3.0);
    for (int i = 0; i <= n; i++)
        map1(g.cx + g.rx - 1 + i * 3, g.cy - g.ry - 2 - i * 4, ZED, 4, C_R5);
}

void spr_horn_glow(double t, stage_t st, cat_geom_t g)
{
    int horn = FEAT[st].horn ? FEAT[st].horn : 2;
    int tip = gfx_round(gfx_top_at(g.cy, g.rx, g.ry, 0)) + 2 - horn;
    double ph = (sin(t / 1100.0) + 1) / 2;
    if (ph > 0.30) map1(g.cx - 1, tip - 2, SPARK, 3, C_HORN);
    if (ph > 0.72) { gfx_px(g.cx - 3, tip - 1, C_R3); gfx_px(g.cx + 3, tip - 1, C_R3); }
}

void spr_hearts(double anim_t, double base_t, cat_geom_t g)
{
    for (int i = 0; i < 3; i++) {
        double ph = fmod((anim_t - base_t) / 1500.0 + i * 0.33, 1.0);
        if (ph < 0) ph += 1.0;
        map1(g.cx - 9 + i * 9, g.cy - g.ry - 1 - gfx_round(ph * 8),
             ICON_HEART, 4, C_R1);
    }
}

void spr_sparkles(double t, cat_geom_t g)
{
    static const int sx_sign[4] = { -1, 1, -1, 1 };
    const rgb_t cols[4] = { C_R3, C_R5, C_R1, C_R6 };
    for (int i = 0; i < 4; i++) {
        double ph = fmod(t / 240.0 + i * 0.25, 1.0);
        double sx = g.cx + sx_sign[i] * (g.rx + 2 + (i % 2) * 3);
        double sy = g.cy - 2 - gfx_round(ph * 18);
        if (ph < 0.5) map1(sx - 1, sy - 1, SPARK, 3, cols[i]);
        else gfx_px((int)sx, (int)sy, cols[i]);
    }
}

void spr_notes(double t, cat_geom_t g)
{
    for (int i = 0; i < 2; i++) {
        double ph = fmod(t / 340.0 + i * 0.5, 1.0);
        map1(g.cx + (g.rx + 3) * (i ? 1 : -1) - 1,
             g.cy - 8 - gfx_round(ph * 12), NOTE, 4, C_R6);
    }
}

/* She shimmers when she's ready to grow — the cue to reach for a cupcake. */
void spr_grow_shimmer(double t, cat_geom_t g)
{
    const rgb_t cols[4] = { C_R3, C_R5, C_R6, C_R1 };
    for (int i = 0; i < 4; i++) {
        double a = (i / 4.0) * 2 * M_PI + t / 650.0;
        double r = g.rx + 6;
        map1(g.cx + cos(a) * r - 1, g.cy + sin(a) * r * 0.7 - 1, SPARK, 3, cols[i]);
    }
}

/* Screen asleep: almost every pixel off, which on AMOLED costs almost
 * nothing. One slow breath of light so she reads as asleep, not dead. */
/* Resting. Almost every pixel is off, which on an AMOLED costs almost
 * nothing - but the old version was a single pixel, and a single pixel on a
 * 45 mm circle does not say "tap me", it says "possibly broken". A slow
 * breathing sparkle plus a faint ring reads as asleep-and-waiting while
 * still lighting only a few dozen pixels. */
void spr_resting(double t)
{
    double ph = (sin(t / 1900.0) + 1) / 2;          /* 0..1, ~4 s cycle */

    /* the sparkle at her horn, growing and shrinking */
    map1(31, 29, (const char *[]){ ".#.", "###", ".#." }, 3, C_HORN);
    if (ph > 0.35) {
        gfx_px(32, 27, C_HORNSH);
        gfx_px(32, 33, C_HORNSH);
        gfx_px(29, 30, C_HORNSH);
        gfx_px(35, 30, C_HORNSH);
    }
    if (ph > 0.70) {
        gfx_px(32, 26, C_HORN);
        gfx_px(32, 34, C_HORN);
        gfx_px(28, 30, C_HORN);
        gfx_px(36, 30, C_HORN);
    }
    /* a faint ring, so the whole face reads as the target */
    if (ph > 0.55) gfx_ring(32, 31, 20 + ph * 3, 0x2A1F14);
}

/* ---- animations ------------------------------------------------------- */

cat_geom_t spr_roll(stage_t st, double t, double e)
{
    double ang = 0, lift = 0, squash = 0;
    if (e < 200)        squash = -1;                    /* crouch          */
    else if (e < 1300) { double p = (e - 200) / 1100.0;
                         ang = p * 2 * M_PI;
                         lift = gfx_round(sin(M_PI * p) * 9); }
    else                squash = -1;                    /* landing bounce  */

    /* Render her flat, then spin the bitmap — nearest-neighbour, so she
     * stays crisp at every angle. 4096 samples, free on an S3. */
    gfx_use_alt(1);
    cat_geom_t g = spr_cat(st, MOOD_HAPPY, t, 0, squash, 0);
    gfx_use_alt(0);
    gfx_rot_blit(g.cx + 0.5, g.cy + 0.5 - lift, ang, g.cx + 0.5, g.cy + 0.5);

    if (e > 200 && e < 1300) {
        const rgb_t cols[3] = { C_R3, C_R5, C_R6 };
        double p = (e - 200) / 1100.0;
        for (int i = 0; i < 3; i++) {
            double a = p * 2 * M_PI - i * 0.8;
            map1(g.cx + cos(a) * (g.rx + 5) - 1,
                 g.cy - lift + sin(a) * (g.rx + 5) - 1, SPARK, 3, cols[i]);
        }
    }
    cat_geom_t out = { g.cx, g.cy - lift, g.rx, g.ry };
    return out;
}

static double ease_in (double p) { return p * p * p; }
static double ease_out(double p) { return 1 - pow(1 - p, 3); }

cat_geom_t spr_peek(stage_t st, double t, double e, int d)
{
    /* Timings are ~1.8x the first draft. At 15 fps the original bolt was
     * seven frames to cross the entire screen, which reads as a jump cut
     * rather than a dash - and the absence beat was over before you had
     * registered she was gone. */
    double L = 0, hop = 0, p;
    if      (e <  500) { p = e / 500.0;          L = -6 * ease_out(p); hop = -1; }
    else if (e < 1400) { p = (e - 500) / 900.0;  L = -6 + 61 * ease_in(p); }
    else if (e < 2100) {                         L = 55; }
    else if (e < 2700) { p = (e - 2100) / 600.0; L = 55 - 29 * ease_out(p); }
    else if (e < 3200) { p = (e - 2700) / 500.0; L = 26 + 14 * ease_in(p); }
    else if (e < 4000) { p = (e - 3200) / 800.0; L = 40 - 44 * ease_out(p); }
    else               { p = (e - 4000) / 300.0; L = -4 + 4 * ease_out(p);
                         hop = (p < 0.5) ? -1 : 0; }
    double lean = gfx_round(L * d);

    cat_geom_t g = spr_cat(st, MOOD_HAPPY, t, lean, hop, 0);

    if (e >= 650 && e < 1400) {                     /* speed streaks */
        const rgb_t cols[3] = { C_R5, C_R3, C_R6 };
        for (int i = 0; i < 3; i++)
            gfx_rect(g.cx - d * (g.rx + 3 + i * 5), g.cy - 4 + i * 5, 4, 1, cols[i]);
    }
    if (e >= 1400 && e < 2100) {                    /* she's gone */
        int ex = (d < 0) ? 5 : 58;
        if ((int)fmod(floor(e / 220.0), 2.0)) map1(ex - 1, 34, SPARK, 3, C_R3);
    }
    if (e >= 3200 && e < 4000) {                    /* coming home */
        const rgb_t cols[3] = { C_R1, C_R5, C_R3 };
        for (int i = 0; i < 3; i++)
            map1(g.cx - d * (g.rx + 4 + i * 4) - 1, g.cy - 6 + i * 6, SPARK, 3, cols[i]);
    }
    return g;
}

void spr_evo(double t, double e, stage_t from, stage_t to, stage_t cur)
{
    const double cx = 32, cy = PET_CY;

    if (e < 1000) {                                 /* she trembles */
        const rgb_t cols[6] = { C_R3, C_R5, C_R1, C_R6, C_R4, C_R2 };
        int sh = gfx_round(sin(e / 40.0) * 2);
        spr_cat(cur, MOOD_HAPPY, t, sh, 0, sh > 0 ? 1 : -1);
        for (int i = 0; i < 6; i++) {
            double a = (i / 6.0) * 2 * M_PI + e / 260.0;
            double r = 28 - (e / 1000.0) * 17;
            map1(cx + cos(a) * r - 1, cy + sin(a) * r - 1, SPARK, 3, cols[i]);
        }
    } else if (e < 2200) {                          /* the light takes her */
        double p = (e - 1000) / 1200.0;
        gfx_ellipse(cx, cy, 4 + p * 42, 4 + p * 42, C_HORN);
        gfx_silh = C_WHITE; gfx_silh_on = 1;
        spr_cat(cur, MOOD_HAPPY, t, 0, 0, 0);
        gfx_silh_on = 0;
    } else if (e < 3000) {                          /* the shape changes */
        gfx_ellipse(cx, cy, 46, 46, C_HORN);
        int flip = (int)fmod(floor(e / 90.0), 2.0);
        gfx_silh = C_WHITE; gfx_silh_on = 1;
        spr_cat(flip ? to : from, MOOD_HAPPY, t, 0, 0, 0);
        gfx_silh_on = 0;
        if (flip) gfx_ring(cx, cy, 20 + fmod(e / 8.0, 14.0), C_WHITE);
    } else if (e < 4100) {                          /* the light bursts */
        const rgb_t cols[6] = { C_R1, C_R2, C_R3, C_R4, C_R5, C_R6 };
        double p = (e - 3000) / 1100.0;
        spr_cat(cur, MOOD_HAPPY, t, 0, gfx_round(sin(e / 120.0) * 2), 0);
        double r = 46 * (1 - p);
        if (r > 18) {
            gfx_ellipse(cx, cy, r, r, C_HORN);
            gfx_silh = C_WHITE; gfx_silh_on = 1;
            spr_cat(cur, MOOD_HAPPY, t, 0, 0, 0);
            gfx_silh_on = 0;
        }
        gfx_ring(cx, cy, 10 + p * 34, C_WHITE);
        gfx_ring(cx, cy,  4 + p * 46, C_HORN);
        gfx_rays(cx, cy, 12 + p * 20, 20 + p * 26, C_R3);
        for (int i = 0; i < 6; i++) {
            double a = (i / 6.0) * 2 * M_PI + 0.3, rr = 8 + p * 30;
            map1(cx + cos(a) * rr - 1, cy + sin(a) * rr - 1, SPARK, 3, cols[i]);
        }
    }
}

double spr_perk_hop(double at) { return gfx_round(sin(M_PI * (at / 800.0)) * 4); }
int    spr_perk_wig(double at) { return fmod(at, 260.0) < 130.0 ? 1 : -1; }

void spr_perk(double at, cat_geom_t g)
{
    const rgb_t cols[3] = { C_R3, C_R1, C_R5 };
    for (int i = 0; i < 3; i++) {
        double a = (i / 3.0) * 2 * M_PI - 0.6;
        double r = g.rx + 3 + at / 90.0;
        map1(g.cx + cos(a) * r - 1,
             g.cy - fabs(sin(a)) * r * 0.6 - 1, SPARK, 3, cols[i]);
    }
}
