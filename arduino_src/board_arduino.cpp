/* board_arduino.cpp — the Arduino flavour of board.h.
 *
 * Everything that can be written against stock Arduino APIs is written.
 * What's left is the display, touch, RTC, IMU and codec, because those need
 * this board's specific drivers — take them from the Arduino examples in
 * Waveshare's repo, which is the shortest path by a mile:
 *
 *   github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75  ->  Arduino Examples
 *
 * Each hole is marked TODO(ws) and BRINGUP.md has the order to fill them in.
 * Nothing above this file knows hardware exists, and it's all been verified
 * on a PC, so a misbehaviour after this compiles is in here.
 */
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <WiFi.h>
#include <esp_bt.h>

/* Display: the same stack Waveshare's own Arduino examples use.
 *
 * Install via Library Manager: "GFX Library for Arduino" (moononournation).
 * pin_config.h is already in this sketch folder. It is the Waveshare
 * ESP32-S3-Touch-AMOLED-1.75 wiring:
 *   TP_INT 11, TP_RESET 40          touch
 *   I2S: MCLK 42, BCLK 9, WS 45, DO 10, DI 8
 *   PA 46                           speaker amp enable - the audio stays
 *                                   silent until this is driven high
 *   XPOWERS_CHIP_AXP2101            the PMIC, via XPowersLib
 */
#include "Arduino_GFX_Library.h"

/* Touch: CST9217 via SensorLib (lewisxhe). Library Manager -> "SensorLib".
 * Same driver and calls as Waveshare's 10_Touch_CST9217 example. */
/* SensorLib renamed its umbrella header. Prefer the new one where it exists
 * so current versions stop warning, without breaking older installs. */
#if defined(__has_include)
#  if __has_include("TouchDrv.hpp")
#    include "TouchDrv.hpp"
#  else
#    include "TouchDrvCSTXXX.hpp"
#  endif
#else
#  include "TouchDrvCSTXXX.hpp"
#endif

/* RTC, also SensorLib. RtcDrv.hpp is the current umbrella; the old
 * SensorPCF85063.hpp header only prints a deprecation warning and then
 * includes the same driver. The IMU header is included only when USE_IMU
 * is turned on, further down — this build does not start the QMI8658. */
#include "RtcDrv.hpp"

/* Audio: arduino-esp32's I2S class plus Espressif's ES8311 driver, which is
 * vendored next to this file (es8311.c/.h/es8311_reg.h, Apache-2.0, taken
 * verbatim from Waveshare's 08_ES8311 example). */
#include "ESP_I2S.h"
extern "C" {
#include "es8311.h"
}

/* pin_config.h MUST come before XPowersLib.h. It is what defines
 * XPOWERS_CHIP_AXP2101, and XPowersLib only typedefs XPowersPMU to the right
 * chip class if that symbol is already visible. Include them the other way
 * round and you get "XPowersPMU does not name a type", which reads like a
 * missing library rather than an ordering mistake. Their own example puts
 * pin_config first for exactly this reason. */
#include "pin_config.h"

/* Belt and braces: pin_config.h defines this, but if it ever stops doing so
 * the failure is an inscrutable "XPowersPMU does not name a type" rather
 * than a missing-define message. */
#ifndef XPOWERS_CHIP_AXP2101
#define XPOWERS_CHIP_AXP2101
#endif

/* Battery telemetry. Library Manager -> "XPowersLib". */
#include "XPowersLib.h"

extern "C" {
#include "board.h"
#include "gfx.h"
#include "wallclock.h"
}

/* ---- TCA9554 I/O expander ----------------------------------------------
 * From the board's HARDWARE_REFERENCE: the expander sits at 0x20 and P4 /
 * EXIO4 carries SYS_OUT, the conditioned PWR-button state — HIGH while
 * pressed, low while released. Note that is the opposite polarity to BOOT,
 * which is pulled up and reads low when pressed.
 *
 * Reading it needs no AXP2101 register transaction, so a plain two-byte I2C
 * read is the whole driver. Registers are the standard TCA9554 map:
 *   0x00 input, 0x01 output, 0x02 polarity, 0x03 config (1 = input).
 *
 * P7 is GPS_RST on the -G variant, so the config register is read-modify-
 * written rather than blatted with 0xFF. */
