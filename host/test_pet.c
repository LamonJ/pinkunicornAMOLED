/* test_pet.c — the state machine, on a PC, with no hardware in sight.
 * Covers the invariants that matter: hunger and energy bottom at 0, a heart
 * at 0 kills her, she never grows on her own, and the cuddle cooldown gates
 * the heart and not the reaction. */
#include "../sprinkles/pet.h"
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { \
    fails++; printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

#define MIN(m) ((int64_t)((m) * 60000LL))
#define HOUR(h) MIN((h) * 60.0)

/* Run the clock forward in one-minute steps, daytime, no randomness. */
static void advance(pet_t *p, double minutes, int64_t *now)
{
    for (int i = 0; i < (int)minutes; i++) {
        *now += MIN(1);
        pet_tick(p, *now, 1.0);   /* rand01=1.0 -> never messes */
    }
}

/* Same clock, but she stays awake. Hunger and heart timings are specified
 * for an awake pet; once the moon bar empties she sleeps and those drops
 * slow down, which is a different rule. */
static void advance_awake(pet_t *p, double minutes, int64_t *now)
{
    for (int i = 0; i < (int)minutes; i++) {
        p->energy = 4;
        p->frac_energy = 0;
        p->napping = false;
        *now += MIN(1);
        pet_tick(p, *now, 1.0);
    }
}

/* A long wait that is about the growth clock, not about starving.
 * Top her up often enough that the heart bar cannot reach 0. */
static void age_cared_for(pet_t *p, double minutes, int64_t *now)
{
    while (minutes > 0) {
        double step = minutes > 30 ? 30 : minutes;
        p->hunger = 4;
        p->happy = 4;
        p->energy = 4;
        p->mess = 0;
        p->asleep = false;
        p->napping = false;
        p->frac_hunger = p->frac_happy = p->frac_regen_happy = 0;
        advance(p, step, now);
        minutes -= step;
    }
}

/* She must arrive awake and with chores waiting. A brand-new pet with full
 * meters and a clean floor teaches a four-year-old that the buttons do
 * nothing. */
static void test_hatches_awake_with_chores(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);

    /* Even if a nap was pressed while she was still an egg. */
    pet_set_nap(&p, true, now);
    advance(&p, 2, &now);

    CHECK(p.stage == ST_BLOB, "hatched");
    CHECK(!p.asleep, "AWAKE on arrival");
    CHECK(!p.napping, "and not carrying a nap out of the egg");
    CHECK(p.hunger == 2, "two cupcakes short, got %d", p.hunger);
    CHECK(p.mess == 1, "one mess to sweep, got %d", p.mess);
    CHECK(p.energy == 4, "rested");
    CHECK(p.happy == 4, "and happy");

    /* Both buttons must therefore do something on the very first press. */
    CHECK(pet_feed(&p, now) == FEED_HUNGRY, "feeding works straight away");
    CHECK(pet_clean(&p), "sweeping works straight away");
}

static void test_starts_as_egg(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    CHECK(p.stage == ST_EGG, "should start as an egg");
    CHECK(p.hunger == 4 && p.happy == 4 && p.energy == 4, "starts full");
    advance(&p, 2, &now);
    CHECK(p.stage == ST_BLOB, "hatches on its own after 45s, got %s",
          PET_STAGE_NAME[p.stage]);
    CHECK(p.hatched, "hatched flag set");
}

static void test_egg_nudge(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    for (int i = 0; i < 8; i++) pet_nudge_egg(&p);   /* 48 s of credit */
    now += 1000; pet_tick(&p, now, 1.0);
    CHECK(p.stage == ST_BLOB, "eight taps should hatch it early");
}

