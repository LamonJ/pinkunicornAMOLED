/* test_app.c — the interaction layer on a PC. Covers the rules that took the
 * most iteration to get right, because those are the ones a refactor will
 * quietly undo. */
#include "../sprinkles/app.h"
#include <stdio.h>
#include <math.h>

extern int64_t hb_now;
extern double  hb_rand;
extern double  hb_rand_ring[8];
extern int     hb_rand_ring_n;
extern int     hb_rand_i;
extern int     hb_beeps, hb_slept;
extern bool    hb_audio;
extern spr_note_t hb_last_beep[48];
extern int     hb_last_beep_n;

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { \
    fails++; printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static app_t A;

static void boot(stage_t st)
{
    hb_now = 1000000; hb_rand = 0.99;
    hb_rand_ring[0] = 0.99; hb_rand_ring_n = 1; hb_rand_i = 0;
    app_init(&A, hb_now);
    A.pet.stage = st;
    A.pet.hunger = A.pet.happy = A.pet.energy = 4;
    A.pet.born_ms = hb_now - (int64_t)(100.0 * 3600 * 1000);
    A.pet.stage_since_ms = hb_now - (int64_t)(100.0 * 3600 * 1000);
    A.pet.last_tick_ms = hb_now;
    A.last_pet_tick_ms = hb_now;
}
/* run frames for n ms */
static void frames(double ms)
{
    for (double e = 0; e < ms; e += 16) {
        hb_now += 16;
        app_advance(&A, 16, hb_now);
    }
}
static void seconds(int n)
{
    for (int i = 0; i < n; i++) {
        hb_now += 1000;
        app_advance(&A, 1000, hb_now);
        app_tick(&A, hb_now);
    }
}

/* ---- the menu is summoned, and it stays ------------------------------- */
static void test_menu_summon(void)
{
    boot(ST_ROYAL);
    CHECK(!app_menu_visible(&A), "hidden by default");
    app_button(&A, hb_now, 0);
    CHECK(app_menu_visible(&A), "top button summons it");

    /* no timeout of any kind, even past the screen-sleep window */
    A.sleep_after_s = 60;
    seconds(300);
    CHECK(app_menu_visible(&A), "still open after 5 minutes");
    CHECK(!A.screen_off, "screen must not doze off under an open menu");

    app_button(&A, hb_now, 0);
    CHECK(!app_menu_visible(&A), "pressing again dismisses it");
}

static void test_menu_dismissed_by_use(void)
{
    boot(ST_ROYAL); A.pet.hunger = 2;
    app_button(&A, hb_now, 0);
    CHECK(app_menu_visible(&A), "open");
    app_touch(&A, hb_now, BTN_FEED_X, BTN_Y);
    CHECK(!app_menu_visible(&A), "using a button dismisses the menu at once");
    /* The action itself waits a beat so the buttons are visibly gone before
     * anything moves - otherwise she starts eating while they are still on
     * screen and you miss the first bite. */
    CHECK(A.pet.hunger == 2, "but has NOT fed her yet, got %d", A.pet.hunger);
    CHECK(A.act_pending == 1, "the feed is queued");
    CHECK(app_busy(&A), "and further taps are ignored meanwhile");

    frames(1100);
    CHECK(A.pet.hunger == 3, "fed a second later, got %d", A.pet.hunger);
    CHECK(A.anim == AN_EAT, "eating animation started");
    CHECK(A.anim_love, "she was hungry, so hearts");
    CHECK(!app_busy(&A), "and the queue is clear");
}

static void test_nearest_button_wins(void)
{
    boot(ST_ROYAL); A.pet.mess = 1; A.pet.mess_x[0] = 13;
    app_button(&A, hb_now, 0);
    app_touch(&A, hb_now, BTN_CLEAN_X - 5, BTN_Y);
    frames(1100);
    CHECK(A.pet.mess == 0, "a tap five px left of the broom still sweeps");

    /* The centre is deliberately NOT a button: that is her body, and a tap
     * there should pet her even with the menu open. */
    boot(ST_ROYAL); A.pet.happy = 1;
    app_button(&A, hb_now, 0);
    app_touch(&A, hb_now, 32, BTN_Y);
    CHECK(A.anim == AN_PET, "the middle of the screen is still the cat");
    CHECK(app_menu_visible(&A), "and the menu stays open");
}

/* The side buttons can be hidden by a case. The cake meter feeds her and
 * a tap on a pile sweeps it, with the same delayed action as the menu. */
static void test_touch_cake_and_mess(void)
{
    boot(ST_ROYAL);
    A.pet.hunger = 2;
    app_touch(&A, hb_now, 18, 11);
    CHECK(A.act_pending == 1, "cake meter queues a feed");
    frames(1100);
    CHECK(A.pet.hunger == 3, "tapping the cake meter feeds her, got %d",
          A.pet.hunger);

    boot(ST_ROYAL);
    A.pet.mess = 1;
    A.pet.mess_x[0] = 13;
    app_touch(&A, hb_now, 13, 50);
    CHECK(A.act_pending == 2, "a pile queues a sweep");
    frames(1100);
    CHECK(A.pet.mess == 0, "tapping the pile sweeps it");
}

static void test_tricks_stand_down_for_menu(void)
{
    boot(ST_ROYAL);
    app_button(&A, hb_now, 0);
    hb_rand = 0.0;                    /* everything that can fire, will */
    seconds(5);
    CHECK(!A.roll_on && !A.peek_on, "no tricks while the menu is open");
    CHECK(app_menu_visible(&A), "menu survived");
}

/* ---- petting ---------------------------------------------------------- */
static void test_cuddle_reaction_never_blocked(void)
{
    boot(ST_ROYAL); A.pet.happy = 1;
    app_touch(&A, hb_now, 32, PET_CY);
    CHECK(A.anim == AN_PET, "she jumps");
    CHECK(A.anim_love, "first cuddle earns a heart");
    CHECK(A.pet.happy == 2, "heart applied");

    /* Inside the cooldown she performs instead of taking a heart. hb_rand is
     * 0.99 here, which lands in the dance branch. */
    hb_now += 500;
    app_touch(&A, hb_now, 32, PET_CY);
    CHECK(A.anim == AN_PET || A.roll_on || A.peek_on,
          "she STILL does something on a spammed tap");
    CHECK(!A.anim_love, "but no heart");
    CHECK(A.pet.happy == 2, "meter unchanged");
    CHECK(A.dance_until > (double)hb_now, "and she shows off");
}

/* ---- growth ----------------------------------------------------------- */
static void test_transformation_flow(void)
{
    boot(ST_UNICORN);                 /* 100 h old, so royal is available */
    CHECK(pet_pending_grow(&A.pet, hb_now) == ST_ROYAL, "eligible");
    seconds(30);
    CHECK(A.pet.stage == ST_UNICORN, "never grows on its own");

    app_button(&A, hb_now, 0);
    app_touch(&A, hb_now, BTN_FEED_X, BTN_Y);
    frames(1100);                            /* the press-to-action beat */
    CHECK(A.evo_queued, "feeding queues the transformation");
    CHECK(A.anim == AN_EAT, "and she eats the cupcake FIRST");
    CHECK(app_busy(&A), "input is locked during it");

    app_touch(&A, hb_now, 32, PET_CY);
    CHECK(A.anim != AN_PET, "taps ignored while it plays out");

    frames(2400);                            /* the meal is 2.25 s now */
    CHECK(A.evo_on, "the light show follows the meal");
    CHECK(A.evo_to == ST_ROYAL, "target stage");

    /* Margins rather than exact edges: the queue drain and the first evo
     * step land in the same frame, so evo_t runs a frame or two ahead. */
    frames(2500);
    CHECK(A.pet.stage == ST_UNICORN, "stage holds until the reveal");
    frames(700);
    CHECK(A.pet.stage == ST_ROYAL, "flips around 3000ms, got %s",
          PET_STAGE_NAME[A.pet.stage]);
    frames(1200);
    CHECK(!A.evo_on, "ends by 4100ms");
    CHECK(A.dance_until > (double)hb_now, "and she dances afterwards");
}

/* ---- nap and screen sleep are different things ------------------------ */
static void test_nap_is_visible(void)
{
    boot(ST_ROYAL); A.pet.energy = 2;
    app_button(&A, hb_now, 1);
    CHECK(A.pet.napping, "bottom button naps her");
    CHECK(!A.screen_off, "the panel stays ON - the nap is the point");
    CHECK(!app_menu_visible(&A), "menu hidden while she sleeps");

    seconds(10);
    CHECK(A.pet.mess == 0, "no mess while asleep");
    CHECK(!A.screen_off, "screen still lit so she can be watched sleeping");

    app_button(&A, hb_now, 0);
    CHECK(!A.pet.napping, "the TOP button also wakes her");
    CHECK(A.anim == AN_WAKE, "with the perk reaction");

    /* Left alone, a nap turns the screen off much sooner than ordinary idle
     * does - there is nothing to watch once she has settled. */
    app_button(&A, hb_now, 1);
    CHECK(A.pet.napping, "napped again");
    seconds(50);
    CHECK(A.screen_off, "and the screen is off within ~45 s of napping");
}

static void test_nap_restores_energy(void)
{
    boot(ST_ROYAL); A.pet.energy = 1;
    app_button(&A, hb_now, 1);
    seconds(60 * 50);
    CHECK(A.pet.energy >= 2, "a nap restores energy, got %d", A.pet.energy);
}

static void test_screen_sleep_and_wake(void)
{
    boot(ST_ROYAL); A.sleep_after_s = 60;
    seconds(70);
    CHECK(A.screen_off, "panel sleeps after a minute idle");
    CHECK(A.pet.napping && A.pet.asleep, "a dark screen puts her to sleep");
    int b = hb_beeps;
    app_picked_up(&A, hb_now);
    CHECK(!A.screen_off, "picking it up wakes it");
    CHECK(!A.pet.napping && !A.pet.asleep, "and she wakes with the screen");
    CHECK(hb_beeps > b, "and she greets you");
    CHECK(A.anim == AN_WAKE, "perk reaction");
}

/* The case hides the side buttons. Leaving the screen dark is how the moon
 * bar comes back: one pip about every 3 minutes, and a tap wakes her with
 * whatever filled in. Light sleep does not tick, so the clock jumps. */
static void test_dark_screen_restores_energy(void)
{
    boot(ST_ROYAL);
    A.pet.energy = 1;
    A.pet.frac_regen_energy = 0;
    A.sleep_after_s = 30;
    seconds(40);
    CHECK(A.screen_off && A.pet.napping, "she is asleep in the dark");

    seconds(3 * 60);
    CHECK(A.screen_off, "still dark");
    CHECK(A.pet.energy >= 2, "ticking while dark fills a pip, got %d",
          A.pet.energy);

    /* The board path: no ticks, then the clock has moved. */
    boot(ST_ROYAL);
    A.pet.energy = 0;
    A.pet.frac_regen_energy = 0;
    A.sleep_after_s = 30;
    seconds(40);
    CHECK(A.pet.energy == 0, "she has only just nodded off, got %d", A.pet.energy);
    hb_now += 12LL * 60 * 1000;
    app_touch(&A, hb_now, 0, 0);
    CHECK(A.act_pending == 0, "lighting the glass does not also feed");
    CHECK(!A.screen_off, "the tap brings the picture back");
    CHECK(!A.pet.napping && !A.pet.asleep, "she is awake");
    CHECK(A.pet.energy == 4, "twelve dark minutes fill an empty bar, got %d",
          A.pet.energy);

    /* A tap on the dark glass gets her up even when the moon is still low.
     * The screen used to come back and leave her asleep, and every tap
     * after that was thrown away. */
    boot(ST_ROYAL);
    A.pet.energy = 0;
    A.pet.frac_regen_energy = 0;
    A.pet.frac_energy = 0;
    A.sleep_after_s = 30;
    seconds(40);
    CHECK(A.screen_off && A.pet.napping, "nodded off");
    hb_now += 4LL * 60 * 1000;
    int beeps = hb_beeps;
    app_touch(&A, hb_now, 0, 0);
    CHECK(A.act_pending == 0, "a dark-screen tap at the corner does not feed");
    CHECK(!A.screen_off, "the picture is back");
    CHECK(hb_beeps > beeps, "she chimes when the tap gets her up");
    CHECK(!A.pet.napping && !A.pet.asleep, "a tap gets her up");
    CHECK(A.pet.wake_until_ms == hb_now + PET_WAKE_OVERRIDE_MS,
          "the tap keeps her up for half an hour");
    CHECK(A.pet.energy >= 1 && A.pet.energy < 4, "one pip, not a full bar, got %d",
          A.pet.energy);
    A.sleep_after_s = 0;             /* the next minute is the energy rule */
    seconds(60);
    CHECK(!A.pet.asleep, "she stays up after the tap");

    boot(ST_EGG);
    A.pet.born_ms = hb_now;
    A.pet.stage_since_ms = hb_now;
    A.pet.last_tick_ms = hb_now;
    A.sleep_after_s = 30;
    seconds(40);
    CHECK(A.pet.stage == ST_EGG, "still an egg");
    CHECK(A.screen_off, "the egg's screen can still go dark");
    CHECK(!A.pet.napping, "an egg does not nap");

    boot(ST_DEAD);
    A.pet.stage = ST_DEAD;
    A.pet.happy = 0;
    A.sleep_after_s = 30;
    seconds(40);
    CHECK(!A.pet.napping, "a dead pet does not nap");
}

static void test_first_tap_after_wake_is_swallowed(void)
{
    boot(ST_ROYAL); A.sleep_after_s = 60;
    seconds(70);
    CHECK(A.screen_off, "asleep");
    app_touch(&A, hb_now, 32, PET_CY);      /* this tap wakes it */
    CHECK(!A.screen_off, "woke");
    int happy = A.pet.happy;
    app_touch(&A, hb_now + 100, 32, PET_CY);
    CHECK(A.pet.happy == happy, "the waking tap must not also pet her");
    app_touch(&A, hb_now + 500, 32, PET_CY);
    CHECK(A.anim == AN_PET, "a later tap does");
}

/* ---- spontaneous behaviour -------------------------------------------- */
static void test_roll_conditions(void)
{
    /* app_tick draws exactly four values: mess, roll, peek, direction.
     * High first = never messes; zeros after = every trick fires. */
    boot(ST_ROYAL);
    hb_rand_ring[0] = 0.99; hb_rand_ring[1] = 0.0;
    hb_rand_ring[2] = 0.0;  hb_rand_ring[3] = 0.0; hb_rand_ring_n = 4;
    A.pet.mess = 1; A.pet.mess_x[0] = 13;
    seconds(3);
    CHECK(!A.roll_on, "no tumbling with a mess on the floor");

    boot(ST_ROYAL);
    hb_rand_ring[0] = 0.99; hb_rand_ring[1] = 0.0;
    hb_rand_ring[2] = 0.99; hb_rand_ring[3] = 0.0; hb_rand_ring_n = 4;
    A.pet.energy = 2;
    seconds(3);
    CHECK(!A.roll_on, "no tumbling when tired");

    boot(ST_ROYAL);
    hb_rand_ring[0] = 0.99; hb_rand_ring[1] = 0.0;
    hb_rand_ring[2] = 0.0;  hb_rand_ring[3] = 0.0; hb_rand_ring_n = 4;
    seconds(1);
    CHECK(A.roll_on, "tumbles when fed, rested and clean");
    frames(1600);
    CHECK(!A.roll_on, "and lands");
}

static void test_peek_pushes_roll_out(void)
{
    boot(ST_ROYAL);
    hb_rand_ring[0] = 0.99; hb_rand_ring[1] = 0.0;
    hb_rand_ring[2] = 0.0;  hb_rand_ring[3] = 0.0; hb_rand_ring_n = 4;
    seconds(1);
    CHECK(A.roll_on, "roll fired first");
    frames(1600);
    double rn = A.roll_next;
    /* force a peek by making the roll ineligible but the peek eligible */
    A.pet.energy = 2;
    seconds(2);
    CHECK(A.peek_on, "peekaboo fired");
    CHECK(A.roll_next >= rn, "and pushed the roll timer out");
    frames(4500);                            /* the sequence is ~4.35 s now */
    CHECK(!A.peek_on, "peekaboo ends");
}

static void test_egg_tap(void)
{
    boot(ST_EGG);
    A.pet.born_ms = hb_now; A.pet.last_tick_ms = hb_now;
    A.pet.stage_since_ms = hb_now;
    CHECK(!app_menu_visible(&A), "no menu on an egg");

    /* Every knock must make a noise: a silent press on the very first thing
     * a child touches teaches them the buttons are decoration. */
    int before = hb_beeps;
    app_touch(&A, hb_now, 32, 36);
    CHECK(hb_beeps > before, "tapping the egg beeps");

    before = hb_beeps;
    app_button(&A, hb_now, 0);
    CHECK(hb_beeps > before, "a side button on the egg beeps too");
    before = hb_beeps;
    app_button(&A, hb_now, 1);
    CHECK(hb_beeps > before, "and so does the other one");

    for (int i = 0; i < 8; i++) app_touch(&A, hb_now, 32, 36);
    seconds(1);
    CHECK(A.pet.stage == ST_BLOB, "tapping hatches it early");
}

/* ---- frame composition doesn't crash or leave state dirty ------------- */
/* A new pet is 0: one digit, top row ###, centred at x = 30.
 * 500 min is 25: two digits, width 7, starting at x = 28, top row ###. */
static void test_age_is_drawn(void)
{
    boot(ST_EGG);
    A.pet.born_ms = hb_now;
    app_frame(&A, 0, hb_now);
    CHECK(gfx_fb[58 * GFX_W + 30] == C_COATHI, "a new egg shows age 0");

    boot(ST_BLOB);
    A.pet.born_ms = hb_now - 500LL * 60 * 1000;
    app_frame(&A, 0, hb_now);
    CHECK(gfx_fb[58 * GFX_W + 28] == C_COATHI, "500 min shows age 25");
}

/* The number going up is a birthday. One tune, not one per skipped year,
 * and not on the tick that first notices how old she already is. */
static void test_birthday_tune(void)
{
    boot(ST_BLOB);
    A.sleep_after_s = 0;             /* this test is the song, not the nap */
    A.pet.born_ms = hb_now;
    A.pet.stage_since_ms = hb_now;
    int before = hb_beeps;
    seconds(1);
    CHECK(hb_beeps == before, "the first look at her age is silent");
    CHECK(A.birthday_age == 0, "age 0 is remembered, got %d", A.birthday_age);

    seconds(19 * 60);
    CHECK(pet_sim_age(&A.pet, hb_now) == 0, "19 min is still 0, got %d",
          pet_sim_age(&A.pet, hb_now));
    CHECK(hb_beeps == before, "no tune before the year");

    seconds(60);
    CHECK(pet_sim_age(&A.pet, hb_now) == 1, "20 min is 1, got %d",
          pet_sim_age(&A.pet, hb_now));
    CHECK(hb_beeps == before + 1, "one song for the birthday, got %d extra",
          hb_beeps - before);
    int sung = 0;
    for (int i = 0; i < hb_last_beep_n; i++) sung += hb_last_beep[i].ms;
    CHECK(hb_last_beep_n >= 24, "the birthday is a song, got %d notes",
          hb_last_beep_n);
    CHECK(sung >= 10000, "it lasts for the celebration, got %d ms", sung);
    CHECK(hb_last_beep[0].hz == 392 && hb_last_beep[2].hz == 440
          && hb_last_beep[5].hz == 494, "it opens with Happy Birthday");
    CHECK(hb_last_beep[hb_last_beep_n - 1].hz == 523
          && hb_last_beep[hb_last_beep_n - 1].ms >= 1000,
          "and the last note is held");

    before = hb_beeps;
    seconds(20 * 60);
    CHECK(pet_sim_age(&A.pet, hb_now) == 2, "the next year, got %d",
          pet_sim_age(&A.pet, hb_now));
    CHECK(hb_beeps == before + 1, "and another tune");

    /* Several years in one tick is still one tune. */
    A.pet.born_ms = hb_now - (int64_t)25 * 20 * 60 * 1000;
    before = hb_beeps;
    seconds(1);
    CHECK(pet_sim_age(&A.pet, hb_now) == 25, "jumped to 25, got %d",
          pet_sim_age(&A.pet, hb_now));
    CHECK(hb_beeps == before + 1, "one tune for the jump");
    CHECK(A.birthday_age == 25, "remembered at 25");

    boot(ST_ROYAL);
    before = hb_beeps;
    seconds(2);
    CHECK(hb_beeps == before, "waking up already older does not celebrate");

    boot(ST_BLOB);
    A.pet.born_ms = hb_now - 19LL * 60 * 1000;
    A.pet.stage = ST_DEAD;
    A.pet.happy = 0;
    seconds(1);
    before = hb_beeps;
    seconds(2 * 60);
    CHECK(pet_sim_age(&A.pet, hb_now) >= 1, "the number still moves, got %d",
          pet_sim_age(&A.pet, hb_now));
    CHECK(hb_beeps == before, "a dead pet does not get a birthday tune");
}

static void test_sleeping_tap_wakes_and_acts(void)
{
    boot(ST_ROYAL);
    A.pet.napping = true;
    A.pet.asleep = true;
    A.pet.energy = 1;
    A.pet.hunger = 2;
    app_touch(&A, hb_now, 18, 11);
    CHECK(!A.pet.asleep && !A.pet.napping, "a cake tap wakes her");
    CHECK(A.pet.wake_until_ms == hb_now + PET_WAKE_OVERRIDE_MS, "and she stays up");
    CHECK(A.act_pending == 1, "and that tap still feeds");
    frames(1100);
    CHECK(A.pet.hunger == 3, "the cake landed, got %d", A.pet.hunger);

    boot(ST_ROYAL);
    A.pet.asleep = true;
    A.pet.energy = 0;
    A.pet.mess = 1;
    A.pet.mess_x[0] = 13;
    app_touch(&A, hb_now, 13, 61);
    CHECK(!A.pet.asleep, "a low tap on the pile wakes her");
    CHECK(A.act_pending == 2, "and queues the sweep");
    frames(1100);
    CHECK(A.pet.mess == 0, "the pile is gone");

    boot(ST_ROYAL);
    A.pet.asleep = true;
    A.pet.sound = false;
    app_touch(&A, hb_now, 30, 20);
    CHECK(A.pet.sound, "tapping MUTE turns the sound on");
    CHECK(hb_audio, "and the speaker follows");
    CHECK(!A.pet.asleep, "and she is awake");

    boot(ST_ROYAL);
    A.pet.asleep = true;
    A.pet.hunger = 2;
    app_touch(&A, hb_now, 32, (int)PET_CY);
    CHECK(!A.pet.asleep, "a tap on her wakes her");
    CHECK(A.act_pending == 0, "and does not feed");
}

/* The cake is a few pixels in the corner. The whole top of the glass feeds,
 * including the moon side and a press that lands below the icon. The bottom
 * still sweeps, and her belly does not feed. */
static void test_generous_cake_and_bottom(void)
{
    boot(ST_ROYAL);
    A.pet.hunger = 2;
    app_touch(&A, hb_now, 2, 2);
    CHECK(A.act_pending == 1, "the top left of the glass feeds");
    frames(1100);
    CHECK(A.pet.hunger == 3, "that cake landed, got %d", A.pet.hunger);

    boot(ST_ROYAL);
    A.pet.hunger = 2;
    app_touch(&A, hb_now, 58, 6);
    CHECK(A.act_pending == 1, "the top right feeds too");
    frames(1100);
    CHECK(A.pet.hunger == 3, "the moon-side tap fed her, got %d", A.pet.hunger);

    boot(ST_ROYAL);
    A.pet.hunger = 2;
    app_touch(&A, hb_now, 8, 30);
    CHECK(A.act_pending == 1, "a press below the icon still feeds");

    boot(ST_ROYAL);
    A.pet.hunger = 2;
    app_touch(&A, hb_now, 32, 40);
    CHECK(A.act_pending == 0, "her belly is not the cake");
    CHECK(A.pet.hunger == 2, "hunger unchanged");

    boot(ST_ROYAL);
    A.pet.mess = 1;
    A.pet.mess_x[0] = 13;
    A.pet.hunger = 2;
    app_touch(&A, hb_now, 45.9, 61.1);
    CHECK(A.act_pending == 2, "the bottom of the glass sweeps");
    CHECK(A.pet.hunger == 2, "and does not feed");
    frames(1100);
    CHECK(A.pet.mess == 0, "the pile is gone");

    boot(ST_ROYAL);
    A.pet.mess = 1;
    A.pet.mess_x[0] = 13;
    A.pet.hunger = 1;
    app_touch(&A, hb_now, 48, 40);
    CHECK(A.act_pending == 0, "her side does not sweep");
    CHECK(A.pet.hunger == 1, "and does not feed");

    boot(ST_ROYAL);
    A.pet.hunger = 2;
    A.pet.sound = true;
    CHECK(!app_hold(&A, hb_now, 1, 12, 12), "finger down on the cake");
    CHECK(!app_hold(&A, hb_now + APP_HOLD_MS, 1, 14, 12), "holding the cake does not mute");
    CHECK(A.pet.sound, "sound stays on");
    CHECK(!A.hold_fired, "lifting off the cake can still feed");
}

/* A still hold mutes. Another hold brings the sound back. The word MUTE
 * is on screen the whole time it is quiet, and a new egg keeps the choice. */
static void test_hold_mutes(void)
{
    boot(ST_ROYAL);
    int64_t t = hb_now;
    int beeps = hb_beeps;
    CHECK(A.pet.sound && hb_audio, "sound starts on");
    CHECK(!app_hold(&A, t, 1, 32, 44), "putting the finger down is not a mute");
    CHECK(!app_hold(&A, t + APP_HOLD_MS - 1, 1, 33, 44), "a short hold waits");
    CHECK(A.pet.sound, "still heard");
    CHECK(app_hold(&A, t + APP_HOLD_MS, 1, 32, 45), "the long hold mutes");
    CHECK(!A.pet.sound && !hb_audio, "speaker is off");
    CHECK(hb_beeps == beeps, "muting itself is silent");
    CHECK(!app_hold(&A, t + APP_HOLD_MS + 400, 1, 32, 45), "holding longer does not toggle again");
    CHECK(A.hold_fired, "the release should not also be a tap");

    app_frame(&A, 0, hb_now);
    CHECK(gfx_fb[18 * GFX_W + 24] == C_STAR, "MUTE is drawn");
    CHECK(gfx_dim == 1.0, "the label restores brightness");

    app_hold(&A, t + 2000, 0, 0, 0);
    CHECK(!app_hold(&A, t + 2100, 1, 32, 44), "finger down again");
    CHECK(!A.pet.sound, "not yet");
    beeps = hb_beeps;
    CHECK(app_hold(&A, t + 2100 + APP_HOLD_MS, 1, 33, 44), "the second hold unmutes");
    CHECK(A.pet.sound && hb_audio, "speaker is back");
    CHECK(hb_beeps == beeps + 1, "unmuting plays a note");

    /* A slide is not a mute, and it must not eat the tap on the way up. */
    app_hold(&A, t + 4000, 0, 0, 0);
    app_hold(&A, t + 4100, 1, 10, 10);
    CHECK(!app_hold(&A, t + 4100 + APP_HOLD_MS, 1, 40, 10), "a drag does not mute");
    CHECK(A.pet.sound, "still on after the drag");
    CHECK(!A.hold_fired, "a slide can still be a tap");

    boot(ST_DEAD);
    A.pet.happy = 0;
    app_hold(&A, hb_now, 1, 32, 40);
    app_hold(&A, hb_now + APP_HOLD_MS, 1, 32, 40);
    CHECK(!A.pet.sound, "muted on the dead screen");
    app_hold(&A, hb_now + APP_HOLD_MS + 50, 0, 0, 0);
    app_touch(&A, hb_now + APP_HOLD_MS + 80, 32, 50);
    CHECK(A.pet.stage == ST_EGG, "hatch still starts an egg");
    CHECK(!A.pet.sound, "and the speaker stays muted");
}

/* Full bars still take a cake. Each one costs a heart. The fourth kills,
 * and the hatch button is the only way back to an egg. */
static void test_overfeed_and_hatch(void)
{
    boot(ST_ROYAL);
    A.pet.hunger = 4;
    A.pet.happy = 4;

    for (int i = 0; i < 3; i++) {
        app_touch(&A, hb_now, 20, 10);
        frames(1100);
        CHECK(A.pet.stage == ST_ROYAL, "alive after extra cake %d", i + 1);
        CHECK(A.pet.hunger == 4, "cake stays full");
        CHECK(A.pet.happy == 3 - i, "heart now %d, got %d", 3 - i, A.pet.happy);
        CHECK(A.anim == AN_EAT && !A.anim_love, "she eats it, no hearts");
    }
    app_touch(&A, hb_now, 20, 10);
    frames(1100);
    CHECK(A.pet.stage == ST_DEAD, "the last heart kills her");
    CHECK(A.pet.happy == 0, "hearts are gone");
    CHECK(A.anim != AN_EAT, "the dead picture replaces the bite");

    app_frame(&A, 0, hb_now);
    int dark = 0, coat = 0;
    for (int y = HATCH_Y0; y <= HATCH_Y1; y++) {
        for (int x = HATCH_X0; x <= HATCH_X1; x++) {
            rgb_t c = gfx_fb[y * GFX_W + x];
            if (c == C_DARK) dark++;
            if (c == C_COAT) coat++;
        }
    }
    CHECK(dark > 30, "HATCH NEW EGG is drawn, got %d dark px", dark);
    CHECK(coat > 40, "the button is filled, got %d coat px", coat);
    CHECK(gfx_dim == 1.0, "dead frame restores brightness");

    unsigned long sum0 = 0, sum1 = 0;
    app_frame(&A, 0, hb_now);
    for (int i = 0; i < 40 * GFX_W; i++) sum0 += gfx_fb[i];
    app_frame(&A, 900, hb_now);
    for (int i = 0; i < 40 * GFX_W; i++) sum1 += gfx_fb[i];
    CHECK(sum0 != sum1, "the dead scene moves");

    app_touch(&A, hb_now, 10, 10);
    CHECK(A.pet.stage == ST_DEAD, "a miss does nothing");
    app_button(&A, hb_now, 1);
    CHECK(A.pet.stage == ST_DEAD && !A.pet.napping, "a side button does not nap her");
    app_touch(&A, hb_now, 32, 50);
    CHECK(A.pet.stage == ST_EGG, "Hatch a New Egg starts over");
    CHECK(A.pet.hunger == 4 && A.pet.happy == 4 && A.pet.energy == 4,
          "the egg's meters are full");
}

static void test_frame_all_states(void)
{
    struct { const char *name; stage_t st; int nap, menu, roll, peek, evo; } cases[] = {
        { "egg",     ST_EGG,     0,0,0,0,0 },
        { "dead",    ST_DEAD,    0,0,0,0,0 },
        { "blob",    ST_BLOB,    0,0,0,0,0 },
        { "kitten",  ST_KITTEN,  0,0,0,0,0 },
        { "unicorn", ST_UNICORN, 0,0,0,0,0 },
        { "royal",   ST_ROYAL,   0,0,0,0,0 },
        { "nap",     ST_ROYAL,   1,0,0,0,0 },
        { "menu",    ST_ROYAL,   0,1,0,0,0 },
        { "roll",    ST_ROYAL,   0,0,1,0,0 },
        { "peek",    ST_ROYAL,   0,0,0,1,0 },
        { "evo",     ST_ROYAL,   0,0,0,0,1 },
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        boot(cases[i].st);
        if (cases[i].nap)  { A.pet.napping = true; A.pet.asleep = true; }
        if (cases[i].menu) A.menu = true;
        if (cases[i].roll) { A.roll_on = true; A.roll_t = 700; }
        if (cases[i].peek) { A.peek_on = true; A.peek_t = 900; }
        if (cases[i].evo)  { A.evo_on = true; A.evo_t = 2600;
                             A.evo_from = ST_UNICORN; A.evo_to = ST_ROYAL; }
        for (double t = 0; t < 5000; t += 137) app_frame(&A, t, hb_now);
        CHECK(gfx_dim == 1.0, "%s: left gfx_dim at %.2f - a leak like this "
              "dims the NEXT frame", cases[i].name, gfx_dim);
        CHECK(gfx_silh_on == 0, "%s: left the silhouette flag set", cases[i].name);
        CHECK(gfx_dst == gfx_fb, "%s: left drawing into the rotation buffer",
              cases[i].name);
    }
}

/* START_AS_EGG waits on a button. Time passing must not hatch her, and
 * the clock starts at the press, not at boot. */
static void test_start_button_holds_the_egg(void)
{
    CHECK(!app_gift_needs_start(0, NULL, "build"), "0 never waits");
    CHECK(!app_gift_needs_start(0, "old", "new"), "0 keeps her across uploads");
    CHECK(app_gift_needs_start(1, NULL, "build"), "1 with no stamp waits");
    CHECK(app_gift_needs_start(1, "", "build"), "an empty stamp waits");
    CHECK(app_gift_needs_start(1, "Oct 1 2026 00:00:00", "Oct 2 2026 00:00:00"),
          "a new upload waits again");
    CHECK(!app_gift_needs_start(1, "same", "same"), "the same upload keeps her");

    boot(ST_EGG);
    A.pet.born_ms = hb_now;
    A.pet.stage_since_ms = hb_now;
    A.pet.last_tick_ms = hb_now;
    app_wait_for_start(&A);
    int64_t born = A.pet.born_ms;
    int beeps = hb_beeps;

    app_frame(&A, 0, hb_now);
    CHECK(gfx_fb[29 * GFX_W + 22] == C_DARK, "START is drawn");
    CHECK(gfx_dim == 1.0, "the button restores brightness");

    app_touch(&A, hb_now, 4, 60);
    CHECK(A.awaiting_start, "a miss does not begin the egg");
    CHECK(A.pet.stage == ST_EGG, "still an egg");
    CHECK(hb_beeps == beeps, "a miss is silent");

    app_button(&A, hb_now, 0);
    CHECK(A.awaiting_start, "a side button is not Start");
    CHECK(A.anim != AN_NUDGE, "and it does not knock the egg");
    CHECK(A.pet.born_ms == born, "her clock did not move");

    seconds(90);
    CHECK(A.awaiting_start, "waiting out the hatch time");
    CHECK(A.pet.stage == ST_EGG, "she did not hatch in the box, got %s",
          PET_STAGE_NAME[A.pet.stage]);
    CHECK(A.pet.born_ms == born, "born time stays at boot until the button");

    A.sleep_after_s = 30;
    A.idle_last_ms = hb_now;
    seconds(40);
    CHECK(A.screen_off, "the glass can sleep while she waits");
    CHECK(A.pet.stage == ST_EGG, "sleeping did not hatch her");
    app_touch(&A, hb_now, 32, 32);
    CHECK(A.awaiting_start, "the tap that lights the glass is not Start");
    CHECK(!A.screen_off, "the picture is back");

    app_touch(&A, hb_now, 32, 32);
    CHECK(!A.awaiting_start, "the button begins her");
    CHECK(A.pet.stage == ST_EGG, "and she is an egg");
    CHECK(A.pet.born_ms == hb_now, "the clock starts at the press");
    CHECK(hb_beeps == beeps + 1, "the press has a chime");

    seconds(44);
    CHECK(A.pet.stage == ST_EGG, "44 seconds is still the egg");
    seconds(2);
    CHECK(A.pet.stage == ST_BLOB, "she hatches from the press, not from boot, got %s",
          PET_STAGE_NAME[A.pet.stage]);
}

int main(void)
{
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "menu is summoned and stays",   test_menu_summon },
        { "using a button dismisses it",  test_menu_dismissed_by_use },
        { "nearest button wins",          test_nearest_button_wins },
        { "cake and piles work by touch", test_touch_cake_and_mess },
        { "tricks stand down for menu",   test_tricks_stand_down_for_menu },
        { "cuddle never blocked",         test_cuddle_reaction_never_blocked },
        { "transformation flow",          test_transformation_flow },
        { "nap is visible, not blank",    test_nap_is_visible },
        { "nap restores energy",          test_nap_restores_energy },
        { "screen sleep and wake",        test_screen_sleep_and_wake },
        { "dark screen restores energy",  test_dark_screen_restores_energy },
        { "waking tap is swallowed",      test_first_tap_after_wake_is_swallowed },
        { "sleeping tap wakes and acts",  test_sleeping_tap_wakes_and_acts },
        { "cake and bottom are generous", test_generous_cake_and_bottom },
        { "start button holds the egg",   test_start_button_holds_the_egg },
        { "roll conditions",              test_roll_conditions },
        { "peek pushes roll out",         test_peek_pushes_roll_out },
        { "egg can be tapped out",        test_egg_tap },
        { "every state composes clean",   test_frame_all_states },
        { "age is drawn",                 test_age_is_drawn },
        { "birthday tune",                test_birthday_tune },
        { "hold mutes",                   test_hold_mutes },
        { "overfeed and hatch",           test_overfeed_and_hatch },
    };
    for (unsigned i = 0; i < sizeof tests / sizeof *tests; i++) {
        int before = fails;
        tests[i].fn();
        printf("%s  %s\n", fails == before ? "pass" : "FAIL", tests[i].name);
    }
    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
