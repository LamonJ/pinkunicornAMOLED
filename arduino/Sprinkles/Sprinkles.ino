/* Sprinkles.ino — a unicorn cat for a five-year-old.
 *
 * This folder is the sketch. Open Sprinkles.ino and Upload.
 * Board: "ESP32S3 Dev Module" for the Waveshare ESP32-S3-Touch-AMOLED-1.75.
 * Settings that matter:
 *   Flash Size          16MB (128Mb)
 *   PSRAM               OPI PSRAM        <- required, the frame buffer needs it
 *   Partition Scheme    16M Flash (3MB APP/9.9MB FATFS) or similar
 *   USB CDC On Boot     Enabled          <- so Serial.println works
 *
 * Library Manager, once: "GFX Library for Arduino", "SensorLib", "XPowersLib".
 * pin_config.h is already in this folder.
 *
 * All the logic lives in the .c files next to this one and is verified on a
 * PC. The hardware glue is board_arduino.cpp — see BRINGUP.md.
 */
#include <Arduino.h>

extern "C" {
#include "app.h"
#include "board.h"
}

/* The save blob and all of its arithmetic now live in pet.c, where they can
 * be unit-tested. Version 3 stores DURATIONS plus the wall time at save,
 * instead of raw monotonic timestamps that were being diffed against the
 * RTC - which made her about 56,000 years old on every reboot. */

static uint16_t *panel;
static app_t app;

#define FRAME_MS 66            /* ~15 fps; a Tamagotchi needs no more */

/* 0 = light sleep. A tap on the glass, or the lower side button, turns the
 * panel back on. Deep sleep (1) can only be woken by that button, because
 * the touch controller is powered down with it — a finger on the glass does
 * nothing, which reads as a dead toy. */
#define USE_DEEP_SLEEP 0

static int64_t last_frame, last_tick, last_save;
static bool    touch_was_down = false;
static double  down_x = 0, down_y = 0, last_x = 0, last_y = 0;

static void load_state(int64_t now_ms)
{
    pet_save_t blob;
    int64_t rtc_ms = 0; int hour = 0;
    bool have_rtc = board_rtc_get(&rtc_ms, &hour);

    app_init(&app, now_ms);

    int64_t away = 0;
    if (board_load(&blob, sizeof blob) &&
        pet_restore(&app.pet, &blob, now_ms, rtc_ms, have_rtc, &away)) {
        int64_t age = now_ms - app.pet.born_ms;
        Serial.printf("restored %s, age %lldh%02lldm, away %lld min%s\n",
                      PET_STAGE_NAME[app.pet.stage],
                      (long long)(age / 3600000), (long long)((age / 60000) % 60),
                      (long long)(away / 60000),
                      have_rtc ? "" : " (no clock: no ageing while off)");
    } else {
        Serial.println("no usable saved pet - starting from the egg");
    }
}

static void save_state(void)
{
    int64_t rtc_ms = 0; int hour = 0;
    bool have_rtc = board_rtc_get(&rtc_ms, &hour);
    pet_save_t blob;
    pet_snapshot(&app.pet, board_now_ms(), rtc_ms, have_rtc, &blob);
    board_save(&blob, sizeof blob);
}

/* Reset lives behind a confirm because it destroys a pet a child may have
 * been looking after for days. One tap arms it, a second within eight
 * seconds does it, and anything else - a side button, or just waiting -
 * cancels. */
static bool    dev_confirm = false;
static int64_t dev_confirm_at = 0;
#define DEV_CONFIRM_MS 8000

static void dev_reset_now(int64_t now)
{
    app_init(&app, now);              /* a fresh egg */
    save_state();
    dev_confirm = false;
    Serial.println("[dev] RESET - new egg");
    static const spr_note_t done[] = { {500,150},{700,150},{900,220} };
    board_beep(done, 3);
}

/* Grown-up diagnostics. Redrawn about twice a second — often enough that a
 * battery reading or a meter visibly moves, slow enough not to flicker. */
