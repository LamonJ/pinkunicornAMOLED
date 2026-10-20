/* pet.h — Sprinkles' state machine.
 *
 * Deliberately free of hardware and drawing. Feed it wall-clock milliseconds
 * and it tells you what she is. Everything here is unit-testable on a PC.
 *
 * Two rules that must survive refactoring:
 *   1. Hunger and energy bottom out at 0 and can recover. Happiness can
 *      reach 0 too, and at 0 she dies.
 *   2. She never advances a growth stage on her own. Age only makes her
 *      eligible; feeding is what transforms her, so a child sees it happen.
 */
#ifndef PET_H
#define PET_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { ST_EGG = 0, ST_BLOB, ST_KITTEN, ST_UNICORN, ST_ROYAL, ST_DEAD, ST_COUNT } stage_t;

typedef enum {
    MOOD_OK = 0, MOOD_HAPPY, MOOD_SAD, MOOD_SLEEP, MOOD_EAT
} mood_t;

/* Minutes she must spend IN a stage before the next one unlocks.
 *
 * Deliberately per-stage, not cumulative from birth. With thresholds
 * measured from birth, a pet that has been alive a while is eligible for
 * every remaining stage at once, so each feed cashes one in and she rockets
 * to fully grown in four cupcakes. Growth has to be paced by time spent
 * being looked after, not by the calendar. */
extern const double PET_STAGE_DWELL[ST_COUNT];

/* Kept for the egg only: how long it takes to hatch. */
extern const double PET_STAGE_AT[ST_COUNT];
extern const char  *PET_STAGE_NAME[ST_COUNT];

/* Hunger drains on a GRADED schedule, not a flat rate: the first cupcake
 * goes in five minutes so a child gets something to do almost immediately,
 * then it slows right down so she cannot starve while nobody is looking.
 *
 * Indexed by the pip about to be lost, so [4] is 4->3 and [1] is 1->0.
 * Empty takes about six hours in total. */
extern const double PET_HUNGER_MIN[5];

#define PET_DECAY_HAPPY  190.0
#define PET_DECAY_ENERGY 230.0
/* Hearts refill while she's fed and her floor is clean.
 *
 * This had to come down hard once hunger got its fast first drop: at 100
 * minutes a pip, and requiring 3+ cupcakes, the window closed after about an
 * hour and hearts could essentially never refill on their own. 15 minutes,
 * and satisfied at 2 cupcakes, means care visibly pays off. */
#define PET_REGEN_HAPPY   15.0
#define PET_FED_ENOUGH       2   /* cupcakes needed for hearts to recover */
/* A four-year-old will not wait three hours to see a bar move. A nap now
 * fills one pip every three minutes, so empty to full is about twelve
 * minutes and the very first pip lands while she is still watching. */
#define PET_REGEN_ENERGY   3.0   /* minutes per pip while asleep    */
#define PET_SLEEP_DRAIN   0.18   /* need drain multiplier asleep    */
#define PET_MESS_MAX          2

/* Same idea for mess: the first appears in five minutes so the broom is
 * useful straight away; the second takes over an hour. Indexed by how many
 * are already on the floor. */
extern const double PET_MESS_MIN[PET_MESS_MAX];
#define PET_CUDDLE_CD_MS  10000  /* heart cooldown, not the reaction */

/* Waking her by hand keeps her up for a while even if she's flat out of
 * energy, so a button press always visibly does something. */
#define PET_WAKE_OVERRIDE_MS (30 * 60 * 1000)

/* There is deliberately NO clock-driven bedtime. She sleeps when you put her
 * to sleep, or when she runs out of energy - never because of what time it
 * is. A pet that nods off on a schedule the child can't see reads as broken,
 * and it made every button look dead for anyone who flashed after 8pm. */

typedef struct {
    stage_t stage;
    int     hunger, happy, energy;   /* 0..4 pips                       */
    int     mess;                    /* 0..PET_MESS_MAX                 */
    int     mess_x[PET_MESS_MAX];    /* world x of each pile            */
    bool    asleep;                  /* napped, or out of energy        */
    bool    napping;                 /* the deliberate, visible nap     */
    bool    hatched;

    int64_t born_ms;                 /* wall clock                      */
    int64_t stage_since_ms;          /* when she entered this stage     */
    int64_t last_tick_ms;
    double  speed;                   /* 1.0 = real time; >1 for testing */

    /* fractional decay carries, so pips move on exact boundaries */
    double  frac_hunger, frac_happy, frac_energy, frac_regen_happy,
            frac_regen_energy, frac_mess;

    int64_t cuddle_next_heart_ms;

    /* Waking her by hand keeps her up even when she's flat out of energy,
     * so a button press always visibly does something. */
    int64_t wake_until_ms;

    bool    sound;
} pet_t;