#define EXP_ADDR      0x20
#define EXP_REG_INPUT 0x00
#define EXP_REG_CONFIG 0x03
#define EXIO_PWR_BIT  (1 << 4)

static bool exp_ok = false;

static bool exp_read(uint8_t reg, uint8_t *val)
{
    Wire.beginTransmission(EXP_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)EXP_ADDR, (uint8_t)1) != 1) return false;
    *val = Wire.read();
    return true;
}

static bool exp_write(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(EXP_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

/* ---- pins --------------------------------------------------------------
 * BOOT is on GPIO0 and readable with digitalRead. PWR arrives through EXIO4
 * on the I2C expander, so it needs a register read rather than a pin read.
 *
 * WARNING: holding PWR for about six seconds powers the board down at the
 * PMIC. Not maskable in firmware — which is why the menu is on BOOT and only
 * the nap is on PWR. */
#define PIN_BOOT   0

/* I2C pins come from pin_config.h as IIC_SDA / IIC_SCL. */

/* ---- time -------------------------------------------------------------- */
extern "C" int64_t board_now_ms(void) { return (int64_t)millis(); }

static SensorPCF85063 rtc;
static bool rtc_ok = false;

extern "C" bool board_rtc_ok(void) { return rtc_ok; }

extern "C" bool board_rtc_get(int64_t *unix_ms, int *local_hour)
{
    if (!rtc_ok) return false;
    RTC_DateTime t = rtc.getDateTime();

    /* A PCF85063 that has lost its backup supply comes up somewhere around
     * the year 2000. Treat anything implausible as "never set" so app.c
     * starts her fresh instead of ageing her by a quarter century. */
    if (t.getYear() < 2024) return false;

    if (local_hour) *local_hour = t.getHour();
    if (unix_ms)
        *unix_ms = wc_to_ms(t.getYear(), t.getMonth(), t.getDay(),
                            t.getHour(), t.getMinute(), t.getSecond());
    return true;
}

extern "C" void board_rtc_set(int64_t unix_ms)
{
    if (!rtc_ok) return;
    int y; unsigned mo, d, h, mi, s;
    wc_from_ms(unix_ms, &y, &mo, &d, &h, &mi, &s);
    rtc.setDateTime((uint16_t)y, (uint8_t)mo, (uint8_t)d,
                    (uint8_t)h, (uint8_t)mi, (uint8_t)s);
    Serial.printf("RTC set to %04d-%02u-%02u %02u:%02u:%02u (local)\n",
                  y, mo, d, h, mi, s);
}

/* ---- randomness -------------------------------------------------------- */
extern "C" double board_rand01(void)
{
    return (double)esp_random() / 4294967296.0;
}

/* ---- audio -------------------------------------------------------------
 * Square waves with an exponential decay, matching the prototype's envelope
 * (0.06 down to 0.0008 per note, a factor of about 75).
 *
 * There is no speaker on this board. All of this is optional by design: mute
 * it and nothing else in the project changes. */
#define AUDIO_RATE     16000
#define AUDIO_AMP       6000    /* of 32767; matches the prototype's 0.06 */
#define NOTE_MAX_MS      400    /* headroom over the longest note (300) */
#define NOTE_MAX_FRAMES (AUDIO_RATE * NOTE_MAX_MS / 1000)
#define AUDIO_VOLUME      75    /* 0..100 into the codec */

static I2SClass i2s;
static bool     audio_on = true;
static bool     audio_ok = false;
static int16_t *note_buf = nullptr;      /* interleaved stereo */
static es8311_handle_t es_dev = nullptr;

/* The amplifier is only enabled while something is actually playing.
 *
 * Left on, it sits amplifying whatever the codec's output drifts to, and
 * every wake from light sleep lands as an audible tick. Switching it also
 * pops, so each beep is padded with a few milliseconds of digital silence:
 * the DAC is sitting at zero when the amp comes up and when it goes down. */
static void amp_enable(bool on)
{
    if (es_dev) es8311_voice_mute(es_dev, !on);
    digitalWrite(PA, on ? HIGH : LOW);
}

static void write_silence(int ms)
{
    int frames = ms * AUDIO_RATE / 1000;
    if (frames > NOTE_MAX_FRAMES) frames = NOTE_MAX_FRAMES;
    memset(note_buf, 0, (size_t)frames * 2 * sizeof(int16_t));
    i2s.write((uint8_t *)note_buf, (size_t)frames * 2 * sizeof(int16_t));
}

extern "C" void board_audio_enable(bool on) { audio_on = on; }
extern "C" bool board_audio_ok(void) { return audio_ok; }

static bool audio_begin(void)
{
    /* The codec needs its rails settled before it will accept configuration.
     * On a cold boot it got that for free from the colour self-test; a wake
     * from deep sleep skips the test, so the delay has to be explicit or the
     * ES8311 configures on the second boot and simply does not play. */
    delay(200);

    /* PA is the amplifier enable. Without driving this you will hear nothing
     * at all, however correct the I2S and the codec are. It starts LOW and
     * is raised only around actual playback - see amp_enable(). */
    pinMode(PA, OUTPUT);
    digitalWrite(PA, LOW);

    i2s.setPins(BCLKPIN, WSPIN, DIPIN, DOPIN, MCLKPIN);
    if (!i2s.begin(I2S_MODE_STD, AUDIO_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("audio: I2S init FAILED");
        return false;
    }
    Serial.println("audio: I2S up");

    es8311_handle_t es = es8311_create(0, ES8311_ADDRRES_0);
    if (!es) { Serial.println("audio: ES8311 create FAILED"); return false; }
    es_dev = es;
    Serial.println("audio: codec found");

    const es8311_clock_config_t clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = AUDIO_RATE * 256,
        .sample_frequency = AUDIO_RATE,
    };
    esp_err_t rc = es8311_init(es, &clk, ES8311_RESOLUTION_16,
                               ES8311_RESOLUTION_16);
    if (rc != ESP_OK) {
        Serial.printf("audio: ES8311 init FAILED (%d)\n", (int)rc);
        return false;
    }
    Serial.println("audio: codec configured");
    es8311_sample_frequency_config(es, clk.mclk_frequency, clk.sample_frequency);
    es8311_microphone_config(es, false);
    es8311_voice_volume_set(es, AUDIO_VOLUME, NULL);

    note_buf = (int16_t *)malloc((size_t)NOTE_MAX_FRAMES * 2 * sizeof(int16_t));
    if (!note_buf) { Serial.println("no RAM for the note buffer"); return false; }

    es8311_voice_mute(es, true);        /* stay quiet until asked */
    Serial.println("audio OK");
    return true;
}

extern "C" void board_beep(const spr_note_t *notes, int count)
{
    if (!audio_ok || !audio_on || !notes || count <= 0) return;

    /* Unmute and enable FIRST, then hold silence while the amplifier comes
     * out of shutdown and the codec's fade completes. 4 ms was not enough:
     * short high beeps - the sweep sound in particular - were starting
     * before the amp was actually passing signal, so they simply vanished. */
    amp_enable(true);
    write_silence(25);

    for (int n = 0; n < count; n++) {
        int hz = notes[n].hz;
        int frames = notes[n].ms * AUDIO_RATE / 1000;
        if (frames > NOTE_MAX_FRAMES) frames = NOTE_MAX_FRAMES;

        /* The bus is opened as stereo with both slots, so every frame needs
         * an L and an R sample. Writing mono into it plays at double speed. */
        for (int i = 0; i < frames; i++) {
            int16_t v = 0;
            if (hz > 0) {
                int period = AUDIO_RATE / hz;
                if (period < 2) period = 2;
                double env = exp(-4.3 * (double)i / frames);   /* ~1/75 decay */
                int16_t a = (int16_t)(AUDIO_AMP * env);
                v = ((i % period) < period / 2) ? a : -a;
            }
            note_buf[i * 2]     = v;
            note_buf[i * 2 + 1] = v;
        }
        i2s.write((uint8_t *)note_buf, (size_t)frames * 2 * sizeof(int16_t));
    }

    write_silence(15);                  /* let the tail out before muting */
    amp_enable(false);
}

/* ---- display ----------------------------------------------------------- */
static Arduino_DataBus *bus = nullptr;
static Arduino_CO5300  *gfx = nullptr;

extern "C" bool board_display_ok(void) { return gfx != nullptr; }

/* Boot diagnostics, drawn on the AMOLED as well as printed. If USB CDC is
 * off you get nothing on serial, and a halted sketch looks exactly like a
 * broken panel — this tells the two apart without a debugger. */
static int diag_y = 0;
static void diag(const char *line, uint16_t colour)
{
    Serial.println(line);
    Serial.flush();
    if (!gfx) return;
    if (diag_y == 0) { gfx->fillScreen(RGB565_BLACK); diag_y = 120; }
    gfx->setTextSize(2);
    gfx->setTextColor(colour);
    gfx->setCursor(60, diag_y);
    gfx->println(line);
    diag_y += 24;
}
extern "C" void board_diag(const char *line)      { diag(line, 0xFFFF); }
extern "C" void board_diag_fail(const char *line) { diag(line, RGB565_RED); }

extern "C" void board_panel_push(const uint16_t *fb565)
{
    if (!gfx) return;
    /* One shot for the whole circle. Later optimisation if you ever want the
     * frame rate: push only the changed bounding box — most of this screen is
     * pure black and never moves.
     *
     * If the colours come out inverted or channel-swapped, it is byte order,
     * not the sprites: swap to draw16bitBeRGBBitmap, or flip the two bytes in
     * gfx_blit565. Do NOT go changing palette values. */
    gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)fb565, GFX_PANEL, GFX_PANEL);
}

