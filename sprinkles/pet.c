#include "pet.h"
#include <string.h>

const double PET_STAGE_AT[ST_COUNT] = {
    0.0,        /* egg     */
    0.75,       /* blob    — the egg hatches after 45 s */
    480.0, 1800.0, 4320.0, 0.0
};

/* Time she must spend in each stage before the next unlocks. */
const double PET_STAGE_DWELL[ST_COUNT] = {
    0.75,       /* egg     — 45 s to hatch          */
    480.0,      /* blob    — 8 h  before kitten     */
    1320.0,     /* kitten  — 22 h before unicorn    */
    2520.0,     /* unicorn — 42 h before royal      */
    0.0,        /* royal   — fully grown            */
    0.0         /* dead    — stays until a new egg  */
};
const char *PET_STAGE_NAME[ST_COUNT] = {
    "EGG", "LITTLE BLOB", "KITTEN", "UNICORN", "ROYAL UNICAT", "DEAD"
};

static const int MESS_ZONES[PET_MESS_MAX] = { 13, 51 };

/* [pip about to be lost] -> minutes it takes. Index 0 is unused. */
const double PET_HUNGER_MIN[5] = { 0.0, 180.0, 120.0, 60.0, 5.0 };
/*                            1->0   2->1   3->2  4->3  */

const double PET_MESS_MIN[PET_MESS_MAX] = { 5.0, 70.0 };

void pet_init(pet_t *p, int64_t now_ms)
{
    memset(p, 0, sizeof(*p));
    p->stage = ST_EGG;
    p->hunger = p->happy = p->energy = 4;
    p->born_ms = now_ms;
    p->stage_since_ms = now_ms;
    p->last_tick_ms = now_ms;
    p->speed = 1.0;
    p->sound = true;
}

double pet_age_minutes(const pet_t *p, int64_t now_ms)
{
    return (double)(now_ms - p->born_ms) / 60000.0 * p->speed;
}

int pet_sim_age(const pet_t *p, int64_t now_ms)
{
    /* Each band is 25 birthdays. Crossing one lands on the next band's rate. */
    const double span0 = PET_AGE_BAND * PET_AGE_M0;
    const double span1 = PET_AGE_BAND * PET_AGE_M1;
    const double span2 = PET_AGE_BAND * PET_AGE_M2;
    const double span3 = PET_AGE_BAND * PET_AGE_M3;
    double mins = pet_age_minutes(p, now_ms);

    if (mins < 0) mins = 0;
    if (mins < span0) return (int)(mins / PET_AGE_M0);
    mins -= span0;
    if (mins < span1) return 25 + (int)(mins / PET_AGE_M1);
    mins -= span1;
    if (mins < span2) return 50 + (int)(mins / PET_AGE_M2);
    mins -= span2;
    if (mins < span3) return 75 + (int)(mins / PET_AGE_M3);
    return 100;
}

static void drain(int *pip, double *frac, double dt, double per)
{
    *frac += dt;
    while (*frac >= per) { *frac -= per; if (*pip > 0) (*pip)--; }
}

/* Per-level drain: how long the next pip takes depends on how many are
 * left. Stops dead at zero rather than banking credit, so a long absence
 * cannot leave her instantly empty the moment she is fed. */
static void drain_graded(int *pip, double *frac, double dt, const double *per)
{
    *frac += dt;
    while (*pip > 0 && *frac >= per[*pip]) { *frac -= per[*pip]; (*pip)--; }
    if (*pip == 0) *frac = 0;
}

static void regen(int *pip, double *frac, double dt, double per)
{
    *frac += dt;
    while (*frac >= per) { *frac -= per; if (*pip < 4) (*pip)++; }
}

/* Happiness at 0 is the end. Meters freeze; only a new egg starts again. */
static void pet_die(pet_t *p)
{
    p->stage = ST_DEAD;
    p->happy = 0;
    p->napping = false;
    p->asleep = false;
    p->mess = 0;
}

stage_t pet_pending_grow(const pet_t *p, int64_t now_ms)
{
    if (p->stage == ST_EGG || p->stage >= ST_ROYAL) return ST_COUNT;
    double in_stage = (double)(now_ms - p->stage_since_ms) / 60000.0 * p->speed;
    return in_stage >= PET_STAGE_DWELL[p->stage]
         ? (stage_t)(p->stage + 1) : ST_COUNT;
}

/* Minutes left in this stage, for the grown-up screen. */
double pet_stage_remaining(const pet_t *p, int64_t now_ms)
{
    if (p->stage >= ST_ROYAL) return -1;
    double in_stage = (double)(now_ms - p->stage_since_ms) / 60000.0 * p->speed;
    double left = PET_STAGE_DWELL[p->stage] - in_stage;
    return left < 0 ? 0 : left;
}

