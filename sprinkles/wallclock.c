#include "wallclock.h"

int64_t wc_days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);              /* 0..399 */
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;  /* 0..146096 */
    return era * 146097 + (int64_t)doe - 719468;
}

void wc_civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yr = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    const unsigned mm = mp + (mp < 10 ? 3 : -9);
    if (d) *d = dd;
    if (m) *m = mm;
    if (y) *y = (int)(yr + (mm <= 2));
}

int64_t wc_to_ms(int y, unsigned mo, unsigned d,
                 unsigned h, unsigned mi, unsigned s)
{
    int64_t days = wc_days_from_civil(y, mo, d);
    return ((days * 24 + h) * 60 + mi) * 60000LL + (int64_t)s * 1000LL;
}

void wc_from_ms(int64_t ms, int *y, unsigned *mo, unsigned *d,
                unsigned *h, unsigned *mi, unsigned *s)
{
    int64_t secs = ms / 1000;
    int64_t days = secs / 86400;
    int64_t rem  = secs % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }
    wc_civil_from_days(days, y, mo, d);
    if (h)  *h  = (unsigned)(rem / 3600);
    if (mi) *mi = (unsigned)((rem % 3600) / 60);
    if (s)  *s  = (unsigned)(rem % 60);
}

int64_t wc_build_time_ms(const char *date_str, const char *time_str)
{
    static const char *MON = "JanFebMarAprMayJunJulAugSepOctNovDec";
    if (!date_str || !time_str) return 0;

    unsigned mo = 1;
    for (unsigned i = 0; i < 12; i++)
        if (date_str[0] == MON[i * 3] &&
            date_str[1] == MON[i * 3 + 1] &&
            date_str[2] == MON[i * 3 + 2]) { mo = i + 1; break; }

    /* __DATE__ is exactly "MMM DD YYYY" with the day SPACE-padded, so
     * "Sep  4 2026" has a blank at index 4. Scanning until the next space
     * therefore reads nothing at all - use the fixed offsets instead. */
    unsigned d = 0;
    for (const char *p = date_str + 4; p < date_str + 6; p++)
        if (*p >= '0' && *p <= '9') d = d * 10 + (unsigned)(*p - '0');
    if (d == 0) d = 1;

    int yr = 0;
    for (const char *p = date_str + 7; p < date_str + 11 && *p; p++)
        if (*p >= '0' && *p <= '9') yr = yr * 10 + (*p - '0');

    unsigned h  = (unsigned)((time_str[0] - '0') * 10 + (time_str[1] - '0'));
    unsigned mi = (unsigned)((time_str[3] - '0') * 10 + (time_str[4] - '0'));
    unsigned s  = (unsigned)((time_str[6] - '0') * 10 + (time_str[7] - '0'));

    return wc_to_ms(yr, mo, d, h, mi, s);
}