static void test_never_grows_alone(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);                    /* -> blob */
    advance(&p, 600, &now);                  /* well past the blob dwell */
    CHECK(p.stage == ST_BLOB, "must NOT advance on its own, got %s",
          PET_STAGE_NAME[p.stage]);
    CHECK(pet_pending_grow(&p, now) == ST_KITTEN, "should be eligible");

    feed_result_t r = pet_feed(&p, now);
    CHECK(r == FEED_TRANSFORM, "feeding an eligible pet transforms her");
    CHECK(p.stage == ST_BLOB, "stage flips only when the animation says so");
    pet_finish_transform(&p, now);
    CHECK(p.stage == ST_KITTEN, "now a kitten, got %s", PET_STAGE_NAME[p.stage]);

    /* And she must NOT be instantly eligible again. Growth is paced by time
     * spent in each stage, so an old pet cannot bank up four transformations
     * and burn through them on four cupcakes. */
    CHECK(pet_pending_grow(&p, now) == ST_COUNT,
          "not eligible for unicorn the moment she becomes a kitten");
    age_cared_for(&p, 600, &now);
    CHECK(p.stage == ST_KITTEN, "care keeps her alive, got %s", PET_STAGE_NAME[p.stage]);
    CHECK(pet_pending_grow(&p, now) == ST_COUNT,
          "still not eligible after 10 h as a kitten (needs 22)");
    age_cared_for(&p, 800, &now);
    CHECK(pet_pending_grow(&p, now) == ST_UNICORN,
          "eligible after 23 h as a kitten");
}

/* The exact bug: a long-lived pet used to transform on every single feed. */
static void test_old_pet_does_not_rocket_through_stages(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    age_cared_for(&p, 60 * 200, &now);      /* 200 h old, looked after, ancient */

    CHECK(pet_feed(&p, now) == FEED_TRANSFORM, "first feed transforms");
    pet_finish_transform(&p, now);
    CHECK(p.stage == ST_KITTEN, "one stage");

    p.hunger = 1;
    feed_result_t r = pet_feed(&p, now);
    CHECK(r == FEED_HUNGRY, "the NEXT feed just feeds her, got %d", r);
    CHECK(p.hunger == 2, "and the food lands, got %d", p.hunger);
    CHECK(p.stage == ST_KITTEN, "still a kitten");
}

static void test_hunger_bottoms(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    advance(&p, 7 * 60, &now);
    CHECK(p.hunger == 0, "hunger bottoms out at 0, got %d", p.hunger);
    CHECK(p.hunger >= 0 && p.happy >= 0 && p.energy >= 0, "nothing goes negative");
    CHECK(p.stage != ST_DEAD, "empty cake alone does not kill her");
    for (int i = 0; i < 4; i++) pet_feed(&p, now);
    CHECK(p.hunger == 4, "four cupcakes refill her, got %d", p.hunger);
    CHECK(p.stage != ST_DEAD, "feeding an empty bar does not cost a heart");
}

static void test_overfeed_costs_a_heart(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    p.stage = ST_ROYAL;                      /* fully grown, so food cannot transform */
    p.hunger = 4;
    p.happy = 4;
    CHECK(pet_feed(&p, now) == FEED_OVERFULL, "a full bar still accepts the cake");
    CHECK(p.hunger == 4, "cake stays full, got %d", p.hunger);
    CHECK(p.happy == 3, "and a heart comes off, got %d", p.happy);
    CHECK(p.stage == ST_ROYAL, "one extra cake does not kill her");
    pet_feed(&p, now);
    pet_feed(&p, now);
    CHECK(p.happy == 1, "three extra cakes leave one heart, got %d", p.happy);
    CHECK(pet_feed(&p, now) == FEED_OVERFULL, "the last cake is still eaten");
    CHECK(p.stage == ST_DEAD, "the last heart kills her, got %s", PET_STAGE_NAME[p.stage]);
    CHECK(p.happy == 0, "hearts are gone");
    CHECK(pet_feed(&p, now) == FEED_OVERFULL, "feeding a corpse reports the same");
    CHECK(p.stage == ST_DEAD && p.happy == 0, "and does not bring her back");
}

static void test_grow_feed_is_not_overfeed(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    p.hunger = 4;
    p.happy = 4;
    p.stage_since_ms = now - MIN(500);       /* blob dwell is 8 h */
    CHECK(pet_feed(&p, now) == FEED_TRANSFORM, "a full bar still grows her");
    CHECK(p.happy == 4, "growing does not cost a heart, got %d", p.happy);
    CHECK(p.stage == ST_BLOB, "the shape waits for the animation");
}

