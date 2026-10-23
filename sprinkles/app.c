#include "app.h"
#include <math.h>
#include <string.h>

/* ---- sounds ----------------------------------------------------------- */
/* Square waves, same shapes as the prototype. Kept here rather than in
 * board.c because which sound plays is a design decision, not a driver one. */
static const spr_note_t SND_STAGE[]    = { {780,140},{980,140},{1180,150},{1560,300} };
static const spr_note_t SND_EVO_START[]= { {620,90},{780,90},{940,90},{1120,140} };
static const spr_note_t SND_EVO_FLIP[] = { {880,90},{1180,90},{1560,90},{1980,260} };
static const spr_note_t SND_FEED_HUNGRY[]={{760,80},{980,80},{1240,120},{1480,90},{1760,140}};
static const spr_note_t SND_FEED_PLAIN[]={ {760,80},{980,80},{1240,120} };
static const spr_note_t SND_REFUSE[]   = { {300,180} };
/* Note lengths here are the third pass. 60-90 ms notes are simply not long
 * enough to register on a small speaker after the amplifier's turn-on delay
 * - you hear that something happened without hearing what. 130 ms is about
 * the floor for a note you can actually identify.
 *
 * The cost: board_beep blocks, so a 500 ms cue drops about seven frames at
 * 15 fps. Acceptable for a press, which is why the long ones are on
 * deliberate actions and the autonomous cues stay shorter. */
static const spr_note_t SND_CUDDLE_H[] = { {900,130},{1200,130},{1500,150},{1900,220} };
static const spr_note_t SND_CUDDLE[]   = { {900,150},{1250,200} };
/* Was the shortest cue in the set at 200 ms of very high notes, which is
 * exactly what an amplifier still waking up swallows. Longer and lower. */
static const spr_note_t SND_CLEAN[]    = { {1100,130},{800,130},{1300,150},{900,200} };
static const spr_note_t SND_NAP[]      = { {700,160},{520,220} };
static const spr_note_t SND_WAKE[]     = { {900,140},{1300,200} };
static const spr_note_t SND_MENU_ON[]  = { {1100,110},{1400,140} };
static const spr_note_t SND_MENU_OFF[] = { {900,150} };
static const spr_note_t SND_ROLL[]     = { {880,130},{1180,150} };
static const spr_note_t SND_ROLL_LAND[]= { {620,170} };
static const spr_note_t SND_PEEK[]     = { {1250,120},{950,120},{700,170} };
static const spr_note_t SND_PEEK_BACK[]= { {820,180} };
static const spr_note_t SND_PEEK_HOME[]= { {1000,140},{1350,220} };
static const spr_note_t SND_DANCE[]    = { {800,130},{1000,130},{1250,150},{1500,200} };
/* Happy Birthday, sung. The melody is old enough to be free to use.
 * A birthday is the one moment the picture may sit still: the song
 * is the celebration, and it runs about thirteen seconds. */
static const spr_note_t SND_BIRTHDAY[] = {
    /* Happy birthday to you */
    {392,250},{392,250},{440,500},{392,500},{523,500},{494,1000},{0,250},
    /* Happy birthday to you */
    {392,250},{392,250},{440,500},{392,500},{587,500},{523,1000},{0,250},
    /* Happy birthday, dear Sprinkles */
    {392,250},{392,250},{784,500},{659,500},{523,500},{494,500},{440,1000},{0,250},
    /* Happy birthday to you */
    {698,250},{698,250},{659,500},{523,500},{587,500},{523,1200},
};
/* Things she does on her own that used to happen in silence. */
static const spr_note_t SND_POOP[]     = { {520,170},{380,230} };
static const spr_note_t SND_READY[]    = { {900,140},{1200,140},{1500,160},{1900,260} };
static const spr_note_t SND_LOW[]      = { {600,200},{440,260} };
static const spr_note_t SND_DIE[]      = { {420,240},{280,280},{160,360} };
/* Acknowledges the press itself, since the action it triggers is a second
 * behind. Without this the button feels dead for that whole second. */
