/* board_esp32.c — the only file in this project that knows about hardware.
 *
 * Every function marked TODO(bsp) is a hole to be filled from Waveshare's
 * board support component:
 *
 *   idf.py add-dependency "waveshare/esp32_s3_touch_amoled_1_75"
 *
 * Their examples are the shortcut — do not hand-roll the CO5300 init or the
 * AXP2101 register map. Bring the holes up one at a time in the order given
 * in BRINGUP.md; each one is independently observable.
 *
 * Everything above this file is already proved correct on a PC, so if
 * something misbehaves after this compiles, it is in here.
 */
#include "sprinkles/board.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"

static const char *TAG = "board";

/* ---- pins and constants ------------------------------------------------
 * BOOT is on GPIO0 and readable directly. PWR arrives via EXIO4 on the
 * expander, so it needs an I2C read rather than a GPIO read.
 *
 * WARNING: holding PWR for about six seconds powers the board down at the
 * PMIC. That is not maskable in firmware, which is why the menu lives on
 * BOOT and only the nap lives on PWR. */
#define PIN_BOOT        GPIO_NUM_0
#define EXIO_PWR        4

#define AUDIO_RATE      16000
#define AUDIO_AMPLITUDE 6000        /* of 32767; matches the prototype's 0.06 */

/* ---- time -------------------------------------------------------------- */
int64_t board_now_ms(void) { return esp_timer_get_time() / 1000; }

bool board_rtc_get(int64_t *unix_ms, int *local_hour)
{
    /* TODO(bsp): read the PCF85063 over I2C.
     *
     * This is the single most important peripheral in the project: it is what
     * lets her age through deep sleep and through being switched off. On the
     * very first boot the RTC has never been set, so return false — app.c
     * treats that as a newborn rather than inventing a date.
     *
     * Only differences matter - it is what lets her age across a power cut -
     * so the zone is irrelevant as long as it is consistent. */
    (void)unix_ms; (void)local_hour;
    return false;
}

void board_rtc_set(int64_t unix_ms)
{
    /* TODO(bsp): write the PCF85063. Called once, when the clock is first
     * set — over Wi-Fi SNTP, or from a build-time constant, or by hand. */
    (void)unix_ms;
}

/* ---- randomness -------------------------------------------------------- */
double board_rand01(void) { return (double)esp_random() / 4294967296.0; }

/* ---- audio -------------------------------------------------------------
 * Square waves with an exponential decay, generated into an I2S buffer.
 * The prototype's WebAudio envelope decays from 0.06 to 0.0008 over each
 * note, which is a factor of 75 — reproduced here in the integer domain.
 *
 * There is no speaker on the board. Everything here is optional by design:
 * with the codec absent or muted, nothing else in the project changes. */
static bool audio_on = true;
void board_audio_enable(bool on) { audio_on = on; }

static int16_t *note_buf;           /* reused across notes */
#define NOTE_BUF_SAMPLES (AUDIO_RATE / 2)   /* half a second is the longest */

void board_beep(const spr_note_t *notes, int count)
{
    if (!audio_on || !notes || count <= 0) return;
    if (!note_buf) {
        note_buf = malloc(NOTE_BUF_SAMPLES * sizeof(int16_t));
        if (!note_buf) return;
    }

    for (int n = 0; n < count; n++) {
        int hz = notes[n].hz, ms = notes[n].ms;
        int samples = ms * AUDIO_RATE / 1000;
        if (samples > NOTE_BUF_SAMPLES) samples = NOTE_BUF_SAMPLES;

        if (hz <= 0) {                       /* a rest */
            memset(note_buf, 0, (size_t)samples * sizeof(int16_t));
        } else {
            int period = AUDIO_RATE / hz;
            if (period < 2) period = 2;
            for (int i = 0; i < samples; i++) {
                double env = exp(-4.3 * (double)i / samples);   /* ~1/75 */
                int16_t v = (int16_t)(AUDIO_AMPLITUDE * env);
                note_buf[i] = ((i % period) < period / 2) ? v : -v;
            }
        }

        /* TODO(bsp): i2s_channel_write(tx, note_buf,
         *                samples * sizeof(int16_t), &written, portMAX_DELAY);
         * Blocking is fine — the longest sequence is under a second and it
         * only ever runs in response to a press. If you find it stalling the
         * animation, hand this to a small task with a queue instead. */
    }
}

/* ---- display ----------------------------------------------------------- */
void board_panel_push(const uint16_t *fb565)
{
    /* TODO(bsp): esp_lcd_panel_draw_bitmap(panel, 0, 0,
     *                GFX_PANEL, GFX_PANEL, fb565);
     *
     * Later optimisation: push only the changed bounding box. Most of this
     * screen is pure black and never moves, so this is where the frame rate
     * is if you ever need it. */
    (void)fb565;
}

void board_brightness(int percent)
{
    /* TODO(bsp): panel brightness. Start LOW — 30 to 40 percent is plenty
     * indoors, and brightness is the dominant battery cost on AMOLED. Full
     * brightness on a 466x466 panel will empty a small cell in hours. */
    (void)percent;
}

/* ---- touch -------------------------------------------------------------
 * Screen coordinates convert to the 64x64 world by undoing the blit: the
 * scene sits at GFX_OFFSET plus the burn-in drift, scaled by GFX_SCALE.
 * Getting the drift term wrong here means taps land a pixel or two off,
 * which nobody would ever notice — but the arithmetic is free, so do it. */
