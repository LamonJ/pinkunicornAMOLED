/* app.h — the interaction layer.
 *
 * Owns everything that happens between "a finger touched the glass" and
 * "these 4096 pixels". Pure logic plus calls into board.h, so it is
 * unit-testable on a PC — see host/test_app.c.
 */
#ifndef APP_H
#define APP_H

#include "sprites.h"
#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AN_NONE = 0,
    AN_EAT,     /* cupcake, bites, then hearts if she wanted it */
    AN_PET,     /* perk + sparkles, hearts if the cooldown allowed one */
    AN_WAKE,    /* she notices you */
    AN_CLEAN,   /* broom sweeps across */
    AN_NUDGE    /* the egg rocking under a tap */
} anim_kind_t;

typedef struct {
    pet_t pet;

    anim_kind_t anim;
    double      anim_t;
    bool        anim_love;

    double dance_until, dance_next;

    bool   roll_on;  double roll_t, roll_next;
    bool   peek_on;  double peek_t, peek_next;  int peek_dir;

    bool   evo_on;   double evo_t;
    stage_t evo_from, evo_to;
    bool   evo_flipped;
    /* A transformation waits for her to finish the cupcake first, so feeding
     * always looks like feeding. */
    bool    evo_queued; double evo_queue_t;

    bool   menu;                 /* summoned by the top button, and it stays */
    double press_t; int pressed; /* 0 none, 1 feed, 2 clean                  */

    /* A pressed button closes the menu, then the action happens a beat
     * later, so the buttons are gone before the animation starts and you
     * see the whole thing. */
    int    act_pending;          /* 0 none, 1 feed, 2 clean                  */
    double act_delay_t;

    bool    screen_off;
    int64_t idle_last_ms, woke_at_ms;
    int     sleep_after_s;       /* 0 = never                               */
    /* A deliberate nap is something to watch, not something to leave the
     * screen lit for. Long enough to enjoy her curling up, then away. */
    int     nap_sleep_after_s;
    bool    dev_menu;            /* grown-up diagnostics, both buttons 5 s  */

    /* START_AS_EGG 1. The egg is on screen and her clock is not running
     * until the Start button is pressed. */
    bool    awaiting_start;

    int64_t last_pet_tick_ms;

    /* Displayed age the last time a birthday tune was considered.
     * -1 until the first tick, so a boot does not play one. */
    int     birthday_age;

    /* Finger-down tracking for the mute hold. hold_fired stays set until
     * the finger lifts after a finished mute, so that release is not also
     * a tap. A slide clears the hold instead, and the lift can still feed
     * or sweep. */
    bool    hold_down;
    bool    hold_fired;
    int64_t hold_at;
    double  hold_x, hold_y;
} app_t;

void app_init(app_t *a, int64_t now_ms);

/* 1 when boot should wait on the Start button.
 * The stamp arguments are ignored. While the flag is 1, every boot waits,
 * including a reboot of an upload whose button was already pressed. */
int app_gift_needs_start(int start_as_egg, const char *saved_stamp,
                         const char *build_stamp);
void app_wait_for_start(app_t *a);

/* Call about once a second. Advances needs and rolls for spontaneous
 * behaviour. */
void app_tick(app_t *a, int64_t now_ms);

/* Advances animation clocks. Call once per rendered frame with the real
 * elapsed milliseconds, so timing holds at any frame rate. */
void app_advance(app_t *a, double dt_ms, int64_t now_ms);

/* Composes the scene into gfx_fb. Does not push it. */
void app_frame(app_t *a, double t, int64_t now_ms);

/* Input. Coordinates are world units (0..63). */
void app_touch(app_t *a, int64_t now_ms, double wx, double wy);

/* Call every frame with the finger state. A still hold of APP_HOLD_MS
 * toggles the speaker and returns true on that frame only. */
#define APP_HOLD_MS 800
bool app_hold(app_t *a, int64_t now_ms, int down, double x, double y);
void app_button(app_t *a, int64_t now_ms, int which);   /* 0 top, 1 bottom */
void app_picked_up(app_t *a, int64_t now_ms);

bool app_menu_visible(const app_t *a);
bool app_busy(const app_t *a);       /* animation owns the input */

#ifdef __cplusplus
}
#endif

#endif