static const spr_note_t SND_PRESS[]    = { {1300,90},{1000,120} };
static const spr_note_t SND_UNMUTE[]   = { {880,140},{1320,200} };

#define PLAY(x) board_beep((x), (int)(sizeof(x)/sizeof(spr_note_t)))

/* ---- tuning ----------------------------------------------------------- */
#define DANCE_MS         4000.0
#define DANCE_COOLDOWN  25000.0
#define DANCE_CHANCE        0.22
#define ROLL_END         1500.0
#define ROLL_COOLDOWN   22000.0
#define ROLL_CHANCE         0.14
#define PEEK_END         4350.0
#define PEEK_COOLDOWN   34000.0
#define PEEK_CHANCE         0.09
#define EVO_END          4100.0
#define EVO_FLIP_AT      3000.0
/* Eating and sweeping are both 50% slower than the first draft. At 15 fps a
 * three-bite cupcake in 1.5 s gave each bite about seven frames, and the
 * broom crossed the screen in under a second - both read as a flicker
 * rather than an action. */
#define EAT_MS           2250.0
#define BITE_MS           750.0
#define LOVE_FROM        1200.0
#define LOVE_TO          3000.0
#define PET_LOVE_FROM     250.0
#define PET_LOVE_TO      1750.0
#define PERK_MS           800.0
#define CLEAN_MS         1350.0
/* Long enough for the menu to be visibly gone before anything moves. */
#define ACT_DELAY_MS     1000.0
#define NUDGE_MS          400.0

void app_init(app_t *a, int64_t now_ms)
{
    memset(a, 0, sizeof(*a));
    pet_init(&a->pet, now_ms);
    /* Three quiet minutes, then the panel sleeps. A minute was short enough
     * that she blinked off while you were still watching. A nap still blanks
     * sooner: once she has curled up there is less to look at. */
    a->sleep_after_s = 180;
    a->nap_sleep_after_s = 45;
    a->peek_dir = -1;
    a->idle_last_ms = now_ms;
    a->last_pet_tick_ms = now_ms;
    a->birthday_age = -1;            /* the first tick learns her age quietly */
}

static void play(app_t *a, anim_kind_t k)
{
    a->anim = k; a->anim_t = 0; a->anim_love = false;
}

/* Every press on the egg is a knock, and the pitch climbs as she gets closer
 * to hatching - so the child can hear the progress they are making, not just
 * watch the cracks appear. A little jitter keeps consecutive taps from
 * sounding mechanical. */
static void egg_tap(app_t *a, int64_t now_ms)
{
    double p = pet_age_minutes(&a->pet, now_ms) / PET_STAGE_AT[ST_BLOB];
    if (p < 0) p = 0;
    if (p > 1) p = 1;

    int base = 380 + (int)(p * 300) + (int)(board_rand01() * 40);
    spr_note_t knock[3] = {
        { base,               110 },
        { (int)(base * 1.5),  150 },
        { (int)(base * 2.0),  p > 0.8 ? 190 : 0 }   /* a third note near the end */
    };
    pet_nudge_egg(&a->pet);
    play(a, AN_NUDGE);
    board_beep(knock, p > 0.8 ? 3 : 2);
}

/* Defined below, but app_advance runs the queued action. */
static void do_feed(app_t *a, int64_t now_ms);
static void do_clean(app_t *a);

bool app_busy(const app_t *a)
{
    return a->evo_on || a->evo_queued || a->roll_on || a->peek_on ||
           a->act_pending != 0;
}

/* The menu is summoned by the top button and nothing takes it away except
 * using it or dismissing it. Her tricks stand down while it is open. */
bool app_menu_visible(const app_t *a)
{
    if (a->pet.stage == ST_EGG || a->pet.stage == ST_DEAD || a->pet.asleep ||
        a->evo_on || a->screen_off || a->dev_menu)
        return false;
    return a->menu;
}

