/* board_host.c — a board that isn't there, so app.c can be tested on a PC.
 * Sounds and pushes are recorded rather than performed, which lets tests
 * assert on them. */
#include "../sprinkles/board.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int64_t  hb_now = 0;
double   hb_rand = 0.99;          /* high = "nothing random happens" */
/* Consecutive calls walk this ring, so a test can say "no mess, but do
 * roll" — app_tick asks for randomness more than once per second. */
double   hb_rand_ring[8] = { 0.99 };
int      hb_rand_ring_n = 1;
int board_drift_x = 0, board_drift_y = 0;
int board_touch_raw_x = -1, board_touch_raw_y = -1;
int      hb_rand_i = 0;   /* tests reset this; it must not leak between them */
int      hb_beeps = 0;
bool     hb_audio = true;
spr_note_t   hb_last_beep[48];
int      hb_last_beep_n = 0;
int      hb_pushes = 0;
int      hb_brightness = 100;
int      hb_slept = 0;
static unsigned char hb_blob[1024];
static int hb_have_blob = 0;

int64_t board_now_ms(void) { return hb_now; }
double  board_rand01(void)
{
    if (hb_rand_ring_n <= 1) return hb_rand;
    double v = hb_rand_ring[hb_rand_i % hb_rand_ring_n];
    hb_rand_i++;
    return v;
}

bool board_rtc_get(int64_t *unix_ms, int *hour)
{ if (unix_ms) *unix_ms = hb_now; if (hour) *hour = 12; return true; }
void board_rtc_set(int64_t unix_ms) { (void)unix_ms; }

void board_beep(const spr_note_t *n, int c)
{
    hb_beeps++;
    hb_last_beep_n = c > 48 ? 48 : c;
    memcpy(hb_last_beep, n, (size_t)hb_last_beep_n * sizeof(spr_note_t));
}
void board_audio_enable(bool on) { hb_audio = on; }
void board_panel_push(const uint16_t *fb) { (void)fb; hb_pushes++; }
void board_brightness(int p) { hb_brightness = p; }
bool board_touch(double *x, double *y) { (void)x; (void)y; return false; }
bool board_touch_ok(void) { return false; }
bool board_rtc_ok(void) { return false; }
bool board_imu_ok(void) { return false; }
bool board_audio_ok(void) { return false; }
bool board_pwr_ok(void) { return false; }
void board_battery(int *p, int *mv, bool *c, bool *pr)
{ if(p)*p=-1; if(mv)*mv=0; if(c)*c=false; if(pr)*pr=false; }
void board_batt_info(char *buf, int len)
{ if (buf && len > 0) snprintf(buf, (size_t)len, "n/a"); }
void board_dev_screen(const char *const *l, int n, const char *b, int d)
{ (void)l; (void)n; (void)b; (void)d; }
bool board_button_pressed(int w) { (void)w; return false; }
bool board_button_down(int w) { (void)w; return false; }
bool board_picked_up(void) { return false; }

bool board_load(void *blob, uint32_t len)
{
    if (!hb_have_blob) return false;
    memcpy(blob, hb_blob, len > sizeof hb_blob ? sizeof hb_blob : len);
    return true;
}
void board_save(const void *blob, uint32_t len)
{
    memcpy(hb_blob, blob, len > sizeof hb_blob ? sizeof hb_blob : len);
    hb_have_blob = 1;
}
static char hb_stamp[48];
bool board_stamp_load(char *buf, int cap)
{
    if (!buf || cap < 2 || !hb_stamp[0]) return false;
    strncpy(buf, hb_stamp, (size_t)cap - 1);
    buf[cap - 1] = 0;
    return true;
}
void board_stamp_save(const char *stamp)
{
    if (!stamp) stamp = "";
    strncpy(hb_stamp, stamp, sizeof hb_stamp - 1);
    hb_stamp[sizeof hb_stamp - 1] = 0;
}
void board_init_quiet(int q) { (void)q; }
void board_deep_sleep(void) { hb_slept++; }
bool board_woke_from_deep_sleep(void) { return false; }

void board_sleep_until_touched(void) { hb_slept++; }
