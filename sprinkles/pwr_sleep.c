#include "pwr_sleep.h"

int pwr_hold_step(pwr_hold_t *s, int64_t now_ms, int pwr_down,
                  int boot_down, int64_t hold_ms)
{
    if (!s) return 0;

    /* Released, or the lower button joined the press. Either one drops
     * the count. A release is also what arms the next hold, so a line
     * that is already high at boot cannot sleep her by itself. */
    if (!pwr_down || boot_down) {
        if (!pwr_down) s->seen_up = 1;
        s->counting = 0;
        s->fired = 0;
        return 0;
    }
    if (!s->seen_up || s->fired) return 0;

    if (!s->counting) {
        s->counting = 1;
        s->down_at = now_ms;
    }
    if (now_ms - s->down_at < hold_ms) return 0;

    s->fired = 1;
    return 1;
}