static bool can_roll(const app_t *a)
{
    return !a->pet.asleep && !a->evo_on && !a->roll_on && !a->peek_on && !a->menu
        && a->pet.hunger >= PET_FED_ENOUGH && a->pet.energy >= 3 && a->pet.mess == 0
        && a->pet.stage != ST_EGG && a->pet.stage != ST_DEAD;
}
static bool can_peek(const app_t *a)
{
    return !a->pet.asleep && !a->evo_on && !a->roll_on && !a->peek_on && !a->menu
        && a->pet.happy >= 3 && a->pet.energy >= 2
        && a->pet.stage != ST_EGG && a->pet.stage != ST_DEAD;
}

static void wake_screen(app_t *a, int64_t now_ms)
{
    a->idle_last_ms = now_ms;
    if (!a->screen_off) return;

    /* The panel was dark, and the main loop was not ticking. She has been
     * asleep since it went dark, so credit that stretch before she wakes.
     * Afterwards the same gap would count as awake time and the moon bar
     * would fall instead of filling. */
    if (a->pet.napping)
        pet_tick(&a->pet, now_ms, 1.0);

    a->screen_off = false;
    a->woke_at_ms = now_ms;
    if (pet_screen_woke(&a->pet)) {
        play(a, AN_WAKE);
        PLAY(SND_WAKE);
    }
}

static void set_nap(app_t *a, bool nap, int64_t now_ms)
{
    if (nap) { pet_set_nap(&a->pet, true, now_ms); PLAY(SND_NAP); }
    else     { pet_set_nap(&a->pet, false, now_ms); play(a, AN_WAKE); PLAY(SND_WAKE); }
    a->idle_last_ms = now_ms;
}

