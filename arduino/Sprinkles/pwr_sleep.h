/* A long press on the upper power button.
 *
 * EXIO4 rises on its own, so a tap on that line is not a button. Three
 * continuous seconds, and only after the line has been seen released, is
 * a person holding it. The lower button held with it is the grown-up
 * screen, so that does not count.
 *
 * The chip still powers the board off at about six seconds. This timer
 * is shorter on purpose, and nothing here writes that register. */
#ifndef PWR_SLEEP_H
#define PWR_SLEEP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PWR_SLEEP_MS 3000

typedef struct {
    int64_t down_at;
    int     counting;
    int     seen_up;
    int     fired;
} pwr_hold_t;

/* 1 on the single frame a PWR-only hold reaches hold_ms. */
int pwr_hold_step(pwr_hold_t *s, int64_t now_ms, int pwr_down,
                  int boot_down, int64_t hold_ms);

#ifdef __cplusplus
}
#endif

#endif