int board_brightness_pct = 35;   /* remembered so sleep can restore it */

extern "C" void board_brightness(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    board_brightness_pct = percent;
    if (!gfx) return;
    /* Brightness is the dominant battery cost on AMOLED, and full brightness
     * on a 466x466 panel is genuinely unpleasant indoors. 35 is a good start. */
    gfx->setBrightness((uint8_t)(percent * 255 / 100));
}

/* ---- touch -------------------------------------------------------------
 * Screen coordinates convert to the 64x64 world by undoing the blit: the
 * scene sits at GFX_OFFSET plus the burn-in drift, scaled by GFX_SCALE.
 * main() sets the drift every frame — don't skip it or taps land a couple of
 * pixels off from where she's drawn. */
int board_drift_x = 0, board_drift_y = 0;   /* declared in board.h */
int board_touch_raw_x = -1, board_touch_raw_y = -1;   /* for logging */

static TouchDrvCST92xx touch;
static bool touch_ok = false;

extern "C" bool board_touch(double *wx, double *wy)
{
    if (!touch_ok) return false;

    /* Only ever the FIRST contact. The controller reports two, and small
     * hands rest a palm on the glass — a second contact must not read as a
     * second tap. Polled once per frame, which is 66 ms resolution: plenty
     * for taps, and it avoids needing the interrupt to be wired. */
    int16_t x[2] = {0, 0}, y[2] = {0, 0};
    if (touch.getPoint(x, y, 1) == 0) {
        board_touch_raw_x = board_touch_raw_y = -1;
        return false;
    }

    board_touch_raw_x = x[0];
    board_touch_raw_y = y[0];
    if (wx) *wx = (double)(x[0] - GFX_OFFSET - board_drift_x) / GFX_SCALE;
    if (wy) *wy = (double)(y[0] - GFX_OFFSET - board_drift_y) / GFX_SCALE;
    return true;
}

