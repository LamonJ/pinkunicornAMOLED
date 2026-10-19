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
    int b = hb_beeps;
    app_picked_up(&A, hb_now);
    CHECK(!A.screen_off, "picking it up wakes it");
    CHECK(hb_beeps > b, "and she greets you");
    CHECK(A.anim == AN_WAKE, "perk reaction");
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
static void test_frame_all_states(void)
{
    struct { const char *name; stage_t st; int nap, menu, roll, peek, evo; } cases[] = {
        { "egg",     ST_EGG,     0,0,0,0,0 },
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

int main(void)
{
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "menu is summoned and stays",   test_menu_summon },
        { "using a button dismisses it",  test_menu_dismissed_by_use },
        { "nearest button wins",          test_nearest_button_wins },
        { "tricks stand down for menu",   test_tricks_stand_down_for_menu },
        { "cuddle never blocked",         test_cuddle_reaction_never_blocked },
        { "transformation flow",          test_transformation_flow },
        { "nap is visible, not blank",    test_nap_is_visible },
        { "nap restores energy",          test_nap_restores_energy },
        { "screen sleep and wake",        test_screen_sleep_and_wake },
        { "waking tap is swallowed",      test_first_tap_after_wake_is_swallowed },
        { "roll conditions",              test_roll_conditions },
        { "peek pushes roll out",         test_peek_pushes_roll_out },
        { "egg can be tapped out",        test_egg_tap },
        { "every state composes clean",   test_frame_all_states },
    };
    for (unsigned i = 0; i < sizeof tests / sizeof *tests; i++) {
        int before = fails;
        tests[i].fn();
        printf("%s  %s\n", fails == before ? "pass" : "FAIL", tests[i].name);
    }
    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