/* ---- once a second ---------------------------------------------------- */
void app_tick(app_t *a, int64_t now_ms)
{
    /* Draw all randomness up front, exactly four values per tick regardless
     * of which branch we take. Otherwise the number of calls depends on the
     * control flow, which makes behaviour hard to reproduce — and tests
     * impossible to write. */
    double r_mess = board_rand01();
    double r_roll = board_rand01();
    double r_peek = board_rand01();
    double r_dir  = board_rand01();

    /* Watch for things that happen to her rather than to her buttons, so
     * they can be given a voice. Silent events on a small screen are easy
     * for a child to miss entirely. */
    stage_t was = a->pet.stage;
    int mess_was = a->pet.mess;
    bool ready_was = pet_pending_grow(&a->pet, now_ms) != ST_COUNT;
    int low_was = a->pet.hunger <= 1 || a->pet.happy <= 1;

    pet_tick(&a->pet, now_ms, r_mess);

    if (was != ST_DEAD && a->pet.stage == ST_DEAD) {
        a->menu = false;
        a->roll_on = false;
        a->peek_on = false;
        a->evo_on = false;
        a->evo_queued = false;
        a->anim = AN_NONE;
        a->act_pending = 0;
        a->pressed = 0;
        PLAY(SND_DIE);
    }

    if (a->pet.stage != ST_DEAD) {
        if (a->pet.mess > mess_was) PLAY(SND_POOP);
        if (!ready_was && pet_pending_grow(&a->pet, now_ms) != ST_COUNT)
            PLAY(SND_READY);                 /* she has started shimmering */
        if (!low_was && (a->pet.hunger <= 1 || a->pet.happy <= 1))
            PLAY(SND_LOW);                   /* a meter just got worrying */
    }
    a->last_pet_tick_ms = now_ms;

    if (was == ST_EGG && a->pet.stage != ST_EGG) {
        PLAY(SND_STAGE);
        a->dance_until = (double)now_ms + 5500;
    }

    /* One song per time the displayed age goes up, including a jump of
     * several years in a single tick. The first sample after boot is
     * silent, so switching her on does not celebrate the age she already is.
     * 100 stays, so the song stops there. */
    {
        int age_now = pet_sim_age(&a->pet, now_ms);
        if (a->birthday_age < 0)
            a->birthday_age = age_now;
        else if (age_now > a->birthday_age && a->pet.stage != ST_DEAD) {
            a->birthday_age = age_now;
            PLAY(SND_BIRTHDAY);
        } else if (age_now != a->birthday_age)
            a->birthday_age = age_now;
    }

    /* The menu being open counts as being in use — the screen must not doze
     * off underneath it. */
    if (a->menu) a->idle_last_ms = now_ms;

    int timeout = a->pet.napping ? a->nap_sleep_after_s : a->sleep_after_s;
    if (timeout > 0 && !a->screen_off && !app_busy(a) &&
        now_ms - a->idle_last_ms > (int64_t)timeout * 1000) {
        a->screen_off = true;
        /* The case covers the side buttons, so the dark screen is the nap.
         * An egg has nothing to restore, and a dead pet stays dead. */
        if (!a->pet.napping && a->pet.stage != ST_EGG && a->pet.stage != ST_DEAD) {
            pet_set_nap(&a->pet, true, now_ms);
            a->roll_on = false;
            a->peek_on = false;
            a->anim = AN_NONE;
            PLAY(SND_NAP);
        }
    }

    if (a->screen_off || a->pet.stage == ST_DEAD) return;

    double t = (double)now_ms;

    /* A tumble now and then, when everything is going well. */
    if (can_roll(a) && t > a->roll_next && t > a->dance_until &&
        r_roll < ROLL_CHANCE) {
        a->roll_on = true; a->roll_t = 0;
        a->roll_next = t + ROLL_COOLDOWN;
        PLAY(SND_ROLL);
        return;
    }
    /* Otherwise she might come and play peekaboo with you. */
    if (can_peek(a) && t > a->peek_next && t > a->dance_until &&
        r_peek < PEEK_CHANCE) {
        a->peek_on = true; a->peek_t = 0;
        a->peek_dir = r_dir < 0.5 ? -1 : 1;
        a->peek_next = t + PEEK_COOLDOWN;
        if (a->roll_next < t + 10000) a->roll_next = t + 10000;
        PLAY(SND_PEEK);
        return;
    }
    /* And she dances when she is feeling good. */
    if (!a->pet.asleep && !a->menu && pet_mood(&a->pet, false) == MOOD_HAPPY &&
        t > a->dance_next && t > a->dance_until &&
        r_roll < DANCE_CHANCE) {
        a->dance_until = t + DANCE_MS;
        a->dance_next  = t + DANCE_COOLDOWN;
        PLAY(SND_DANCE);
    }
}

/* ---- per frame -------------------------------------------------------- */
void app_advance(app_t *a, double dt, int64_t now_ms)
{
    if (a->act_pending) {
        a->act_delay_t += dt;
        if (a->act_delay_t >= ACT_DELAY_MS) {
            int what = a->act_pending;
            a->act_pending = 0;
            if (what == 1) do_feed(a, now_ms); else do_clean(a);
        }
    }

    if (a->anim != AN_NONE) a->anim_t += dt;

    if (a->roll_on) {
        a->roll_t += dt;
        if (a->roll_t >= ROLL_END) { a->roll_on = false; PLAY(SND_ROLL_LAND); }
    }
    if (a->peek_on) {
        double before = a->peek_t;
        a->peek_t += dt;
        if (before < 2100 && a->peek_t >= 2100) PLAY(SND_PEEK_BACK);
        if (before < 3200 && a->peek_t >= 3200) PLAY(SND_PEEK_HOME);
        if (a->peek_t >= PEEK_END) a->peek_on = false;
    }
    if (a->evo_queued) {
        a->evo_queue_t += dt;
        if (a->evo_queue_t >= EAT_MS) {       /* cupcake finished */
            a->evo_queued = false;
            a->evo_on = true; a->evo_t = 0; a->evo_flipped = false;
            a->evo_from = a->pet.stage;
            a->evo_to   = pet_pending_grow(&a->pet, now_ms);
            PLAY(SND_EVO_START);
        }
    }
    if (a->evo_on) {
        a->evo_t += dt;
        if (a->evo_t >= EVO_FLIP_AT && !a->evo_flipped) {
            a->evo_flipped = true;
            pet_finish_transform(&a->pet, now_ms);
            PLAY(SND_EVO_FLIP);
        }
        if (a->evo_t >= EVO_END) {
            a->evo_on = false;
            a->dance_until = (double)now_ms + 5000;
        }
    }
}