static void test_neglect_kills(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    for (int d = 0; d < 30; d++) advance(&p, 1440, &now);   /* a month, ignored */
    CHECK(p.stage == ST_DEAD, "a month alone ends it, got %s", PET_STAGE_NAME[p.stage]);
    CHECK(p.happy == 0, "hearts are gone");
    CHECK(p.hunger >= 0 && p.energy >= 0, "other meters stay in range");
    CHECK(!p.asleep && !p.napping, "death is not sleep");

    /* One long catch-up must land on the same death as minute steps.
     * Two hearts at the awake rate is about 380 minutes, but an empty moon
     * puts her to sleep and slows the drop, so this window is longer. */
    pet_t a, b; int64_t t0 = 0;
    pet_init(&a, t0); pet_init(&b, t0);
    a.stage = b.stage = ST_ROYAL;
    a.hunger = b.hunger = 0;
    a.happy = b.happy = 2;
    a.frac_happy = b.frac_happy = 0;
    int64_t t = t0;
    for (int i = 0; i < 520; i++) { t += MIN(1); pet_tick(&a, t, 1.0); }
    pet_tick(&b, t0 + MIN(520), 1.0);
    CHECK(a.stage == ST_DEAD && b.stage == ST_DEAD, "both paths die");
    CHECK(a.happy == b.happy && a.energy == b.energy && a.hunger == b.hunger,
          "stepwise %d/%d/%d vs catch-up %d/%d/%d",
          a.hunger, a.happy, a.energy, b.hunger, b.happy, b.energy);
}

static void test_feed_results(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    p.hunger = 4;                            /* she hatches at 2 now */
    p.happy = 4;
    CHECK(pet_feed(&p, now) == FEED_OVERFULL, "full pet still eats");
    CHECK(p.happy == 3, "and loses a heart, got %d", p.happy);
    p.hunger = 2;
    CHECK(pet_feed(&p, now) == FEED_HUNGRY, "hungry pet swoons");
    CHECK(p.happy == 3, "a needed cake does not cost a heart");
    p.hunger = 4;
    CHECK(pet_feed(&p, now) == FEED_OVERFULL, "still no cutoff");
    CHECK(p.happy == 2, "another heart, got %d", p.happy);
}

static void test_happy_follows_care(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    p.happy = 1; p.hunger = 4; p.frac_hunger = 0; p.mess = 0;
    advance(&p, 20, &now);
    CHECK(p.happy == 2, "hearts regenerate when fed and clean, got %d", p.happy);

    /* a mess on the floor stops it and starts the slide */
    p.happy = 3; p.mess = 1; p.mess_x[0] = 13; p.frac_happy = 0;
    advance_awake(&p, 200, &now);
    CHECK(p.happy == 2, "hearts drain while messy, got %d", p.happy);
}

/* Awake, the moon bar has to move during a play session. One pip in 8
 * minutes, empty in half an hour, and the next tick is when she nods off
 * (sleep is decided at the start of a tick, from the energy she already has). */
static void test_awake_energy_drops(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    p.energy = 4;
    p.frac_energy = 0;
    p.napping = false;

    advance(&p, 8, &now);
    CHECK(p.energy == 3, "one pip in 8 min, got %d", p.energy);
    CHECK(!p.asleep, "still up after the first pip");

    advance(&p, 24, &now);
    CHECK(p.energy == 0, "empty after half an hour awake, got %d", p.energy);
    CHECK(!p.asleep, "the tick that empties her is still awake");

    advance(&p, 1, &now);
    CHECK(p.asleep && !p.napping, "the next tick she is asleep from exhaustion");
}

/* An empty bar used to wake her the moment one pip came back. She spent
 * that pip and fell asleep again. She stays down until the bar is full,
 * then the next tick gets her up with all four pips. */