mood_t pet_mood(const pet_t *p, bool eating)
{
    if (p->asleep) return MOOD_SLEEP;
    if (eating)    return MOOD_EAT;
    int low = p->hunger;
    if (p->happy  < low) low = p->happy;
    if (p->energy < low) low = p->energy;
    if (low <= 1) return MOOD_SAD;
    if (low >= 4) return MOOD_HAPPY;
    return MOOD_OK;
}

/* One slice of elapsed time. Kept small by pet_tick so that thresholds
 * (notably "is she fed enough for hearts to regenerate") are re-evaluated
 * as they are crossed. */
static void needs_step(pet_t *p, double dt)
{
    double mul = p->asleep ? PET_SLEEP_DRAIN : 1.0;

    drain_graded(&p->hunger, &p->frac_hunger, dt * mul, PET_HUNGER_MIN);

    /* Hearts refill on their own while she is fed and her space is clean.
     * Feeding and sweeping are the entire care loop. */
    if (!p->asleep && p->hunger >= PET_FED_ENOUGH && p->mess == 0)
        regen(&p->happy, &p->frac_regen_happy, dt, PET_REGEN_HAPPY);
    else
        drain(&p->happy, &p->frac_happy, dt * mul, PET_DECAY_HAPPY);

    if (p->asleep)
        regen(&p->energy, &p->frac_regen_energy, dt, PET_REGEN_ENERGY);
    else
        drain(&p->energy, &p->frac_energy, dt, PET_DECAY_ENERGY);
}

void pet_tick(pet_t *p, int64_t now_ms, double rand01)
{
    double dt = (double)(now_ms - p->last_tick_ms) / 60000.0 * p->speed;
    p->last_tick_ms = now_ms;
    if (dt <= 0) return;
    if (p->stage == ST_DEAD) return;

    /* The egg is the only stage that advances on its own. */
    if (p->stage == ST_EGG) {
        if (pet_age_minutes(p, now_ms) >= PET_STAGE_AT[ST_BLOB]) {
            p->stage = ST_BLOB;
            p->stage_since_ms = now_ms;      /* the dwell clock starts here */
            p->hatched = true;

            /* She arrives with something to do. A brand-new pet with every
             * meter full and a clean floor gives a four-year-old nothing to
             * press, so the first thing she learns is that the buttons do
             * nothing. Two cupcakes short and one mess on the floor means
             * both actions work, and both pay off, immediately. */
            p->hunger = 2;
            p->happy  = 4;
            p->energy = 4;
            p->mess = 1;
            p->mess_x[0] = MESS_ZONES[0];

            /* And she is awake. Whatever sleep state the egg was carrying
             * (a nap pressed before hatching, or a restored one) must not
             * survive into her first moment. */
            p->napping = false;
            p->asleep = false;
            p->wake_until_ms = 0;
            p->frac_hunger = p->frac_happy = p->frac_energy = 0;
            p->frac_regen_happy = p->frac_regen_energy = p->frac_mess = 0;
        }
        return;
    }

    /* Slice the elapsed time so a single catch-up after hours (or days) of
     * deep sleep lands on exactly the same state as ticking once a minute
     * would have. Without this, coming back from the RTC gives a different
     * pet than never switching off — the one bug in here that would have
     * been genuinely hard to spot on hardware.
     *
     * Sleep is part of that. It is decided at the start of each minute,
     * from the energy she has then, and the next minute sees whatever that
     * minute did to the bar. Checking it once for the whole absence would
     * keep her awake (and draining) straight through a nap she should have
     * taken in the first half hour.
     *
     * 8 h off is 480 iterations. A month is ~43k. Both are free. */
    const double MAX_STEPS = 200000.0;
    double slice = 1.0;
    if (dt / slice > MAX_STEPS) slice = dt / MAX_STEPS;

    double left = dt;
    while (left > 0) {
        /* Asleep if you put her there, or she ran out of energy. An empty
         * bar keeps her down until it is full again: waking on the first
         * pip just spends it and puts her straight back to sleep. A manual
         * wake still beats that for half an hour. A nap stays a nap even
         * after the bar fills, so a long screen-off catch-up does not turn
         * into awake drain halfway through. */
        bool rested = p->energy >= 4;
        bool forced_awake = now_ms < p->wake_until_ms;
        if (p->napping)
            p->asleep = true;
        else if (forced_awake)
            p->asleep = false;
        else if (p->energy == 0 || (p->asleep && !rested))
            p->asleep = true;
        else
            p->asleep = false;

        double step = (left > slice) ? slice : left;
        needs_step(p, step);
        left -= step;
        /* Stop on the minute she dies, so a long absence matches
         * ticking once a minute: later time does not keep draining her. */
        if (p->happy <= 0) break;
    }

    /* A heart at 0 is death, whether it fell from neglect or from
     * one cake too many. */
    if (p->happy <= 0) pet_die(p);

    /* Mess, never while asleep or dead. Evaluated once per call: the caller
     * supplies randomness so tests stay deterministic. */
    if (p->stage != ST_DEAD && !p->asleep && p->mess < PET_MESS_MAX &&
        rand01 < dt / PET_MESS_MIN[p->mess]) {
        p->mess_x[p->mess] = MESS_ZONES[p->mess % PET_MESS_MAX];
        p->mess++;
    }
}