static void draw_age(const app_t *a, int64_t now_ms)
{
    double saved = gfx_dim;
    gfx_dim = 1.0;
    spr_age(pet_sim_age(&a->pet, now_ms));
    gfx_dim = saved;
}

static void draw_mute(const app_t *a)
{
    if (a->pet.sound) return;
    double saved = gfx_dim;
    gfx_dim = 1.0;
    spr_muted();
    gfx_dim = saved;
}

/* ---- composition ------------------------------------------------------ */
void app_frame(app_t *a, double t, int64_t now_ms)
{
    bool nap = a->pet.napping || a->pet.asleep;

    if (a->screen_off) {
        gfx_clear(C_VOID_OFF);
        spr_resting(t);
        return;
    }

    gfx_clear(nap ? C_VOID_OFF : C_VOID);

    if (a->pet.stage == ST_DEAD) {
        gfx_dim = 1.0;
        spr_dead(t);
        gfx_dim = 1.0;
        draw_mute(a);
        return;
    }

    if (a->pet.stage == ST_EGG) {
        double p = pet_age_minutes(&a->pet, now_ms) / PET_STAGE_AT[ST_BLOB];
        if (p > 1) p = 1;
        spr_egg(p, t, a->anim == AN_NUDGE && a->anim_t < NUDGE_MS);
        draw_age(a, now_ms);
        draw_mute(a);
        return;
    }

    if (a->evo_on) {
        spr_evo(t, a->evo_t, a->evo_from, a->evo_to, a->pet.stage);
        draw_age(a, now_ms);
        draw_mute(a);
        return;
    }

    bool menu = app_menu_visible(a);

    /* The cake and the piles stay on the glass while she sleeps. Hiding
     * them made a tap on the cake do nothing a child could see. */
    gfx_dim = 1.0;
    spr_hud(&a->pet, 0, t);
    gfx_dim = menu ? 0.32 : 1.0;
    spr_mess(&a->pet);

    cat_geom_t g;
    if (a->roll_on) {
        g = spr_roll(a->pet.stage, t, a->roll_t);
        gfx_dim = 1.0;
    } else if (a->peek_on) {
        g = spr_peek(a->pet.stage, t, a->peek_t, a->peek_dir);
        gfx_dim = 1.0;
    } else {
        mood_t m = nap ? MOOD_SLEEP
                       : pet_mood(&a->pet, a->anim == AN_EAT && a->anim_t < EAT_MS);
        /* she holds a happy face through the post-meal swoon */
        if (a->anim == AN_EAT && a->anim_love && a->anim_t < LOVE_TO && m != MOOD_SLEEP)
            if (a->anim_t >= EAT_MS) m = MOOD_HAPPY;

        bool dancing = !nap && !menu && t < a->dance_until &&
                       (m == MOOD_HAPPY || m == MOOD_OK);
        double lean = 0, hop = 0; int wig = 0;
        if (dancing) {
            double ph = sin(t / 165.0);
            lean = gfx_round(ph * 3);
            hop  = gfx_round(fabs(ph) * 3);
            wig  = ph > 0 ? 1 : -1;
        } else if (m == MOOD_SLEEP) {
            hop = gfx_round(sin(t / 1500.0) * 0.9);
        } else {
            hop = gfx_round(sin(t / 430.0) * 0.6);
        }
        if ((a->anim == AN_WAKE || a->anim == AN_PET) && a->anim_t < PERK_MS) {
            hop += spr_perk_hop(a->anim_t);
            wig  = spr_perk_wig(a->anim_t);
        }

        g = spr_cat(a->pet.stage, m, t, lean, hop, wig);
        gfx_dim = 1.0;

        if (pet_pending_grow(&a->pet, now_ms) != ST_COUNT && !nap)
            spr_grow_shimmer(t, g);
        if (dancing) { spr_notes(t, g); spr_sparkles(t, g); }
        if (m == MOOD_SLEEP) { spr_zzz(t, g); spr_horn_glow(t, a->pet.stage, g); }
        if (a->anim == AN_EAT && a->anim_t < EAT_MS)
            spr_cupcake(g.cx + 3, g.cy + 2, (int)(a->anim_t / BITE_MS));
        if (a->anim == AN_EAT && a->anim_love &&
            a->anim_t >= LOVE_FROM && a->anim_t < LOVE_TO)
            spr_hearts(a->anim_t, LOVE_FROM, g);
        if ((a->anim == AN_WAKE || a->anim == AN_PET) && a->anim_t < PERK_MS)
            spr_perk(a->anim_t, g);
        if (a->anim == AN_PET && a->anim_love &&
            a->anim_t >= PET_LOVE_FROM && a->anim_t < PET_LOVE_TO)
            spr_hearts(a->anim_t, PET_LOVE_FROM, g);
        if (a->anim == AN_CLEAN && a->anim_t < CLEAN_MS) {
            double p = a->anim_t / CLEAN_MS;
            double bx = 8 + gfx_round(p * 36);
            /* wobble period scaled with the slower sweep, so it is the same
             * number of wobbles across the screen rather than more of them */
            spr_broom(bx, 45 + gfx_round(sin(a->anim_t / 105.0) * 1));
            const rgb_t cols[3] = { C_R3, C_R5, C_R6 };
            for (int i = 0; i < 3; i++) {
                static const char *SP[] = { ".#.", "###", ".#." };
                gfx_map(bx - 4 - i * 4, 47 + ((i * 5) % 6), SP, 3,
                        (gfx_pal[]){ { '#', cols[i] }, { 0, 0 } });
            }
        }
    }

    if (menu) spr_buttons(&a->pet, now_ms, t, a->pressed == 1, a->pressed == 2);
    draw_age(a, now_ms);
    draw_mute(a);
}