extern "C" bool board_touch_ok(void) { return touch_ok; }

/* ---- buttons ----------------------------------------------------------- */
static bool boot_last = true;      /* GPIO0 idles high */

extern "C" bool board_pwr_ok(void) { return exp_ok; }

extern "C" bool board_button_down(int which)
{
    if (which == 0) return !digitalRead(PIN_BOOT);   /* pulled up, active low */
    if (!exp_ok) return false;
    uint8_t in = 0;
    if (!exp_read(EXP_REG_INPUT, &in)) return false;
    return (in & EXIO_PWR_BIT) != 0;                 /* HIGH means pressed */
}

extern "C" bool board_button_pressed(int which)
{
    if (which == 0) {
        bool now = digitalRead(PIN_BOOT);
        bool pressed = (boot_last && !now);       /* falling edge */
        boot_last = now;
        return pressed;
    }
    /* PWR, rising edge (it reads HIGH while held). */
    static bool pwr_last = false;
    bool now = board_button_down(1);
    bool pressed = (!pwr_last && now);
    pwr_last = now;
    return pressed;
}

/* ---- IMU --------------------------------------------------------------- */
/* The IMU exists only to notice the device being picked up, and with deep
 * sleep that job is gone: waking is the side button now. The gyroscope was
 * never enabled at all. Set to 1 to bring it back for light-sleep builds. */