static void test_exhaustion_rests_until_full(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    p.energy = 0;
    p.frac_energy = 0;
    p.frac_regen_energy = 0;
    p.napping = false;
    p.asleep = false;
    p.wake_until_ms = 0;

    advance(&p, 1, &now);
    CHECK(p.asleep && !p.napping, "empty bar puts her to sleep");
    CHECK(p.energy == 0, "the first minute has not returned a pip, got %d",
          p.energy);

    advance(&p, 2, &now);
    CHECK(p.asleep, "still asleep when the first pip lands");
    CHECK(p.energy == 1, "one pip after 3 min, got %d", p.energy);

    advance(&p, 8, &now);
    CHECK(p.asleep, "stays down while the bar refills");
    CHECK(p.energy == 3, "three pips at 11 min, got %d", p.energy);

    advance(&p, 1, &now);
    CHECK(p.asleep, "the minute that fills the bar is still sleep");
    CHECK(p.energy == 4, "full after 12 min, got %d", p.energy);

    advance(&p, 1, &now);
    CHECK(!p.asleep, "the next tick she is up");
    CHECK(p.energy == 4, "and she kept the full bar, got %d", p.energy);
}

/* A nap has to pay off inside a four-year-old's attention span: the first
 * pip must land while she is still watching, and empty-to-full in minutes
 * rather than hours. */
static void test_sleep_restores_energy_fast(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    p.energy = 0;
    p.mess = 0;                              /* she hatches with one */
    pet_set_nap(&p, true, now);

    advance(&p, 4, &now);
    CHECK(p.energy >= 1, "first pip inside 4 min, got %d", p.energy);

    advance(&p, 11, &now);
    CHECK(p.energy == 4, "full within ~15 min, got %d", p.energy);
    CHECK(p.mess == 0, "no NEW mess while asleep");

    /* And it must not race away while she is awake. */
    pet_set_nap(&p, false, now);
    p.energy = 1;
    advance(&p, 30, &now);
    CHECK(p.energy <= 1, "awake energy does not refill, got %d", p.energy);
}

/* No clock-driven bedtime at all: the time of day must never put her to
 * sleep. This is the rule that made every button look dead for anyone who
 * flashed the board after 8pm. */
static void test_no_clock_bedtime(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    /* Run a full simulated day. Falling asleep only happens when the bar
     * is empty. Staying asleep while it refills is rest, not a bedtime. */
    bool was_asleep = false;
    for (int i = 0; i < 24 * 60; i++) {
        was_asleep = p.asleep;
        now += MIN(1); pet_tick(&p, now, 1.0);
        if (p.asleep && !p.napping && !was_asleep)
            CHECK(p.energy == 0, "fell asleep at minute %d with energy %d",
                  i, p.energy);
    }
}

/* Exhaustion does put her under - but a manual wake beats it for a while,
 * so a button press always visibly does something. */
static void test_manual_wake_beats_exhaustion(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    p.energy = 0;
    now += MIN(1); pet_tick(&p, now, 1.0);
    CHECK(p.asleep, "flat out of energy means asleep");

    pet_set_nap(&p, false, now);            /* the user presses a button */
    CHECK(!p.asleep, "wakes immediately");
    now += MIN(1); pet_tick(&p, now, 1.0);
    CHECK(!p.asleep, "and STAYS awake through the next tick");

    for (int i = 0; i < 35; i++) { now += MIN(1); pet_tick(&p, now, 1.0); }
    CHECK(p.energy > 0 || p.asleep, "after the override she rests again");

    /* A deliberate nap still sticks even mid-override. */
    pet_set_nap(&p, false, now);
    pet_set_nap(&p, true, now);
    now += MIN(1); pet_tick(&p, now, 1.0);
    CHECK(p.asleep, "a nap always sticks");
}

static void test_cuddle_cooldown(void)
{
    pet_t p; int64_t now = 100000; pet_init(&p, now);
    advance(&p, 2, &now);
    p.happy = 1;
    CHECK(pet_cuddle(&p, now), "first cuddle lands a heart");
    CHECK(p.happy == 2, "heart applied");
    CHECK(!pet_cuddle(&p, now + 500), "spamming earns nothing");
    CHECK(p.happy == 2, "still 2");
    CHECK(pet_cuddle(&p, now + PET_CUDDLE_CD_MS), "after 10s it lands again");
    CHECK(p.happy == 3, "heart applied again");
    p.happy = 4;
    CHECK(!pet_cuddle(&p, now + 100000), "no heart wasted when full");
}