/* ---- actions ---------------------------------------------------------- */
static void do_feed(app_t *a, int64_t now_ms)
{
    feed_result_t r = pet_feed(&a->pet, now_ms);
    switch (r) {
    case FEED_OVERFULL:
        /* She ate it. A full cake bar costs a heart, and the last one
         * ends her — the dead picture replaces the bite. */
        if (a->pet.stage == ST_DEAD) {
            a->anim = AN_NONE;
            a->menu = false;
            a->roll_on = false;
            a->peek_on = false;
            PLAY(SND_DIE);
        } else {
            play(a, AN_EAT);
            PLAY(SND_LOW);
        }
        break;
    case FEED_TRANSFORM:
        /* Eat first. Cutting straight to the light show meant a child pressed
         * "feed" and never saw her eat - it read as the wrong button. */
        play(a, AN_EAT);
        a->anim_love  = true;
        a->evo_queued = true;
        a->evo_queue_t = 0;
        PLAY(SND_FEED_HUNGRY);
        break;
    case FEED_HUNGRY:
        play(a, AN_EAT); a->anim_love = true; PLAY(SND_FEED_HUNGRY); break;
    case FEED_PLAIN:
        play(a, AN_EAT); PLAY(SND_FEED_PLAIN); break;
    }
}

static void do_clean(app_t *a)
{
    if (pet_clean(&a->pet)) { play(a, AN_CLEAN); PLAY(SND_CLEAN); }
    else PLAY(SND_REFUSE);
}

/* One use, then the buttons get out of the way. The action itself waits
 * a beat so the icons are gone before she moves. */