static void draw_dev_screen(int64_t now)
{
    static int64_t last = 0;
    if (now - last < 500) return;
    last = now;

    int pct, mv; bool charging, present;
    board_battery(&pct, &mv, &charging, &present);

    int64_t age = now - app.pet.born_ms;
    int64_t rtc_ms; int hour = -1;
    bool rtc = board_rtc_get(&rtc_ms, &hour);

    static char l[12][40];
    int n = 0;
    snprintf(l[n++], 40, "GROWN-UP");
    if (!present)      snprintf(l[n++], 40, "Batt   no cell");
    else               snprintf(l[n++], 40, "Batt   %d%%  %d.%02dV%s",
                                pct, mv / 1000, (mv % 1000) / 10,
                                charging ? " CHG" : "");
    snprintf(l[n++], 40, "%s", PET_STAGE_NAME[app.pet.stage]);
    snprintf(l[n++], 40, "Age    %lldd %02lldh %02lldm",
             (long long)(age / 86400000), (long long)(age / 3600000 % 24),
             (long long)(age / 60000 % 60));
    snprintf(l[n++], 40, "Cake %d Heart %d En %d",
             app.pet.hunger, app.pet.happy, app.pet.energy);
    snprintf(l[n++], 40, "Mess %d  %s", app.pet.mess,
             app.pet.asleep ? "asleep" : "awake");
    stage_t nx = pet_pending_grow(&app.pet, now);
    if (app.pet.stage == ST_DEAD)
        snprintf(l[n++], 40, "Next   hatch an egg");
    else if (nx != ST_COUNT)
        snprintf(l[n++], 40, "Next   FEED HER NOW");
    else {
        double left = pet_stage_remaining(&app.pet, now);
        if (left < 0) snprintf(l[n++], 40, "Next   fully grown");
        else snprintf(l[n++], 40, "Next   in %dh %02dm",
                      (int)(left / 60), (int)left % 60);
    }
    if (rtc) snprintf(l[n++], 40, "Clock  %02d:xx  set", hour);
    else     snprintf(l[n++], 40, "Clock  NOT SET");
    snprintf(l[n++], 40, "Touch %s Aud %s IMU %s",
             board_touch_ok() ? "y" : "n",
             board_audio_ok() ? "y" : "n",
             board_imu_ok()   ? "y" : "n");
    snprintf(l[n++], 40, "PSRAM  %u KB free",
             (unsigned)(ESP.getFreePsram() / 1024));
    snprintf(l[n++], 40, "Up     %lu s", (unsigned long)(now / 1000));
    snprintf(l[n++], 40, "side button = close");

    const char *ptr[12];
    for (int i = 0; i < n; i++) ptr[i] = l[i];
    board_dev_screen(ptr, n,
                     dev_confirm ? "TAP AGAIN: ERASE" : "RESET TO EGG",
                     dev_confirm);
}

