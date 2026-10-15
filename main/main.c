/* main.c — the loop. Everything interesting is in app.c; this just wires
 * the board to it and keeps time honestly.
 *
 * State layout note: the saved blob is pet_t verbatim. That is fine while
 * the struct is stable, but if you add a field, bump SAVE_VERSION so old
 * blobs are discarded rather than misread. Nobody wants a corrupted pet.
 */
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "sprinkles/app.h"

static const char *TAG = "sprinkles";

#define SAVE_VERSION 1
typedef struct { uint32_t version; pet_t pet; } save_t;

static uint16_t *panel;
static app_t app;

#define FRAME_MS 66             /* ~15 fps; a Tamagotchi needs no more */

static void load_state(int64_t now_ms)
{
    save_t s;
    int64_t rtc_ms; int hour;
    bool have_rtc = board_rtc_get(&rtc_ms, &hour);

    if (board_load(&s, sizeof s) && s.version == SAVE_VERSION && have_rtc) {
        app_init(&app, now_ms);
        app.pet = s.pet;
        /* Re-base the stored wall-clock timestamps onto this boot's
         * monotonic clock, then let pet_tick catch up however long it was.
         * The catch-up is unit-tested to land on the same state as ticking
         * once a minute the whole time. */
        int64_t away_ms = rtc_ms - s.pet.last_tick_ms;
        if (away_ms < 0) away_ms = 0;
        app.pet.born_ms      = now_ms - (rtc_ms - s.pet.born_ms);
        app.pet.last_tick_ms = now_ms - away_ms;
        ESP_LOGI(TAG, "restored: %s, away for %lld min",
                 PET_STAGE_NAME[app.pet.stage], (long long)(away_ms / 60000));
    } else {
        app_init(&app, now_ms);
        ESP_LOGI(TAG, "no saved pet%s - starting from the egg",
                 have_rtc ? "" : " (and no RTC yet)");
    }
}

static void save_state(void)
{
    save_t s = { .version = SAVE_VERSION, .pet = app.pet };
    board_save(&s, sizeof s);
}

void app_main(void)
{
    board_init();

    panel = heap_caps_malloc((size_t)GFX_PANEL * GFX_PANEL * sizeof(uint16_t),
                             MALLOC_CAP_SPIRAM);
    if (!panel) { ESP_LOGE(TAG, "no PSRAM for the panel buffer"); return; }

    board_brightness(35);        /* start dim; it is the main battery cost */

    int64_t now = board_now_ms();
    load_state(now);

    int64_t last_frame = now, last_tick = now, last_save = now;
    bool touch_was_down = false;
    double down_x = 0, down_y = 0, last_x = 0, last_y = 0;
    int64_t down_at = 0;

    while (1) {
        now = board_now_ms();
        double dt = (double)(now - last_frame);
        last_frame = now;

        /* ---- input ---- */
        for (int b = 0; b < 2; b++)
            if (board_button_pressed(b)) app_button(&app, now, b);

        if (board_picked_up()) app_picked_up(&app, now);

        /* A real tap: press and release in the same place, quickly. Small
         * hands smear, so allow some travel but not a drag. */
        double tx = 0, ty = 0;
        bool down = board_touch(&tx, &ty);
        if (down) { last_x = tx; last_y = ty; }
        if (down && !touch_was_down) { down_x = tx; down_y = ty; down_at = now; }
        if (!down && touch_was_down &&
            now - down_at < 400 &&
            fabs(last_x - down_x) < 5 && fabs(last_y - down_y) < 5)
            app_touch(&app, now, down_x, down_y);
        touch_was_down = down;

        /* ---- state ---- */
        if (now - last_tick >= 1000) {
            int hour = 12; int64_t unused;
            board_rtc_get(&unused, &hour);
            app_tick(&app, now, hour);
            app.pet.last_tick_ms = now;         /* what gets persisted */
            last_tick = now;
        }
        app_advance(&app, dt, now);

        /* Persist about every half minute, and immediately after anything
         * irreversible. Flash has limited write cycles; a pet that loses
         * thirty seconds of ageing loses nothing anyone can perceive. */
        if (now - last_save >= 30000) { save_state(); last_save = now; }

        /* ---- draw ---- */
        app_frame(&app, (double)now, now);
        gfx_drift((double)now / 60000.0, &board_drift_x, &board_drift_y);
        gfx_blit565(panel, board_drift_x, board_drift_y);
        board_panel_push(panel);

        /* When the panel is off there is nothing to animate, so stop burning
         * cycles and let the silicon idle until she is touched or lifted. */
        if (app.screen_off) {
            save_state(); last_save = board_now_ms();
            board_sleep_until_touched();
            last_frame = board_now_ms();
        } else {
            vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
        }
    }
}