static void queue_action(app_t *a, int64_t now_ms, int which)
{
    a->pressed = which;
    a->press_t = (double)now_ms;
    a->menu = false;
    a->act_pending = which;
    a->act_delay_t = 0;
    PLAY(SND_PRESS);
}

/* A pile is drawn around (mess_x, 53). The box is a finger wider than the
 * sprite, and it stops short of her paws so a pet does not sweep. */
static bool on_mess(const app_t *a, double wx, double wy)
{
    for (int i = 0; i < a->pet.mess; i++) {
        double dx = wx - a->pet.mess_x[i];
        double dy = wy - 53.0;
        if (dx * dx <= 144.0 && dy * dy <= 144.0) return true;
    }
    return false;
}

/* Hunger cake and its pips, top of the screen. */
static bool on_cake_meter(double wx, double wy)
{
    return wx >= 4 && wx <= 36 && wy >= 4 && wy <= 16;
}

/* The word MUTE, under the meters. Only drawn while the speaker is off. */
static bool on_mute_word(double wx, double wy)
{
    return wx >= 22 && wx <= 42 && wy >= 17 && wy <= 24;
}

static void toggle_sound(app_t *a)
{
    a->pet.sound = !a->pet.sound;
    board_audio_enable(a->pet.sound);
    if (a->pet.sound) PLAY(SND_UNMUTE);
}

/* A still finger. Sliding off cancels it, and it only lands once per press. */
bool app_hold(app_t *a, int64_t now_ms, int down, double x, double y)
{
    if (!down) {
        a->hold_down = false;
        a->hold_fired = false;
        return false;
    }
    if (!a->hold_down) {
        a->hold_down = true;
        a->hold_fired = false;
        a->hold_at = now_ms;
        a->hold_x = x;
        a->hold_y = y;
        return false;
    }
    if (a->hold_fired) return false;
    double dx = x - a->hold_x, dy = y - a->hold_y;
    if (dx * dx + dy * dy > 25.0) {
        a->hold_fired = true;            /* a drag is not a mute */
        return false;
    }
    if (now_ms - a->hold_at < APP_HOLD_MS) return false;

    a->hold_fired = true;
    toggle_sound(a);
    return true;
}

/* Cake, a pile, or the word MUTE. Shared by a waking tap and a normal one
 * so the press that gets her up can also feed, sweep, or unmute. */
static bool tap_care(app_t *a, int64_t now_ms, double wx, double wy)
{
    if (a->pet.stage == ST_EGG || a->pet.stage == ST_DEAD) return false;
    if (!a->pet.sound && on_mute_word(wx, wy)) {
        toggle_sound(a);
        return true;
    }
    if (on_mess(a, wx, wy)) { queue_action(a, now_ms, 2); return true; }
    if (on_cake_meter(wx, wy)) { queue_action(a, now_ms, 1); return true; }
    return false;
}