#define USE_IMU 0

#if USE_IMU
#include "ImuDrv.hpp"
static SensorQMI8658 qmi;
static float imu_last_mag = 0;
#endif
static bool  imu_ok = false;

extern "C" bool board_imu_ok(void) { return imu_ok; }

/* Trigger on a jump in the magnitude of acceleration between reads, which
 * catches being lifted without firing on a tap. Expect to retune the
 * threshold once she is in a case with a battery - it is the one number here
 * that has to be found by feel rather than reasoning. */
#define PICKUP_DELTA 2.2f

extern "C" bool board_picked_up(void)
{
#if !USE_IMU
    return false;
#else
    if (!imu_ok) return false;
    if (!qmi.isDataReady(static_cast<uint8_t>(ImuBase::DataReadyMask::ACCEL)))
        return false;

    AccelerometerData accel;
    if (!qmi.readAccel(accel)) return false;

    float mag = sqrtf(accel.mps2.x * accel.mps2.x +
                      accel.mps2.y * accel.mps2.y +
                      accel.mps2.z * accel.mps2.z);
    bool jumped = (imu_last_mag > 0.1f) && (fabsf(mag - imu_last_mag) > PICKUP_DELTA);
    imu_last_mag = mag;
    return jumped;
#endif
}

/* ---- battery ----------------------------------------------------------- */
static XPowersPMU pmu;
static bool pmu_ok = false;

extern "C" void board_battery(int *percent, int *mv, bool *charging, bool *present)
{
    if (percent)  *percent  = -1;
    if (mv)       *mv       = 0;
    if (charging) *charging = false;
    if (present)  *present  = false;
    if (!pmu_ok) return;

    bool have = pmu.isBatteryConnect();
    if (present) *present = have;
    if (!have) return;
    if (percent)  *percent  = pmu.getBatteryPercent();
    if (mv)       *mv       = pmu.getBattVoltage();
    if (charging) *charging = pmu.isCharging();
}

/* ---- grown-up screen --------------------------------------------------- */
extern "C" void board_dev_screen(const char *const *lines, int count,
                                 const char *button, int danger)
{
    if (!gfx) return;
    gfx->fillScreen(RGB565_BLACK);
    gfx->setTextSize(2);
    /* Start well inside the circle: near the top and bottom of a round panel
     * the usable width collapses and text gets clipped by the bezel. */
    int y = 78;
    for (int i = 0; i < count && y < 320; i++) {
        gfx->setTextColor(i == 0 ? 0xFE60 : 0xFFFF);   /* heading in amber */
        gfx->setCursor(74, y);
        gfx->println(lines[i]);
        y += (i == 0) ? 28 : 20;
    }

    if (button) {
        uint16_t fill = danger ? 0xB000 : 0x2124;      /* dark red / grey */
        uint16_t edge = danger ? 0xF9A6 : 0x8410;
        gfx->fillRoundRect(DEV_BTN_X0, DEV_BTN_Y0,
                           DEV_BTN_X1 - DEV_BTN_X0, DEV_BTN_Y1 - DEV_BTN_Y0,
                           12, fill);
        gfx->drawRoundRect(DEV_BTN_X0, DEV_BTN_Y0,
                           DEV_BTN_X1 - DEV_BTN_X0, DEV_BTN_Y1 - DEV_BTN_Y0,
                           12, edge);
        /* Rough centring: the built-in font is 6x8 per char at size 1. */
        int len = 0; while (button[len]) len++;
        int tw = len * 12;
        gfx->setTextColor(0xFFFF);
        gfx->setCursor(DEV_BTN_X0 + ((DEV_BTN_X1 - DEV_BTN_X0) - tw) / 2,
                       DEV_BTN_Y0 + 22);
        gfx->println(button);
    }
}