void setup()
{
    Serial.begin(115200);
    /* A charger is not a serial monitor. Prints must not stall the toy
     * when nothing is reading them. */
    Serial.setTxTimeoutMs(0);
    /* Native USB needs a moment for the host to attach, and anything printed
     * before that is simply lost. Wait, but never block forever - the device
     * has to work with nothing plugged in. */
    for (int i = 0; i < 30 && !Serial; i++) delay(100);
    delay(300);

    /* Waking from deep sleep is a full boot, so the usual start-up theatre
     * would play every single time she is woken. Skip it and go straight to
     * her - the point is that pressing the button feels like waking a pet,
     * not like switching on a device. */
    bool woke = board_woke_from_deep_sleep();

    Serial.println();
    Serial.println(woke ? "=== Sprinkles waking ===" : "=== Sprinkles booting ===");

    board_init_quiet(woke);     /* skips the colour self-test when waking */

    if (!board_display_ok())
        Serial.println("DISPLAY FAILED - check pin_config.h");
    if (!woke) {
        board_diag("Sprinkles");
        board_diag(board_display_ok() ? "display OK" : "display FAIL");
    }

    char buf[64];
    if (!woke) {
        snprintf(buf, sizeof buf, "PSRAM %u KB", (unsigned)(ESP.getPsramSize() / 1024));
        board_diag(buf);
    }

    panel = (uint16_t *)ps_malloc((size_t)GFX_PANEL * GFX_PANEL * sizeof(uint16_t));
    if (!panel) {
        /* Almost always the PSRAM setting. This message is on the panel too,
         * so you can read it with no serial link at all. */
        board_diag_fail("NO PSRAM!");
        board_diag_fail("Tools > PSRAM:");
        board_diag_fail("OPI PSRAM");
        while (1) delay(1000);
    }
    if (!woke) {
        snprintf(buf, sizeof buf, "framebuf %u KB",
                 (unsigned)((size_t)GFX_PANEL * GFX_PANEL * 2 / 1024));
        board_diag(buf);
    }

    board_brightness(35);       /* start dim: it's the main battery cost */

    int64_t now = board_now_ms();
    load_state(now);
    board_audio_enable(app.pet.sound);
    last_frame = last_tick = last_save = now;

    if (!woke) {
        snprintf(buf, sizeof buf, "%s", PET_STAGE_NAME[app.pet.stage]);
        board_diag(buf);
        board_diag("starting...");
    }
    /* Three rising notes on EVERY start, cold or woken. If you hear these,
     * the whole audio path came up - which makes "sounds stopped working"
     * something you find out in the first second rather than later. */
    {
        static const spr_note_t hello[] = { {880,140},{1180,140},{1560,220} };
        board_beep(hello, 3);
    }
    /* Say out loud what is not wired up yet, so an unimplemented peripheral
     * never gets mistaken for a broken one. */
    Serial.println("--- peripheral status ---");
    Serial.printf("display : %s\n", board_display_ok() ? "OK" : "FAIL");
    Serial.println("PWR     : NOT IMPLEMENTED (needs EXIO4 expander read)");
    Serial.printf ("touch   : %s\n", board_touch_ok() ? "OK" : "FAILED");
    Serial.printf ("audio   : %s%s\n", board_audio_ok() ? "OK" : "FAILED",
                   woke ? "  (this boot was a deep-sleep wake)" : "");
    Serial.printf ("RTC     : %s\n", board_rtc_ok()   ? "OK" : "FAILED");
    Serial.printf ("IMU     : %s\n", board_imu_ok()   ? "OK" : "FAILED");
    Serial.printf ("PWR btn : %s\n", board_pwr_ok()   ? "OK" : "FAILED");
    Serial.println("tap the top = feed | tap the bottom = sweep | hold her = mute");
    Serial.println("BOOT tap = menu | dark screen = nap | a tap wakes her");
    Serial.println("-------------------------");
    Serial.printf("world %dx%d -> %d px at %dx, offset %d\n",
                  GFX_W, GFX_W, GFX_W * GFX_SCALE, GFX_SCALE, GFX_OFFSET);
    if (woke) {
        /* She notices you, exactly as she would have on a light-sleep wake. */
        app_picked_up(&app, now);
    } else {
        delay(1500);            /* long enough to read the panel */
    }
}