static void test_mess_cap(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    for (int i = 0; i < 500; i++) { now += MIN(1); pet_tick(&p, now, 0.0); }
    CHECK(p.mess == PET_MESS_MAX, "mess caps at %d, got %d", PET_MESS_MAX, p.mess);
    CHECK(pet_clean(&p), "sweeping works");
    CHECK(p.mess == 0, "floor is clear");
    CHECK(!pet_clean(&p), "sweeping a clean floor reports nothing to do");
}

static void test_catch_up_equals_stepwise(void)
{
    /* One big catch-up tick after 8 hours off must match ticking every
     * minute. This is the property the RTC boot path depends on. */
    pet_t a, b; int64_t now = 0;
    pet_init(&a, now); pet_init(&b, now);
    a.stage = b.stage = ST_ROYAL;
    a.born_ms = b.born_ms = now - HOUR(100);

    int64_t t = now;
    for (int i = 0; i < 480; i++) { t += MIN(1); pet_tick(&a, t, 1.0); }
    pet_tick(&b, now + MIN(480), 1.0);

    CHECK(a.hunger == b.hunger, "hunger: stepwise %d vs catch-up %d",
          a.hunger, b.hunger);
    CHECK(a.happy  == b.happy,  "happy: stepwise %d vs catch-up %d",
          a.happy, b.happy);
    CHECK(a.energy == b.energy, "energy: stepwise %d vs catch-up %d",
          a.energy, b.energy);
}

/* The bug that made her ancient after every reboot: the saved timestamps are
 * monotonic (ms since boot) and the RTC is wall clock. Subtracting one from
 * the other gave an "away" time of roughly 56,000 years, so she was instantly
 * eligible for every stage and every meter was flat. */
static void test_save_restore_units(void)
{
    pet_t p; int64_t now = 500000; pet_init(&p, now);
    advance(&p, 2, &now);                     /* hatch */
    advance(&p, 300, &now);                   /* 5 h as a blob */
    p.hunger = 2; p.happy = 3; p.energy = 4;

    const int64_t WALL = 1788000000000LL;     /* a realistic unix ms */
    pet_save_t blob;
    pet_snapshot(&p, now, WALL, true, &blob);

    CHECK(blob.age_ms > 0 && blob.age_ms < 60LL * 60 * 1000 * 24,
          "age stored as a duration, got %lld ms", (long long)blob.age_ms);

    /* Reboot: monotonic clock restarts near zero, wall clock has moved on. */
    int64_t boot = 1200;
    int64_t later = WALL + 2LL * 3600 * 1000;      /* two hours later */
    pet_t q; int64_t away = -1;
    CHECK(pet_restore(&q, &blob, boot, later, true, &away), "restores");

    CHECK(away == 2LL * 3600 * 1000, "away is 2 h, got %lld min",
          (long long)(away / 60000));
    double age_min = pet_age_minutes(&q, boot);
    CHECK(age_min > 400 && age_min < 500,
          "age is ~7 h (5 h lived + 2 h away), got %.0f min", age_min);
    CHECK(q.stage == ST_BLOB, "still a blob, got %s", PET_STAGE_NAME[q.stage]);
    CHECK(q.hunger == 2 && q.happy == 3, "meters survive");

    /* And she must NOT be instantly eligible: 7 h in the blob stage is
     * short of the 8 h dwell. */
    CHECK(pet_pending_grow(&q, boot) == ST_COUNT,
          "not eligible to grow yet");

    /* No RTC: no time passes, but nothing is lost either. */
    pet_t r; away = -1;
    CHECK(pet_restore(&r, &blob, boot, 0, false, &away), "restores with no RTC");
    CHECK(away == 0, "no clock means no elapsed time, got %lld", (long long)away);
    CHECK(r.stage == ST_BLOB && r.hunger == 2, "state still survives");
    double r_age = pet_age_minutes(&r, boot);
    CHECK(r_age > 290 && r_age < 310, "age preserved at ~5 h, got %.0f", r_age);

    /* A clock that has gone backwards must not rewind her. */
    pet_t t2; away = -1;
    CHECK(pet_restore(&t2, &blob, boot, WALL - 99999, true, &away), "restores");
    CHECK(away == 0, "backwards clock clamps to zero, got %lld", (long long)away);

    /* Wrong version is refused rather than misread. Version 3 is the blob
     * an earlier flash left in NVS: it must not come back as a grown cat. */
    pet_save_t old_blob = blob; old_blob.version = 1;
    pet_t u;
    CHECK(!pet_restore(&u, &old_blob, boot, later, true, NULL),
          "a stale blob version is refused");
    old_blob = blob; old_blob.version = 3; old_blob.pet.stage = ST_ROYAL;
    CHECK(!pet_restore(&u, &old_blob, boot, later, true, NULL),
          "a version-3 adult is refused");
    old_blob = blob; old_blob.version = 4;
    CHECK(!pet_restore(&u, &old_blob, boot, later, true, NULL),
          "a version-4 pet from an earlier flash is refused");
    old_blob = blob; old_blob.version = 5;
    CHECK(!pet_restore(&u, &old_blob, boot, later, true, NULL),
          "a version-5 pet from an earlier flash is refused");
    old_blob = blob; old_blob.version = 6;
    CHECK(!pet_restore(&u, &old_blob, boot, later, true, NULL),
          "a version-6 pet from the previous flash is refused");
    old_blob = blob; old_blob.pet.stage = (stage_t)99;
    CHECK(!pet_restore(&u, &old_blob, boot, later, true, NULL),
          "a garbage stage is refused");
}