/* ---- persistence ------------------------------------------------------- */
static Preferences prefs;

extern "C" bool board_load(void *blob, uint32_t len)
{
    if (!prefs.begin("sprinkles", true)) return false;
    size_t got = prefs.getBytes("state", blob, len);
    prefs.end();
    return got == len;
}

extern "C" void board_save(const void *blob, uint32_t len)
{
    if (!prefs.begin("sprinkles", false)) return;
    prefs.putBytes("state", blob, len);
    prefs.end();
}

/* ---- power ------------------------------------------------------------- */
/* Light sleep, not deep: she keeps her RAM, so waking is instant and feels
 * like she noticed you rather than like a device booting.
 *
 * Wake sources are the two real GPIOs — touch interrupt (11) and BOOT (0),
 * both active low. PWR cannot wake us: it only exists behind the I2C
 * expander, and neither can the IMU, whose INT1 is on expander P6.
 *
 * So being picked up is handled with a periodic wake instead: the timer pops
 * every couple of seconds, we read the accelerometer, and go straight back
 * to sleep unless she was actually lifted. A few milliseconds of I2C every
 * two seconds is a rounding error next to a lit AMOLED, and it keeps the
 * nicest behaviour on the device working. */
/* Set to 0 for guaranteed silence while she sleeps.
 *
 * Waking periodically is the only way to notice being picked up, because
 * QMI_INT1 is on the I/O expander rather than a real GPIO and so cannot be a
 * wake source. But every wake restarts the I2S clock, and a codec whose MCLK
 * stops and starts thumps its output. The amp is muted and its enable pin is
 * latched (see below), which should keep that inaudible - if any tick
 * survives on your board, turn this off and pickup-wake goes with it. */
#define SLEEP_PICKUP_POLL 1

/* 500 ms, not 3 s.
 *
 * The touch interrupt is the fast path, but it is not reliable enough on its
 * own - waking her by tapping was genuinely hard, which for a toy is the
 * same as broken. So the timer also POLLS the touch controller, and at half
 * a second the worst case still feels like a response rather than a wait.
 *
 * The cost is more wakes: a few milliseconds of I2C every 500 ms. Next to a
 * lit AMOLED that is noise, and the amplifier is latched off (see below) so
 * it no longer ticks. */
#define SLEEP_POLL_US (500 * 1000)

extern "C" bool board_woke_from_deep_sleep(void)
{
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
}

