/* board.h — every hardware touchpoint in the project, in one interface.
 *
 * app.c and everything below it call only these. There are two
 * implementations: board_esp32.c (real) and board_host.c (a stub that lets
 * the whole interaction layer be unit-tested on a PC).
 *
 * If you find yourself including an esp_* header anywhere outside
 * board_esp32.c, something has leaked.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- lifecycle -------------------------------------------------------- */
void board_init(void);
void board_init_quiet(int quiet);   /* skips the splash picture */

/* Boot lines. Serial always. Only a failure is drawn on the glass, because
 * a normal boot shows the splash picture. Use during boot only; the frame
 * loop overwrites the screen. */
void board_diag(const char *line);
void board_diag_fail(const char *line);
bool board_display_ok(void);
bool board_touch_ok(void);
bool board_rtc_ok(void);
bool board_imu_ok(void);
bool board_audio_ok(void);
bool board_pwr_ok(void);

/* ---- battery (AXP2101) ------------------------------------------------
 * percent is -1 when no cell is attached. Charge current and the registers
 * that keep the cell powering the board live in the Arduino board code. */
void board_battery(int *percent, int *millivolts, bool *charging, bool *present);
/* One serial phrase: cell voltage, USB voltage, charger state, status bytes. */
void board_batt_info(char *buf, int len);

/* ---- grown-up screen --------------------------------------------------
 * Drawn at native panel resolution with the display library's own font, NOT
 * through the 64x64 pixel world. It is a diagnostic for an adult, so
 * legibility beats matching the toy's look. First line is the heading. */
/* The button is a touch target, so its rectangle has to be agreed between
 * whoever draws it and whoever tests the tap. Panel pixels, not world. */
#define DEV_BTN_X0 118
#define DEV_BTN_Y0 328
#define DEV_BTN_X1 348
#define DEV_BTN_Y1 394

void board_dev_screen(const char *const *lines, int count,
                      const char *button, int danger);

/* Set every frame by the main loop; board_touch uses them to undo the
 * burn-in drift when converting screen coordinates to world coordinates. */
extern int board_drift_x, board_drift_y;

/* Last raw controller coordinates, or -1 when nothing is down. Only for
 * logging — useful for checking the screen-to-world conversion by eye. */
extern int board_touch_raw_x, board_touch_raw_y;

/* ---- time ------------------------------------------------------------- */
int64_t board_now_ms(void);        /* monotonic since boot                */

/* Wall clock from the RTC. Returns false if the RTC has never been set —
 * in which case treat the pet as newborn rather than inventing a date. */
bool board_rtc_get(int64_t *unix_ms, int *local_hour);
void board_rtc_set(int64_t unix_ms);

/* ---- randomness ------------------------------------------------------- */
double board_rand01(void);

/* ---- audio ------------------------------------------------------------ */
/* Named spr_note_t, not note_t: the ESP32 Arduino core already defines a
 * note_t enum in esp32-hal-ledc.h for ledcWriteNote. Do not rename it back. */
typedef struct { int hz; int ms; } spr_note_t;    /* hz==0 is a rest */
void board_beep(const spr_note_t *notes, int count);
void board_audio_enable(bool on);

/* ---- display ---------------------------------------------------------- */
void board_panel_push(const uint16_t *fb565);
void board_brightness(int percent);            /* 0..100 */

/* ---- input ------------------------------------------------------------ */
/* One touch point, already converted to world (64x64) coordinates.
 * Returns false when nothing is down. */
bool board_touch(double *wx, double *wy);

/* CONFIRMED ON HARDWARE: BOOT is the LOWER side button, PWR is the upper one.
 * (I originally had this backwards, inferred from the product render.)
 *
 * 0 = BOOT (lower, GPIO0)   1 = PWR (upper, via the I2C expander)
 *
 * The case covers PWR. On the Arduino build that pin is not a nap button:
 * EXIO4 rises on its own and each edge was flipping her between nap and
 * wake. A dark screen is the nap. Holding PWR ~6 s still powers the board
 * off at the PMIC, and that timer is not something firmware can turn off. */
bool board_button_pressed(int which);      /* edge: true once per press */
bool board_button_down(int which);         /* level: true while held     */

/* True once when the IMU sees the device picked up. */
bool board_picked_up(void);

/* ---- persistence ------------------------------------------------------ */
bool board_load(void *blob, uint32_t len);
void board_save(const void *blob, uint32_t len);

/* A short note beside the pet: the upload whose Start button was pressed.
 * Missing means the button is still waiting. Not part of the pet save. */
bool board_stamp_load(char *buf, int cap);
void board_stamp_save(const char *stamp);

/* ---- power ------------------------------------------------------------ */
/* Light sleep with wake on touch or IMU. Returns when something woke us. */
void board_sleep_until_touched(void);

/* Deep sleep. Does NOT return - the device reboots on wake.
 *
 * Quiets every peripheral first, which is the part that actually saves the
 * power: the PMIC keeps the display controller, touch chip, IMU and codec
 * powered no matter what the ESP32 does, so deep sleep on its own would
 * change very little. Waking is the lower side button (BOOT) only.
 *
 * Everything needed to survive this is already in place: the RTC keeps wall
 * time and the pet is in NVS, with the catch-up maths unit-tested. */
void board_deep_sleep(void);

/* True when this boot came from board_deep_sleep rather than power-on, so
 * the caller can skip the boot diagnostics and go straight to her. */
bool board_woke_from_deep_sleep(void);

#ifdef __cplusplus
}
#endif

#endif