int board_drift_x = 0, board_drift_y = 0;    /* set each frame by main */
int board_touch_raw_x = -1, board_touch_raw_y = -1;

bool board_touch(double *wx, double *wy)
{
    int raw_x = 0, raw_y = 0;
    bool down = false;

    /* TODO(bsp): read the CST9217. It reports up to two points; use the
     * first and ignore the rest — small hands rest a palm on the glass, and
     * a second contact must not register as a second tap.
     *
     * Require a real tap in main.c: press and release inside the same zone,
     * under 400 ms, with less than about 30 px of travel. */
    if (!down) return false;

    if (wx) *wx = (double)(raw_x - GFX_OFFSET - board_drift_x) / GFX_SCALE;
    if (wy) *wy = (double)(raw_y - GFX_OFFSET - board_drift_y) / GFX_SCALE;
    return true;
}

bool board_touch_ok(void) { return false; }   /* TODO(bsp) */
bool board_rtc_ok(void)   { return false; }   /* TODO(bsp) */
bool board_imu_ok(void)   { return false; }   /* TODO(bsp) */
bool board_audio_ok(void) { return false; }   /* TODO(bsp) */
bool board_pwr_ok(void)   { return false; }   /* TODO(bsp) */
void board_battery(int *p, int *mv, bool *c, bool *pr)   /* TODO(bsp) */
{ if(p)*p=-1; if(mv)*mv=0; if(c)*c=false; if(pr)*pr=false; }
void board_dev_screen(const char *const *l, int n, const char *b, int d)
{ (void)l; (void)n; (void)b; (void)d; }
void board_battery(int *p, int *mv, bool *c, bool *pr)   /* TODO(bsp) */
{ if(p)*p=-1; if(mv)*mv=0; if(c)*c=false; if(pr)*pr=false; }
void board_dev_screen(const char *const *l, int n, const char *b, int d)
{ (void)l; (void)n; (void)b; (void)d; }

/* ---- buttons ----------------------------------------------------------- */
static bool boot_last = true;       /* GPIO0 idles high */

bool board_button_down(int which)
{
    if (which == 0) return !gpio_get_level(PIN_BOOT);   /* active low */
    return false;                                       /* TODO(bsp): EXIO4 */
}

bool board_button_pressed(int which)
{
    if (which == 0) {
        bool now = gpio_get_level(PIN_BOOT);
        bool pressed = (boot_last && !now);      /* falling edge */
        boot_last = now;
        return pressed;
    }
    /* TODO(bsp): read EXIO4 through the expander for PWR, with the same
     * edge detection. Waveshare's button example does exactly this — start
     * from it and confirm which physical button is which before wiring the
     * nap to it. */
    return false;
}

/* ---- IMU --------------------------------------------------------------- */
bool board_picked_up(void)
{
    /* TODO(bsp): read the QMI8658 accelerometer and trigger on a jump in
     * the magnitude of acceleration — the prototype used a delta of about
     * 2.2 m/s^2 between consecutive reads, which felt right on a phone.
     * Expect to retune it once the board is in a case with a battery.
     *
     * This is the nicest feature on the whole device: pick her up and she
     * notices you. Worth the tuning time. */
    return false;
}

/* ---- persistence ------------------------------------------------------- */
#define NVS_NS  "sprinkles"
#define NVS_KEY "state"

bool board_load(void *blob, uint32_t len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sz = len;
    esp_err_t err = nvs_get_blob(h, NVS_KEY, blob, &sz);
    nvs_close(h);
    return err == ESP_OK && sz == len;
}

void board_save(const void *blob, uint32_t len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, NVS_KEY, blob, len) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

bool board_stamp_load(char *buf, int cap)
{
    (void)buf; (void)cap;
    return false;
}
void board_stamp_save(const char *stamp) { (void)stamp; }

/* ---- power ------------------------------------------------------------- */
void board_init_quiet(int q) { (void)q; }
void board_deep_sleep(void) { }              /* TODO(bsp) */
bool board_woke_from_deep_sleep(void) { return false; }

void board_sleep_until_touched(void)
{
    /* Light sleep, not deep: she keeps her RAM, so waking is instant and
     * feels like she noticed you rather than like a device booting.
     *
     * TODO(bsp): enable the touch controller's interrupt line and the IMU's
     * motion interrupt as EXT1 wake sources, then:
     *     esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
     *     esp_light_sleep_start();
     *
     * Turn the panel off first — that is the entire point of the exercise.
     * Deep sleep is the bigger win if battery life still disappoints, and
     * pet_tick() already handles an arbitrarily long catch-up correctly,
     * which is unit-tested. */
    ESP_LOGW(TAG, "board_sleep_until_touched() is a stub");
    vTaskDelay(pdMS_TO_TICKS(100));
}

/* ---- one-time init ----------------------------------------------------- */
void board_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    gpio_config_t boot_cfg = {
        .pin_bit_mask = 1ULL << PIN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&boot_cfg);

    /* TODO(bsp): display, touch, PMIC, RTC, IMU and codec init, in that
     * order. Log each one — knowing which peripheral failed to come up is
     * worth far more than a tidy boot. */
    ESP_LOGI(TAG, "board_init: NVS and BOOT ready; peripherals are stubs");
}