static int64_t mins_ms(double m)
{
    return (int64_t)(m * 60.0 * 1000.0);
}

static void test_age_label(void)
{
    pet_t p; int64_t now = 100000;
    pet_init(&p, now);
    CHECK(pet_sim_age(&p, now) == 0, "a new pet is 0, got %d", pet_sim_age(&p, now));
    CHECK(pet_sim_age(&p, now + mins_ms(19)) == 0, "19 min is still 0, got %d",
          pet_sim_age(&p, now + mins_ms(19)));
    CHECK(pet_sim_age(&p, now + mins_ms(20)) == 1, "20 min is 1, got %d",
          pet_sim_age(&p, now + mins_ms(20)));
    CHECK(pet_sim_age(&p, now + mins_ms(24 * 20)) == 24, "8 h is 24, got %d",
          pet_sim_age(&p, now + mins_ms(24 * 20)));
    CHECK(pet_sim_age(&p, now + mins_ms(25 * 20 - 1)) == 24,
          "24 lasts 20 min, got %d",
          pet_sim_age(&p, now + mins_ms(25 * 20 - 1)));
    CHECK(pet_sim_age(&p, now + mins_ms(25 * 20)) == 25, "500 min turns 25, got %d",
          pet_sim_age(&p, now + mins_ms(25 * 20)));
    CHECK(pet_sim_age(&p, now + mins_ms(25 * 20 + 119)) == 25,
          "25 lasts 2 h, got %d",
          pet_sim_age(&p, now + mins_ms(25 * 20 + 119)));
    CHECK(pet_sim_age(&p, now + mins_ms(25 * 20 + 120)) == 26, "2 h later is 26, got %d",
          pet_sim_age(&p, now + mins_ms(25 * 20 + 120)));

    /* 25 years at 20 min, 25 at 2 h, then the 50s begin. */
    CHECK(pet_sim_age(&p, now + mins_ms(25 * 20 + 25 * 120)) == 50,
          "after the 2 h years she is 50, got %d",
          pet_sim_age(&p, now + mins_ms(25 * 20 + 25 * 120)));
    CHECK(pet_sim_age(&p, now + mins_ms(25 * 20 + 25 * 120 + 180)) == 51,
          "3 h into the 50s is 51, got %d",
          pet_sim_age(&p, now + mins_ms(25 * 20 + 25 * 120 + 180)));
    CHECK(pet_sim_age(&p, now + mins_ms(25 * 20 + 25 * 120 + 25 * 180)) == 75,
          "after the 3 h years she is 75, got %d",
          pet_sim_age(&p, now + mins_ms(25 * 20 + 25 * 120 + 25 * 180)));
    CHECK(pet_sim_age(&p, now + mins_ms(25 * 20 + 25 * 120 + 25 * 180 + 25 * 300)) == 100,
          "25 years at 5 h reach 100, got %d",
          pet_sim_age(&p, now + mins_ms(25 * 20 + 25 * 120 + 25 * 180 + 25 * 300)));
    CHECK(pet_sim_age(&p, now + mins_ms(100000)) == 100,
          "she stays at 100, got %d", pet_sim_age(&p, now + mins_ms(100000)));
}

