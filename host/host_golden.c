/* host_golden.c — render reference scenes on a PC and dump raw PPMs.
 * A companion python script diffs these against the golden PNG frames.
 * No hardware, no display driver: this is milestone 2's gate. */
#include "../sprinkles/sprites.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define NOW 10000LL

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", GFX_W, GFX_W);
    for (int i = 0; i < GFX_W * GFX_W; i++) {
        rgb_t c = gfx_fb[i];
        unsigned char px[3] = { (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static pet_t P;
static void reset(stage_t st)
{
    pet_init(&P, NOW);
    P.stage = st;
    P.hunger = P.happy = P.energy = 4;
    P.mess = 0;
    P.asleep = P.napping = 0;
    P.hatched = 1;
    P.born_ms = NOW - (int64_t)(100LL * 60 * 60 * 1000);   /* 100 h old */
    P.last_tick_ms = NOW;
    P.sound = 0;
}

/* Composes a scene the same way the prototype's frame() does. */
static void scene(mood_t m, double t, int menu, int nap, int eating, int bites,
                  int love, double anim_t, int dance, int shimmer)
{
    gfx_clear(nap ? C_VOID_OFF : C_VOID);

    gfx_dim = 1.0;
    spr_hud(&P, nap, t);

    gfx_dim = nap ? 0.42 : (menu ? 0.32 : 1.0);
    spr_mess(&P);

    double lean = 0, hop = 0; int wig = 0;
    if (dance) {
        double ph = sin(t / 165.0);
        lean = gfx_round(ph * 3);
        hop  = gfx_round(fabs(sin(t / 165.0)) * 3);
        wig  = ph > 0 ? 1 : -1;
    } else if (m == MOOD_SLEEP) {
        hop = gfx_round(sin(t / 1500.0) * 0.9);
    } else {
        hop = gfx_round(sin(t / 430.0) * 0.6);
    }
    cat_geom_t g = spr_cat(P.stage, m, t, lean, hop, wig);
    gfx_dim = 1.0;

    if (shimmer) spr_grow_shimmer(t, g);
    if (dance)   { spr_notes(t, g); spr_sparkles(t, g); }
    if (m == MOOD_SLEEP) { spr_zzz(t, g); spr_horn_glow(t, P.stage, g); }
    if (eating)  spr_cupcake(g.cx + 3, g.cy + 2, bites);
    if (love)    spr_hearts(anim_t, 1200, g);
    if (menu)    spr_buttons(&P, NOW, t, 0, 0);
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    char path[512];
#define SHOT(name) do { snprintf(path, sizeof path, "%s/%s.ppm", dir, name); \
                        write_ppm(path); } while (0)

    /* eggs */
    reset(ST_EGG); P.hatched = 0;
    gfx_clear(C_VOID); spr_egg(5000.0 / 45000.0, 10000, 0);  SHOT("01-egg-early");
    gfx_clear(C_VOID); spr_egg(42000.0 / 45000.0, 10000, 0); SHOT("02-egg-late");

    /* four stages: all are eligible to grow at 100 h, so they shimmer */
    reset(ST_BLOB);    scene(MOOD_HAPPY, 10000, 0,0,0,0,0,0,0, 1); SHOT("03-blob-happy");
    reset(ST_KITTEN);  scene(MOOD_HAPPY, 10000, 0,0,0,0,0,0,0, 1); SHOT("04-kitten-happy");
    reset(ST_UNICORN); scene(MOOD_HAPPY, 10000, 0,0,0,0,0,0,0, 1); SHOT("05-unicorn-happy");
    reset(ST_ROYAL);   scene(MOOD_HAPPY, 10000, 0,0,0,0,0,0,0, 0); SHOT("06-royal-happy");

    /* moods */
    reset(ST_ROYAL); P.hunger=2; P.happy=2; P.energy=3;
    scene(MOOD_OK, 10000, 0,0,0,0,0,0,0,0); SHOT("07-royal-ok");
    reset(ST_ROYAL); P.hunger=1; P.happy=1; P.energy=1;
    scene(MOOD_SAD, 10000, 0,0,0,0,0,0,0,0); SHOT("08-royal-sad");
    reset(ST_ROYAL); P.napping=1; P.asleep=1; P.energy=2;
    scene(MOOD_SLEEP, 10000, 0,1,0,0,0,0,0,0); SHOT("09-royal-nap");

    /* mess */
    reset(ST_ROYAL); P.mess=1; P.mess_x[0]=13;
    scene(MOOD_HAPPY, 10000, 0,0,0,0,0,0,0,0); SHOT("10-royal-mess1");
    reset(ST_ROYAL); P.mess=2; P.mess_x[0]=13; P.mess_x[1]=51;
    scene(MOOD_HAPPY, 10000, 0,0,0,0,0,0,0,0); SHOT("11-royal-mess2");

    /* eating and love */
    reset(ST_ROYAL); P.hunger=2;
    scene(MOOD_EAT, 10000, 0,0,1,0,0,300,0,0);  SHOT("12-royal-eat");
    reset(ST_ROYAL); P.hunger=3;
    scene(MOOD_EAT, 10000, 0,0,1,2,1,1700,0,0); SHOT("13-royal-eat-bites");
    reset(ST_ROYAL); P.hunger=4;
    scene(MOOD_HAPPY, 10000, 0,0,0,0,1,2400,0,0); SHOT("14-royal-love");

    /* menu */
    reset(ST_ROYAL); P.hunger=1;
    scene(MOOD_SAD, 10000, 1,0,0,0,0,0,0,0); SHOT("16-menu-feed-wanted");
    reset(ST_ROYAL); P.mess=2; P.mess_x[0]=13; P.mess_x[1]=51;
    scene(MOOD_HAPPY, 10000, 1,0,0,0,0,0,0,0); SHOT("17-menu-clean-wanted");

    /* dance */
    reset(ST_ROYAL); scene(MOOD_HAPPY, 10000, 0,0,0,0,0,0,1,0); SHOT("18-royal-dance");

    /* petting: perk hop, ear wiggle, sparkle burst, hearts */
    reset(ST_ROYAL);
    {
        double at = 400, t = 10000;
        gfx_clear(C_VOID);
        gfx_dim = 1.0; spr_hud(&P, 0, t);
        spr_mess(&P);
        double hop = gfx_round(sin(t / 430.0) * 0.6) + spr_perk_hop(at);
        cat_geom_t g = spr_cat(P.stage, MOOD_HAPPY, t, 0, hop, spr_perk_wig(at));
        spr_perk(at, g);
        spr_hearts(at, 250, g);
        SHOT("15-royal-pet");
    }

    /* roll */
    reset(ST_ROYAL);
    { double t = 10000;
      gfx_clear(C_VOID); gfx_dim = 1.0; spr_hud(&P, 0, t); spr_mess(&P);
      spr_roll(P.stage, t, 700); SHOT("19-royal-roll"); }

    /* peekaboo */
    for (int k = 0; k < 3; k++) {
        const double es[3] = { 900, 1750, 2400 };
        const char *nm[3] = { "20-royal-peek-bolt", "21-royal-peek-gone",
                              "22-royal-peek-back" };
        reset(ST_ROYAL);
        double t = 10000;
        gfx_clear(C_VOID); gfx_dim = 1.0; spr_hud(&P, 0, t); spr_mess(&P);
        spr_peek(P.stage, t, es[k], -1);
        snprintf(path, sizeof path, "%s/%s.ppm", dir, nm[k]); write_ppm(path);
    }

    /* transformation */
    for (int k = 0; k < 5; k++) {
        const double es[5] = { 500, 1600, 2600, 3400, 3950 };
        const char *nm[5] = { "23-evo-gather", "24-evo-whiteout", "25-evo-morph",
                              "26-evo-burst", "27-evo-reveal" };
        reset(ST_UNICORN);
        stage_t cur = (es[k] >= 3000) ? ST_ROYAL : ST_UNICORN;
        P.stage = cur;
        gfx_clear(C_VOID);
        spr_evo(10000, es[k], ST_UNICORN, ST_ROYAL, cur);
        snprintf(path, sizeof path, "%s/%s.ppm", dir, nm[k]); write_ppm(path);
    }

    /* resting */
    gfx_clear(C_VOID); spr_resting(10000); SHOT("28-resting");

    printf("rendered\n");
    return 0;
}