extern "C" void board_deep_sleep(void)
{
    Serial.println("[sleep] quieting peripherals, then deep sleep");
    Serial.flush();

    /* Order matters a little: silence the audio path before stopping the
     * clocks that feed it, or the codec thumps on the way down. */
    amp_enable(false);
    if (es_dev) es8311_voice_mute(es_dev, true);
    i2s.end();                       /* stops MCLK/BCLK to BOTH codecs */
    digitalWrite(PA, LOW);

    /* The display controller keeps its clocks running at brightness 0 -
     * this is the command that actually puts it away. */
    if (gfx) { gfx->setBrightness(0); gfx->displayOff(); }

    /* Nothing needs to notice a touch or a lift any more, so both can stop
     * scanning entirely. The IMU in particular was running at 1 kHz. */
    if (touch_ok) touch.sleep();
#if USE_IMU
    if (imu_ok)   qmi.disableAccel();
#endif

    /* BOOT is the only wake source. PWR cannot be one - it lives behind the
     * I2C expander - and neither can the IMU, whose INT1 is on expander P6. */
    gpio_hold_en((gpio_num_t)PA);
    if (rtc_gpio_is_valid_gpio((gpio_num_t)PIN_BOOT)) {
        rtc_gpio_pullup_en((gpio_num_t)PIN_BOOT);
        rtc_gpio_pulldown_dis((gpio_num_t)PIN_BOOT);
    }
    esp_sleep_enable_ext1_wakeup(1ULL << PIN_BOOT, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();          /* never returns */
}

extern "C" void board_sleep_until_touched(void)
{
    if (gfx) { gfx->setBrightness(0); gfx->displayOff(); }
    amp_enable(false);

    /* Latch the amplifier-enable pin LOW for the duration.
     *
     * Light sleep powers down the digital peripheral domain, and an ordinary
     * GPIO output does not necessarily hold its level through that - the pad
     * drifts, the amp wakes up with no valid clock from the codec, and you
     * get a tick on every single wake. gpio_hold_en pins the pad in the RTC
     * domain so it physically cannot move while we are asleep. */
    gpio_hold_en((gpio_num_t)PA);

    /* Both wake lines idle HIGH and are driven low by their source, so the
     * pull-ups have to survive the sleep too. pinMode's pull-up lives in the
     * digital domain, which light sleep powers down - leaving the pads
     * floating and the wake unreliable. These are the RTC-domain equivalents. */
    if (rtc_gpio_is_valid_gpio((gpio_num_t)TP_INT)) {
        rtc_gpio_pullup_en((gpio_num_t)TP_INT);
        rtc_gpio_pulldown_dis((gpio_num_t)TP_INT);
    }
    if (rtc_gpio_is_valid_gpio((gpio_num_t)PIN_BOOT)) {
        rtc_gpio_pullup_en((gpio_num_t)PIN_BOOT);
        rtc_gpio_pulldown_dis((gpio_num_t)PIN_BOOT);
    }

    const uint64_t mask = (1ULL << TP_INT) | (1ULL << PIN_BOOT);

    for (;;) {
        esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
        esp_sleep_enable_timer_wakeup(SLEEP_POLL_US);
        esp_light_sleep_start();

        esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
        if (why != ESP_SLEEP_WAKEUP_TIMER) break;      /* interrupt: instant */

        /* Timer: poll everything that cannot raise an interrupt we can hear.
         * Touch is polled as well as interrupt-driven precisely because the
         * interrupt alone was missing taps. */
        double px, py;
        if (board_touch(&px, &py)) break;
        if (board_button_down(0)) break;
#if SLEEP_PICKUP_POLL
        if (board_picked_up()) break;                  /* lifted */
        if (board_button_down(1)) break;               /* PWR */
#endif
    }

    gpio_hold_dis((gpio_num_t)PA);
    if (gfx) { gfx->displayOn(); gfx->setBrightness(board_brightness_pct * 255 / 100); }
}

/* ---- init -------------------------------------------------------------- */
static bool init_quiet = false;

extern "C" void board_init_quiet(int quiet)
{
    init_quiet = quiet != 0;
    board_init();
}

extern "C" void board_init(void)
{
    /* Release anything the previous deep sleep latched.
     *
     * board_deep_sleep pins the amplifier-enable pin low and puts a pull-up
     * on BOOT in the RTC domain. Those holds are not automatically cleared
     * by a wake, and a latched PA means the amp can never be enabled again -
     * everything else about the audio path looks perfectly healthy while
     * producing complete silence. */
    gpio_hold_dis((gpio_num_t)PA);
    gpio_deep_sleep_hold_dis();
    if (rtc_gpio_is_valid_gpio((gpio_num_t)PIN_BOOT))
        rtc_gpio_deinit((gpio_num_t)PIN_BOOT);

    /* Neither radio is ever used, and their PHY power domains are not free.
     * This is the cheapest saving in the whole project. */
    WiFi.mode(WIFI_OFF);
    WiFi.disconnect(true);
    btStop();

    pinMode(PIN_BOOT, INPUT_PULLUP);
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);

    bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK,
                                LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
    gfx = new Arduino_CO5300(bus, LCD_RESET, 0 /* rotation */,
                             LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);
    if (!gfx->begin()) {
        Serial.println("gfx->begin() FAILED - check pin_config.h wiring");
        delete gfx; gfx = nullptr;
    } else {
        /* Flash the primaries on a cold boot. If you see red, green, blue go
         * past, the panel, the QSPI bus and the pin config are all good -
         * regardless of whether serial is working. Skipped when waking from
         * deep sleep, which happens every time she is roused. */
        gfx->displayOn();
        gfx->setBrightness(160);
        if (!init_quiet) {
            gfx->fillScreen(RGB565_RED);   delay(250);
            gfx->fillScreen(RGB565_GREEN); delay(250);
            gfx->fillScreen(RGB565_BLUE);  delay(250);
        }
        gfx->fillScreen(RGB565_BLACK);
    }

    /* Touch, exactly as their 10_Touch_CST9217 example does it. */
    touch.setPins(TP_RESET, TP_INT);
    if (!touch.begin(Wire, CST92XX_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        Serial.println("CST9217 init FAILED");
    } else {
        touch_ok = true;
        /* The raw-coordinate example omits these two calls; the LVGL example,
         * which actually produces a usable UI, includes them. Without the
         * mirror, taps land on the opposite side of the screen from where you
         * pressed - which reads as "touch works" until you notice the cupcake
         * button is answering to presses on the broom. */
        touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
        touch.setMirrorXY(true, true);
        pinMode(TP_INT, INPUT_PULLUP);
        Serial.printf("touch OK: %s, %u points\n",
                      touch.getModelName(),
                      (unsigned)touch.getSupportTouchPoint());
    }

    /* RTC. If its backup supply has been lost, seed it from the build time:
     * she then ages from the moment you flashed her, with no network, no UI
     * and no clock-setting ritual for a parent to perform. */
    if (!rtc.begin(Wire, IIC_SDA, IIC_SCL)) {
        Serial.println("PCF85063 init FAILED");
    } else {
        rtc_ok = true;
        int64_t ms; int hr;
        if (!board_rtc_get(&ms, &hr)) {
            Serial.println("RTC never set - seeding from build time");
            board_rtc_set(wc_build_time_ms(__DATE__, __TIME__));
        } else {
            Serial.printf("RTC OK, local hour %d\n", hr);
        }
    }

    /* IMU, for wake-on-pickup. Left un-initialised by default - see USE_IMU.
     * An un-begun QMI8658 sits in its power-on default, which is idle. */
#if USE_IMU
    if (!qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        Serial.println("QMI8658 init FAILED");
    } else {
        /* 1000 Hz is what the vendor demo uses and it is wildly more than
         * this needs - we sample it twice a second to notice a toy being
         * picked up. Low-power 11 Hz is still five times our read rate. */
        qmi.configAccel(AccelFullScaleRange::FS_4G,
                        11.0f,
                        SensorQMI8658::LpfMode::MODE_0);
        qmi.enableAccel();
        imu_ok = true;
        Serial.println("IMU OK");
    }
#else
    Serial.println("IMU disabled (USE_IMU 0)");
#endif

    /* Audio last: it is the only one the toy can do without. */
    audio_ok = audio_begin();

    /* PMIC, for battery telemetry. Measurement only - charge current and
     * target voltage are left as the board ships them. */
    if (!pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        Serial.println("AXP2101 not found - no battery telemetry");
    } else {
        pmu_ok = true;
        pmu.enableBattDetection();
        pmu.enableBattVoltageMeasure();
        pmu.enableVbusVoltageMeasure();
        pmu.enableSystemVoltageMeasure();
        Serial.printf("PMU OK, battery %s\n",
                      pmu.isBatteryConnect() ? "present" : "absent");
    }

    /* I/O expander, for the PWR button on EXIO4. */
    {
        uint8_t cfg = 0;
        if (exp_read(EXP_REG_CONFIG, &cfg)) {
            /* Make P4 an input without disturbing P7 (GPS_RST on the -G). */
            exp_write(EXP_REG_CONFIG, (uint8_t)(cfg | EXIO_PWR_BIT));
            exp_ok = true;
            Serial.println("expander OK (PWR on EXIO4)");
        } else {
            Serial.println("TCA9554 not found - PWR button unavailable");
        }
    }
}
