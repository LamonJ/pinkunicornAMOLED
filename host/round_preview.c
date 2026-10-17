/* round_preview.c — one frame as the 1.75" round panel will show it.
 * Writes a 466x466 PPM. Pixels off the glass are 0,0,0. */
#include "../sprinkles/sprites.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define NOW 10000LL

static pet_t P;

static void reset(stage_t st)
{
    pet_init(&P, NOW);
    P.stage = st;
    P.hunger = P.happy = P.energy = 4;
    P.mess = 0;
    P.asleep = P.napping = 0;
    P.hatched = 1;
    P.born_ms = NOW - (int64_t)(100LL * 60 * 60 * 1000);
    P.last_tick_ms = NOW;
    P.sound = 0;
}

static void scene(int menu)
{
    double t = 10000;
    gfx_clear(C_VOID);
    gfx_dim = 1.0;
    spr_hud(&P, 0, t);
    gfx_dim = menu ? 0.32 : 1.0;
    spr_mess(&P);
    double hop = gfx_round(sin(t / 430.0) * 0.6);
    cat_geom_t g = spr_cat(P.stage, menu ? MOOD_SAD : MOOD_HAPPY, t, 0, hop, 0);
    gfx_dim = 1.0;
    if (menu) spr_buttons(&P, NOW, t, 0, 0);
    else spr_grow_shimmer(t, g);
    (void)g;
}

static void write_panel(const char *path, const uint16_t *panel)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", GFX_PANEL, GFX_PANEL);
    for (int i = 0; i < GFX_PANEL * GFX_PANEL; i++) {
        uint16_t p = panel[i];
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
    const char *dir = (argc > 1) ? argv[1] : ".";
    uint16_t *panel = calloc((size_t)GFX_PANEL * GFX_PANEL, sizeof(uint16_t));
    if (!panel) return 1;
    char path[512];

    reset(ST_ROYAL);
    scene(0);
    gfx_blit565(panel, 0, 0);
    snprintf(path, sizeof path, "%s/round-royal.ppm", dir);
    write_panel(path, panel);

    reset(ST_ROYAL);
    P.hunger = 1;
    scene(1);
    gfx_blit565(panel, 0, 0);
    snprintf(path, sizeof path, "%s/round-menu.ppm", dir);
    write_panel(path, panel);

    free(panel);
    printf("wrote round panel frames in %s\n", dir);
    return 0;
}