/* The interaction loop a four-year-old actually sees: something to do within
 * five minutes, then a long tail so she can't starve or be buried. */
static void test_graded_rates(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    p.hunger = 4; p.frac_hunger = 0; p.mess = 0;

    advance_awake(&p, 6, &now);
    CHECK(p.hunger == 3, "first cupcake gone within 6 min, got %d", p.hunger);

    advance_awake(&p, 30, &now);
    CHECK(p.hunger == 3, "and the second is NOT gone 36 min in, got %d",
          p.hunger);
    advance_awake(&p, 35, &now);
    CHECK(p.hunger == 2, "second goes at about an hour, got %d", p.hunger);

    /* Feeding restarts the clock rather than instantly losing the new pip.
     * (hunger 3, not 4, or the feed is refused as already-full and the
     * stale credit survives - which is how this test found the bug.) */
    p.hunger = 3; p.frac_hunger = 90;   /* stale credit */
    CHECK(pet_feed(&p, now) == FEED_HUNGRY, "the feed is accepted");
    advance(&p, 1, &now);
    CHECK(p.hunger == 4, "a fresh feed is not eaten by old credit, got %d",
          p.hunger);

    /* She must not starve: six hours to empty from full, awake. */
    p.hunger = 4; p.frac_hunger = 0;
    advance_awake(&p, 300, &now);
    CHECK(p.hunger >= 1, "still has a pip after 5 h, got %d", p.hunger);
    advance_awake(&p, 120, &now);
    CHECK(p.hunger == 0, "empty by about 7 h, got %d", p.hunger);
    CHECK(p.frac_hunger == 0, "and does not bank credit at zero");
}

static void test_mood(void)
{
    pet_t p; int64_t now = 0; pet_init(&p, now);
    advance(&p, 2, &now);
    p.hunger = p.happy = p.energy = 4;
    CHECK(pet_mood(&p, false) == MOOD_HAPPY, "all full = happy");
    p.hunger = 2;
    CHECK(pet_mood(&p, false) == MOOD_OK, "middling = ok");
    p.happy = 1;
    CHECK(pet_mood(&p, false) == MOOD_SAD, "one pip = sad");
    CHECK(pet_mood(&p, true) == MOOD_EAT, "eating overrides");
    p.asleep = true;
    CHECK(pet_mood(&p, true) == MOOD_SLEEP, "asleep overrides everything");
}

int main(void)
{
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "hatches awake with chores", test_hatches_awake_with_chores },
        { "starts as an egg",          test_starts_as_egg },
        { "egg can be tapped out",     test_egg_nudge },
        { "never grows unwatched",     test_never_grows_alone },
        { "no stage stacking",         test_old_pet_does_not_rocket_through_stages },
        { "hunger bottoms and refills", test_hunger_bottoms },
        { "overfeed costs a heart",    test_overfeed_costs_a_heart },
        { "grow feed is not overfeed", test_grow_feed_is_not_overfeed },
        { "neglect kills",             test_neglect_kills },
        { "feed results",              test_feed_results },
        { "happy follows care",        test_happy_follows_care },
        { "awake energy drops",        test_awake_energy_drops },
        { "rests until the bar is full", test_exhaustion_rests_until_full },
        { "nap restores energy fast",  test_sleep_restores_energy_fast },
        { "no clock-driven bedtime",   test_no_clock_bedtime },
        { "wake beats exhaustion",     test_manual_wake_beats_exhaustion },
        { "cuddle cooldown",           test_cuddle_cooldown },
        { "mess caps and clears",      test_mess_cap },
        { "catch-up == stepwise",      test_catch_up_equals_stepwise },
        { "save/restore units",        test_save_restore_units },
        { "age label",                 test_age_label },
        { "graded hunger and mess",    test_graded_rates },
        { "mood",                      test_mood },
    };
    for (unsigned i = 0; i < sizeof tests / sizeof *tests; i++) {
        int before = fails;
        tests[i].fn();
        printf("%s  %s\n", fails == before ? "pass" : "FAIL", tests[i].name);
    }
    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
