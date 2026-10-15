/* wallclock.h — calendar <-> unix time, without the C library.
 *
 * The RTC speaks Y/M/D H:M:S; pet.c wants milliseconds. Neither newlib's
 * mktime nor timezones are involved - the only thing that matters is that
 * the difference between two readings is right, so she ages correctly across
 * a power cut.
 *
 * Unit-tested in host/test_clock.c, because off-by-one-day bugs in date
 * arithmetic are invisible until they aren't.
 */
#ifndef WALLCLOCK_H
#define WALLCLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Days since 1970-01-01 from a proleptic Gregorian date. Howard Hinnant's
 * algorithm: shifts the year to start in March so leap day lands last. */
int64_t wc_days_from_civil(int y, unsigned m, unsigned d);
void    wc_civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d);

int64_t wc_to_ms(int y, unsigned mo, unsigned d,
                 unsigned h, unsigned mi, unsigned s);
void    wc_from_ms(int64_t ms, int *y, unsigned *mo, unsigned *d,
                   unsigned *h, unsigned *mi, unsigned *s);

/* Parse the compiler's __DATE__ / __TIME__, e.g. "Sep  4 2026", "07:31:22".
 * Used to seed the RTC on a board whose clock has never been set: she then
 * ages from the moment you flashed her, which needs no network and no UI. */
int64_t wc_build_time_ms(const char *date_str, const char *time_str);

#ifdef __cplusplus
}
#endif
#endif