void   pet_init(pet_t *p, int64_t now_ms);

/* ---- persistence -------------------------------------------------------
 * The timestamps inside pet_t are on the MONOTONIC clock - milliseconds
 * since boot - because that is what the frame loop has. The RTC is on the
 * wall clock. Diffing one against the other yields about 56,000 years, which
 * is exactly the bug this pair of functions exists to prevent: the blob
 * stores DURATIONS plus the wall time it was written, never a raw monotonic
 * stamp. Round-tripped in host/test_pet.c. */
/* 5 discards pets saved by builds 4 and earlier. An Arduino upload leaves
 * the NVS partition alone, so the previous blob was restoring the same pet
 * after a fresh flash. A flash of this build starts at the egg. Later boots
 * of the same build keep her. */
#define PET_SAVE_VERSION 5

typedef struct {
    uint32_t version;
    pet_t    pet;             /* meters, stage, mess - timestamps ignored */
    int64_t  age_ms;          /* how old she was when saved              */
    int64_t  in_stage_ms;     /* how long she had been in this stage     */
    int64_t  rtc_ms;          /* wall clock at save, or 0 if unknown     */
} pet_save_t;

void pet_snapshot(const pet_t *p, int64_t now_ms,
                  int64_t rtc_ms, bool have_rtc, pet_save_t *out);

/* Returns false (and leaves p untouched) if the blob is the wrong version.
 * `away_ms_out` receives how long she was switched off, so the caller can
 * report it and let pet_tick catch that time up. */
bool pet_restore(pet_t *p, const pet_save_t *s, int64_t now_ms,
                 int64_t rtc_ms, bool have_rtc, int64_t *away_ms_out);

/* Advance to now_ms. Call about once a second while awake, and once on boot
 * after reading the RTC — it catches up however long has passed. */
void   pet_tick(pet_t *p, int64_t now_ms, double rand01);

double pet_age_minutes(const pet_t *p, int64_t now_ms);

/* Simulated age, 0..100, from time alive. 100 is as old as she gets.
 * Each displayed year lasts:
 *   0..24    20 minutes
 *   25..49   2 hours
 *   50..74   3 hours
 *   75..99   5 hours
 * and 100 stays. The year she is in decides the wait for the next one,
 * so 24 lasts 20 minutes and 25 lasts 2 hours. */
#define PET_AGE_BAND 25
#define PET_AGE_M0   20.0             /* minutes per year, ages 0..24  */
#define PET_AGE_M1  120.0             /* minutes per year, ages 25..49 */
#define PET_AGE_M2  180.0             /* minutes per year, ages 50..74 */
#define PET_AGE_M3  300.0             /* minutes per year, ages 75..99 */
int pet_sim_age(const pet_t *p, int64_t now_ms);
mood_t pet_mood(const pet_t *p, bool eating);

/* The next stage if she has been in this one long enough, else ST_COUNT. */
stage_t pet_pending_grow(const pet_t *p, int64_t now_ms);
double  pet_stage_remaining(const pet_t *p, int64_t now_ms);

/* Actions. Return value says whether anything happened, so the caller knows
 * which sound and animation to play. */
typedef enum {
    FEED_OVERFULL,   /* cake already full: she eats it and loses a heart */
    FEED_PLAIN,          /* ate it, wasn't hungry: no hearts              */
    FEED_HUNGRY,         /* ate it and needed it: hearts + long chime     */
    FEED_TRANSFORM       /* eligible: run the transformation              */
} feed_result_t;

feed_result_t pet_feed(pet_t *p, int64_t now_ms);
bool          pet_clean(pet_t *p);              /* false if nothing to sweep */
bool          pet_cuddle(pet_t *p, int64_t now_ms); /* true if a heart landed */
void          pet_finish_transform(pet_t *p, int64_t now_ms);
void          pet_set_nap(pet_t *p, bool nap, int64_t now_ms);

/* Egg only: every press brings hatching 6 s closer. */
void          pet_nudge_egg(pet_t *p);

#ifdef __cplusplus
}
#endif

#endif
