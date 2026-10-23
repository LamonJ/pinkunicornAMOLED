/* sprites.h — everything Sprinkles looks like. */
#ifndef SPRITES_H
#define SPRITES_H

#include "gfx.h"
#include "pet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- palette ---------------------------------------------------------- */
#define C_COAT     0xFFD9EC
#define C_COATSH   0xF0A0CB
#define C_COATHI   0xFFF5FA
#define C_HORN     0xFFE08A
#define C_HORNSH   0xE6A834
#define C_DARK     0x3A1E33
#define C_BLUSH    0xFF96C0
#define C_WHITE    0xFFFFFF
#define C_R1       0xFF6B8A
#define C_R2       0xFFAA64
#define C_R3       0xFFE066
#define C_R4       0x7FE3A0
#define C_R5       0x6BD5FF
#define C_R6       0xB98CFF
#define C_EGGA     0xFFC4E0
#define C_EGGB     0xFF9ECB
#define C_EGGHI    0xFFF0F8
#define C_CUP      0x9E6EE1
#define C_CUPHI    0xC8AAFF
#define C_FROST    0x7FE3A0
#define C_FROSTHI  0xC8F5D8
#define C_CHERRY   0xFF5078
#define C_STAR     0xFFE066
#define C_DIM      0x1E222C
#define C_POO      0x966A48
#define C_POOHI    0xBA8A60
#define C_POOLO    0x6E4C32
#define C_VOID     0x05060A
#define C_VOID_OFF 0x02030A

extern const rgb_t RAIN[6];

/* ---- growth features -------------------------------------------------- */
typedef struct {
    int rx, ry;      /* body radii                                   */
    int ear;         /* ear height, 0 = none                         */
    int horn;        /* horn height, 0 = none, <=4 clamps width to 3  */
    int tail;        /* rainbow segments, 1 = a plain nub             */
    int fore;        /* forelock strands                             */
    int mane;        /* side mane segments                           */
    int whisk;       /* whiskers                                     */
    int sprout;      /* the blob's promise of a horn                 */
    int eye;         /* <4 = 2 px eyes, else 3 px                    */
} feat_t;
extern const feat_t FEAT[ST_COUNT];

#define PET_CY 39.0          /* her resting centre                    */

/* ---- pieces ----------------------------------------------------------- */
typedef struct { double cx, cy; int rx, ry; } cat_geom_t;

cat_geom_t spr_cat(stage_t st, mood_t m, double t,
                   double lean, double hop, int ear_wiggle);

void spr_egg(double progress, double t, int nudged);
void spr_hud(const pet_t *p, int energy_only, double t);
/* Age under her paws. value is 0..100, drawn with no unit. */
void spr_age(int value);
/* The word MUTE, drawn under the meters while the speaker is off. */
void spr_muted(void);
/* The one-time Start button. Not the egg. */
void spr_start_button(double t);
/* Lying still, X eyes, a slow sparkle, and the new-egg button. */
void spr_dead(double t);
void spr_mess(const pet_t *p);
void spr_cupcake(double x, double y, int bites);
void spr_broom(double x, double y);
void spr_buttons(const pet_t *p, int64_t now_ms, double t,
                 int pressed_feed, int pressed_clean);
void spr_zzz(double t, cat_geom_t g);
void spr_horn_glow(double t, stage_t st, cat_geom_t g);
void spr_hearts(double anim_t, double base_t, cat_geom_t g);
void spr_sparkles(double t, cat_geom_t g);
void spr_notes(double t, cat_geom_t g);
void spr_grow_shimmer(double t, cat_geom_t g);
void spr_resting(double t);

/* ---- animations -------------------------------------------------------
 * Each takes the animation's own elapsed ms. They compose the whole scene
 * body themselves because the transforms involved (rotation, silhouettes)
 * can't be expressed as lean/hop arguments to spr_cat. */

/* Front flip. Renders her flat into the alt buffer, then spins the bitmap. */
cat_geom_t spr_roll(stage_t st, double t, double roll_t);

/* Peekaboo: bolts off the edge, waits, peeks back, teases, comes home. */
cat_geom_t spr_peek(stage_t st, double t, double peek_t, int dir);

/* The transformation. `cur` is her stage right now (flips to `to` at 3000ms). */
void spr_evo(double t, double evo_t, stage_t from, stage_t to, stage_t cur);

/* Perk reaction shared by waking and being petted: the sparkle burst.
 * The caller applies the hop and ear wiggle — see spr_perk_hop/_wig. */
void spr_perk(double anim_t, cat_geom_t g);
double spr_perk_hop(double anim_t);
int    spr_perk_wig(double anim_t);

/* Menu button centres, in world units. */
#define BTN_FEED_X  15
#define BTN_CLEAN_X 49
#define BTN_Y       35
#define BTN_HIT_R   14

/* Hatch button on the dead scene. Two short lines: the round glass
 * clips a single "HATCH A NEW EGG" along the bottom edge. */
#define HATCH_X0 12
#define HATCH_X1 52
#define HATCH_Y0 42
#define HATCH_Y1 58

#ifdef __cplusplus
}
#endif

#endif