feed_result_t pet_feed(pet_t *p, int64_t now_ms)
{
    stage_t grow;
    bool hungry;

    if (p->stage == ST_DEAD || p->stage == ST_EGG) return FEED_OVERFULL;

    grow = pet_pending_grow(p, now_ms);
    hungry = p->hunger < 4;

    /* No cutoff. A cake on top of a full bar costs a heart instead of
     * filling anything. The last heart kills her. Growing still wins:
     * that feeding is the one that changes her shape. */
    if (!hungry && grow == ST_COUNT) {
        if (p->happy > 0) p->happy--;
        p->frac_happy = 0;
        p->frac_regen_happy = 0;
        if (p->happy <= 0) pet_die(p);
        return FEED_OVERFULL;
    }

    if (p->hunger < 4) p->hunger++;
    /* Restart the drain timer, or leftover credit could take the new pip
     * straight back off again. */
    p->frac_hunger = 0;
    if (grow != ST_COUNT) return FEED_TRANSFORM;
    return hungry ? FEED_HUNGRY : FEED_PLAIN;
}

void pet_finish_transform(pet_t *p, int64_t now_ms)
{
    stage_t grow = pet_pending_grow(p, now_ms);
    if (grow == ST_COUNT) return;
    p->stage = grow;
    p->stage_since_ms = now_ms;      /* she has to earn the next one too */
}

bool pet_clean(pet_t *p)
{
    if (p->stage == ST_DEAD) return false;
    if (p->mess == 0) return false;
    p->mess = 0;
    return true;
}

/* She always jumps when touched — only the heart is rate-limited. A refused
 * press is confusing for a small child; a missing heart is just a quieter
 * reward. */
bool pet_cuddle(pet_t *p, int64_t now_ms)
{
    if (p->stage == ST_DEAD) return false;
    if (now_ms < p->cuddle_next_heart_ms) return false;
    if (p->happy >= 4) return false;
    p->cuddle_next_heart_ms = now_ms + PET_CUDDLE_CD_MS;
    p->happy++;
    return true;
}

void pet_set_nap(pet_t *p, bool nap, int64_t now_ms)
{
    if (p->stage == ST_DEAD) return;
    p->napping = nap;
    if (nap) {
        p->asleep = true;
        p->wake_until_ms = 0;          /* a deliberate nap cancels any override */
    } else {
        p->asleep = false;
        p->wake_until_ms = now_ms + PET_WAKE_OVERRIDE_MS;
    }
}

void pet_snapshot(const pet_t *p, int64_t now_ms,
                  int64_t rtc_ms, bool have_rtc, pet_save_t *out)
{
    out->version     = PET_SAVE_VERSION;
    out->pet         = *p;
    out->age_ms      = now_ms - p->born_ms;
    out->in_stage_ms = now_ms - p->stage_since_ms;
    out->rtc_ms      = have_rtc ? rtc_ms : 0;
    if (out->age_ms < 0)      out->age_ms = 0;
    if (out->in_stage_ms < 0) out->in_stage_ms = 0;
}

bool pet_restore(pet_t *p, const pet_save_t *s, int64_t now_ms,
                 int64_t rtc_ms, bool have_rtc, int64_t *away_ms_out)
{
    if (s->version != PET_SAVE_VERSION) return false;
    /* A blob that survived an upload can still be the right size and the
     * wrong pet. Refuse anything that is not a stage and a set of meters
     * this build understands, and leave the caller on the fresh egg. */
    if ((unsigned)s->pet.stage >= ST_COUNT) return false;
    if (s->pet.hunger < 0 || s->pet.hunger > 4) return false;
    if (s->pet.happy  < 0 || s->pet.happy  > 4) return false;
    if (s->pet.energy < 0 || s->pet.energy > 4) return false;
    if (s->pet.mess < 0 || s->pet.mess > PET_MESS_MAX) return false;
    if (s->age_ms < 0 || s->in_stage_ms < 0) return false;

    /* Only ever wall-clock minus wall-clock. If either side is missing we
     * simply say no time passed - she keeps her age and her meters, she just
     * does not get hungrier while switched off. */
    int64_t away = 0;
    if (have_rtc && s->rtc_ms > 0) {
        away = rtc_ms - s->rtc_ms;
        if (away < 0) away = 0;                  /* clock went backwards */
    }

    *p = s->pet;
    p->born_ms        = now_ms - (s->age_ms + away);
    p->stage_since_ms = now_ms - (s->in_stage_ms + away);
    p->last_tick_ms   = now_ms - away;           /* pet_tick catches it up */
    if (away_ms_out) *away_ms_out = away;
    return true;
}

void pet_nudge_egg(pet_t *p)
{
    if (p->stage == ST_EGG) p->born_ms -= 6000;
}