void loop()
{
    int64_t now = board_now_ms();
    double dt = (double)(now - last_frame);
    last_frame = now;

    /* ---- input ----
     * BOOT (the LOWER button) taps to toggle the menu. A hold naps her.
     * The screen going dark is the other way she naps, which is the one
     * that works inside the case.
     *
     * PWR is not read as a tap. The case covers it, and EXIO4 was rising
     * on its own: every edge printed "[PWR] nap / wake" and flipped her
     * while the moon was already full. Holding that button about six
     * seconds still powers the board off at the chip. */
    #define HOLD_MS 400
    #define DEV_HOLD_MS 3000
    static bool     btn_down = false;
    static int64_t  btn_at   = 0;
    static bool     btn_fired = false;

    bool boot_down = board_button_down(0);
    bool pwr_down  = board_button_down(1);

    /* Both buttons together for three seconds OPENS the grown-up screen.
     * It only ever opens - closing is a single press, below - because a
     * toggle here would fight the arming logic. */
    static int64_t both_at = 0;
    static bool    dev_fired = false;
    static bool    dev_armed = false;
    if (boot_down && pwr_down) {
        if (both_at == 0) both_at = now;
        if (!dev_fired && now - both_at >= DEV_HOLD_MS && !app.dev_menu) {
            dev_fired = true;
            app.dev_menu = true;
            app.menu = false;
            /* Not armed yet: you are still holding the buttons that opened
             * it, and letting go must NOT count as the closing click. */
            dev_armed = false;
            Serial.println("[both held 3s] dev menu OPEN");
            static const spr_note_t chirp[] = { {1200,140},{900,140},{1500,220} };
            board_beep(chirp, 3);
        }
    } else {
        both_at = 0;
        dev_fired = false;
    }

    /* While the grown-up screen is up the buttons only close it, so nothing
     * below can feed her or start an animation by accident. */
    if (app.dev_menu) {
        if (dev_confirm && now - dev_confirm_at > DEV_CONFIRM_MS) {
            dev_confirm = false;
            Serial.println("[dev] confirm timed out");
        }

        /* Touch drives the on-screen button. Raw PANEL coordinates here,
         * because the grown-up screen is drawn at native resolution rather
         * than through the 64-grid pixel world. */
        double dwx, dwy;
        static bool dev_touch_last = false;
        static int  dev_press_x = -1, dev_press_y = -1;
        bool dev_touch = board_touch(&dwx, &dwy);
        if (dev_touch && !dev_touch_last) {
            dev_press_x = board_touch_raw_x;
            dev_press_y = board_touch_raw_y;
        }
        if (!dev_touch && dev_touch_last) {
            bool in_btn = dev_press_x >= DEV_BTN_X0 && dev_press_x <= DEV_BTN_X1 &&
                          dev_press_y >= DEV_BTN_Y0 && dev_press_y <= DEV_BTN_Y1;
            Serial.printf("[dev] tap %d,%d %s\n", dev_press_x, dev_press_y,
                          in_btn ? "on button" : "(ignored)");
            if (in_btn) {
                if (!dev_confirm) {
                    dev_confirm = true;
                    dev_confirm_at = now;
                    Serial.println("[dev] reset ARMED - tap again to confirm");
                    static const spr_note_t arm[] = { {700,140},{500,200} };
                    board_beep(arm, 2);
                } else {
                    dev_reset_now(now);
                    app.dev_menu = false;
                    dev_armed = false;
                }
            }
        }
        dev_touch_last = dev_touch;

        static bool dev_any_last = false;
        bool any = boot_down || pwr_down;
        if (!dev_armed) {
            /* Arm once you have let go of the opening hold. */
            if (!any) { dev_armed = true; dev_any_last = false; }
        } else if (any && !dev_any_last) {
            app.dev_menu = false;
            dev_armed = false;
            dev_confirm = false;              /* leaving cancels the reset */
            Serial.println("[btn] dev menu closed");
            static const spr_note_t bye[] = { {900,150} };
            board_beep(bye, 1);
        }
        dev_any_last = any;
        btn_down = boot_down;
        goto after_input;
    }

    if (boot_down && !btn_down) {
        btn_at = now; btn_fired = false;
        Serial.println("[BOOT down]");
    }
    if (boot_down && !btn_fired && now - btn_at >= HOLD_MS) {
        btn_fired = true;                     /* fires on the way down, so the
                                                 hold feels immediate */
        Serial.printf("[BOOT hold %lums] -> nap\n", (unsigned long)(now - btn_at));
        app_button(&app, now, 1);
        Serial.printf("            napping=%d asleep=%d\n",
                      app.pet.napping, app.pet.asleep);
    }
    if (!boot_down && btn_down) {
        Serial.printf("[BOOT up after %lums]%s\n",
                      (unsigned long)(now - btn_at),
                      btn_fired ? " (hold already handled)" : " -> menu");
        if (!btn_fired) {
            app_button(&app, now, 0);
            Serial.printf("            menu=%d\n", app_menu_visible(&app));
        }
    }
    btn_down = boot_down;
after_input:;

    if (board_picked_up()) {
        Serial.println("[imu] picked up");
        app_picked_up(&app, now);
    }

    /* Where the finger went down is the tap. A small finger slides off the
     * cake, and pressing until something happens used to run past the old
     * time limit and get thrown away. A finished mute hold is the exception. */
    double tx = 0, ty = 0;
    bool down = board_touch(&tx, &ty);
    if (down) { last_x = tx; last_y = ty; }
    if (down && !touch_was_down) { down_x = tx; down_y = ty; }
    if (down && app_hold(&app, now, 1, tx, ty)) {
        Serial.println(app.pet.sound ? "[mute] sound on" : "[mute] sound off");
        save_state();
        last_save = now;
    }
    /* A hold that muted must not also count as a tap when the finger lifts. */
    bool held = app.hold_fired;
    if (!down) app_hold(&app, now, 0, 0, 0);
    if (!down && touch_was_down && !held) {
        Serial.printf("[tap] raw %d,%d -> world %.1f,%.1f  slide %.0f,%.0f\n",
                      board_touch_raw_x, board_touch_raw_y, down_x, down_y,
                      fabs(last_x - down_x), fabs(last_y - down_y));
        int pending = app.act_pending;
        stage_t before = app.pet.stage;
        app_touch(&app, now, down_x, down_y);
        if (app.act_pending == 1 && pending != 1) Serial.println("[tap] -> feed");
        if (app.act_pending == 2 && pending != 2) Serial.println("[tap] -> sweep");
        /* Save straight away after anything a child would notice losing. */
        if (app.pet.stage != before || app.anim == AN_EAT || app.evo_queued ||
            pending != app.act_pending) {
            save_state();
            last_save = now;
        }
    }
    touch_was_down = down;

    /* ---- state ---- */
    if (now - last_tick >= 1000) {
        app_tick(&app, now);
        app.pet.last_tick_ms = now;          /* what gets persisted */
        last_tick = now;

        /* Dim right down while she sleeps. A 466px AMOLED at full brightness
         * in a dark bedroom isn't a nightlight, it's a torch - and
         * brightness is the dominant battery cost either way. */
        static int want_pct = -1;
        int pct = app.pet.asleep ? 12 : 35;
        if (pct != want_pct) { board_brightness(pct); want_pct = pct; }
    }
    app_advance(&app, dt, now);

    /* Death and a new egg happen off the tap that queued them. Write the
     * stage as soon as it changes so a power cut does not undo it. */
    {
        static int persisted_stage = -1;
        if ((int)app.pet.stage != persisted_stage) {
            if (persisted_stage >= 0) { save_state(); last_save = now; }
            persisted_stage = (int)app.pet.stage;
        }
    }

    /* Flash has limited write cycles, and a pet that loses thirty seconds of
     * ageing loses nothing anyone can perceive. */
    if (now - last_save >= 30000) { save_state(); last_save = now; }

    /* A heartbeat, so "is it alive" is never a guess. Also the fastest way
     * to see that ticks are landing and the frame rate is sane. */
    static int frames = 0;
    static int64_t last_beat = 0;
    frames++;
    if (now - last_beat >= 2000) {
        char batt[80];
        board_batt_info(batt, (int)sizeof batt);
        Serial.printf("[%6lus] %-12s cake=%d heart=%d energy=%d mess=%d "
                      "asleep=%d menu=%d screen=%s %d fps %s\n",
                      (unsigned long)(now / 1000),
                      PET_STAGE_NAME[app.pet.stage],
                      app.pet.hunger, app.pet.happy, app.pet.energy,
                      app.pet.mess, app.pet.asleep,
                      app_menu_visible(&app),
                      app.screen_off ? "off" : "on",
                      frames / 2,
                      batt);
        frames = 0; last_beat = now;
    }

    /* ---- draw ---- */
    if (app.dev_menu) {
        draw_dev_screen(now);
    } else {
        app_frame(&app, (double)now, now);
        gfx_drift((double)now / 60000.0, &board_drift_x, &board_drift_y);
        gfx_blit565(panel, board_drift_x, board_drift_y);
        board_panel_push(panel);
    }

    /* Nothing to animate with the panel off, so let the silicon idle until
     * she's touched or lifted. */
    if (app.screen_off) {
        save_state();
#if USE_DEEP_SLEEP
        board_deep_sleep();          /* never returns; wakes as a fresh boot */
#else
        board_sleep_until_touched();
        /* The tap that woke us usually ended inside the sleep, so the main
         * loop never sees a press and a release. Without this she would
         * paint one frame and go straight back to sleep. */
        app_touch(&app, board_now_ms(), 0, 0);
        touch_was_down = false;
        last_frame = last_save = board_now_ms();
#endif
    } else {
        delay(FRAME_MS);
    }
}