void app_touch(app_t *a, int64_t now_ms, double wx, double wy)
{
    if (a->screen_off) {
        wake_screen(a, now_ms);
        /* The finger that lit the glass is asking her to get up, even when
         * the moon is still low. Leaving her asleep here swallowed every
         * later tap, so feed, sweep, and unmute all looked broken. */
        if (a->pet.asleep && a->pet.stage != ST_EGG && a->pet.stage != ST_DEAD)
            set_nap(a, false, now_ms);
        return;
    }
    a->idle_last_ms = now_ms;
    /* The finger that lit the screen must not also pet her. It can still
     * land on the cake, a pile, or MUTE. */
    if (now_ms - a->woke_at_ms < 400) {
        tap_care(a, now_ms, wx, wy);
        return;
    }
    if (app_busy(a)) return;

    if (a->pet.stage == ST_DEAD) {
        if (wx >= HATCH_X0 && wx <= HATCH_X1 && wy >= HATCH_Y0 && wy <= HATCH_Y1) {
            bool hear = a->pet.sound;    /* mute is the speaker, not the pet */
            app_init(a, now_ms);
            a->pet.sound = hear;
            board_audio_enable(hear);
            PLAY(SND_STAGE);
        }
        return;
    }

    if (a->pet.stage == ST_EGG) { egg_tap(a, now_ms); return; }

    if (app_menu_visible(a)) {
        double best = BTN_HIT_R; int hit = 0;
        double d1 = hypot(wx - BTN_FEED_X,  wy - BTN_Y);
        double d2 = hypot(wx - BTN_CLEAN_X, wy - BTN_Y);
        if (d1 < best) { best = d1; hit = 1; }
        if (d2 < best) { best = d2; hit = 2; }
        if (hit) {
            /* Acknowledge now, act in a second - see ACT_DELAY_MS. */
            queue_action(a, now_ms, hit);
            return;
        }
    }

    /* Asleep or napping: this tap gets her up and keeps her up, and if it
     * landed on the cake, a pile, or MUTE it does that too. */
    if (a->pet.napping || a->pet.asleep) {
        set_nap(a, false, now_ms);
        tap_care(a, now_ms, wx, wy);
        return;
    }

    /* The side buttons sit on the metal edge and a case can hide them.
     * The cake meter and the piles are the same two actions, on things
     * that are already on the glass. */
    if (tap_care(a, now_ms, wx, wy)) return;

    /* Petting her: she always reacts, the heart is rate-limited.
     *
     * When a heart lands she does the perk-and-hearts, because the reward
     * has to be the thing you see. When it doesn't - a second tap inside the
     * cooldown - she shows off instead, picking from her whole repertoire.
     * So mashing her is rewarded with variety rather than the same little
     * hop over and over, and the cooldown stops being a dead press. */
    if (hypot(wx - 32, (wy - PET_CY) * 1.2) < 22) {
        bool heart = pet_cuddle(&a->pet, now_ms);
        double t = (double)now_ms;

        if (heart) {
            play(a, AN_PET);
            a->anim_love = true;
            PLAY(SND_CUDDLE_H);
            a->dance_until = t + 2600;
            return;
        }

        double r = board_rand01();
        if (r < 0.34 && a->pet.energy >= 1) {          /* front flip */
            a->roll_on = true; a->roll_t = 0;
            a->roll_next = t + ROLL_COOLDOWN;
            PLAY(SND_ROLL);
        } else if (r < 0.60) {                          /* peekaboo */
            a->peek_on = true; a->peek_t = 0;
            a->peek_dir = board_rand01() < 0.5 ? -1 : 1;
            a->peek_next = t + PEEK_COOLDOWN;
            PLAY(SND_PEEK);
        } else {                                        /* a dance */
            play(a, AN_PET);
            PLAY(SND_CUDDLE);
            a->dance_until = t + DANCE_MS;
        }
    }
}

void app_button(app_t *a, int64_t now_ms, int which)
{
    if (a->screen_off) { wake_screen(a, now_ms); return; }
    a->idle_last_ms = now_ms;
    if (a->evo_on) return;

    /* While she is still an egg, every button is a knock. Nothing else in
     * the toy is reachable yet, so a press that did nothing would just teach
     * a child that the buttons are decoration. */
    if (a->pet.stage == ST_EGG) { egg_tap(a, now_ms); return; }

    /* Dead: the only restart is the on-screen hatch button. */
    if (a->pet.stage == ST_DEAD) return;

    /* Either button gets her up. A nap and a low-moon rest are the same
     * from the glass: she was down, and a press means get up. */
    if (a->pet.napping || a->pet.asleep) { set_nap(a, false, now_ms); return; }

    if (which == 0) {
        a->menu = !a->menu;
        if (a->menu) PLAY(SND_MENU_ON); else PLAY(SND_MENU_OFF);
    } else {
        set_nap(a, true, now_ms);
        a->menu = false;
    }
}

void app_picked_up(app_t *a, int64_t now_ms)
{
    wake_screen(a, now_ms);
}
