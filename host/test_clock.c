/* test_clock.c — date arithmetic, because off-by-one-day bugs in calendar
 * code are invisible until they aren't. */
#include "../sprinkles/wallclock.h"
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { \
    fails++; printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static void t_epoch(void)
{
    CHECK(wc_days_from_civil(1970, 1, 1) == 0, "epoch is day zero");
    CHECK(wc_to_ms(1970, 1, 1, 0, 0, 0) == 0, "epoch is ms zero");
    CHECK(wc_to_ms(1970, 1, 2, 0, 0, 0) == 86400000LL, "one day");
    CHECK(wc_to_ms(1970, 1, 1, 1, 0, 0) == 3600000LL, "one hour");
}

static void t_known_dates(void)
{
    /* 2000-03-01, the day after the leap day of a century leap year */
    CHECK(wc_days_from_civil(2000, 3, 1) == 11017, "2000-03-01, got %lld",
          (long long)wc_days_from_civil(2000, 3, 1));
    /* 2024-02-29 exists; 2100-02-29 does not (not a leap year) */
    CHECK(wc_days_from_civil(2024, 3, 1) - wc_days_from_civil(2024, 2, 28) == 2,
          "2024 has a leap day");
    CHECK(wc_days_from_civil(2100, 3, 1) - wc_days_from_civil(2100, 2, 28) == 1,
          "2100 does not");
    CHECK(wc_to_ms(2026, 9, 4, 12, 0, 0) == 1788523200000LL,
          "2026-09-04 12:00, got %lld", (long long)wc_to_ms(2026,9,4,12,0,0));
}

static void t_roundtrip(void)
{
    /* Every day for 60 years must survive there-and-back. This is the test
     * that actually catches sign and boundary errors. */
    int bad = 0;
    for (int64_t day = wc_days_from_civil(1990, 1, 1);
         day <= wc_days_from_civil(2050, 12, 31); day++) {
        int y; unsigned m, d;
        wc_civil_from_days(day, &y, &m, &d);
        if (wc_days_from_civil(y, m, d) != day) { bad++; if (bad < 3)
            printf("    day %lld -> %04d-%02u-%02u -> %lld\n", (long long)day,
                   y, m, d, (long long)wc_days_from_civil(y, m, d)); }
    }
    CHECK(bad == 0, "%d days failed the civil round-trip", bad);
}

static void t_ms_roundtrip(void)
{
    struct { int y; unsigned mo, d, h, mi, s; } cases[] = {
        { 2026,  9,  4,  7, 31, 22 },
        { 2026,  1,  1,  0,  0,  0 },
        { 2026, 12, 31, 23, 59, 59 },
        { 2024,  2, 29, 12, 34, 56 },
        { 2000,  3,  1,  1,  2,  3 },
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        int64_t ms = wc_to_ms(cases[i].y, cases[i].mo, cases[i].d,
                              cases[i].h, cases[i].mi, cases[i].s);
        int y; unsigned mo, d, h, mi, s;
        wc_from_ms(ms, &y, &mo, &d, &h, &mi, &s);
        CHECK(y == cases[i].y && mo == cases[i].mo && d == cases[i].d &&
              h == cases[i].h && mi == cases[i].mi && s == cases[i].s,
              "%04d-%02u-%02u %02u:%02u:%02u came back as "
              "%04d-%02u-%02u %02u:%02u:%02u",
              cases[i].y, cases[i].mo, cases[i].d, cases[i].h, cases[i].mi,
              cases[i].s, y, mo, d, h, mi, s);
    }
}

static void t_build_time(void)
{
    /* __DATE__ pads the day with a space, which is the trap here. */
    int64_t a = wc_build_time_ms("Sep  4 2026", "07:31:22");
    CHECK(a == wc_to_ms(2026, 9, 4, 7, 31, 22), "space-padded day");

    int64_t b = wc_build_time_ms("Dec 25 2026", "23:59:01");
    CHECK(b == wc_to_ms(2026, 12, 25, 23, 59, 1), "two-digit day");

    int64_t c = wc_build_time_ms("Jan  1 2027", "00:00:00");
    CHECK(c == wc_to_ms(2027, 1, 1, 0, 0, 0), "new year");

    CHECK(wc_build_time_ms(__DATE__, __TIME__) > wc_to_ms(2024, 1, 1, 0, 0, 0),
          "the real build time is after 2024");
}

int main(void)
{
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "epoch",              t_epoch },
        { "known dates",        t_known_dates },
        { "civil round-trip",   t_roundtrip },
        { "ms round-trip",      t_ms_roundtrip },
        { "__DATE__ parsing",   t_build_time },
    };
    for (unsigned i = 0; i < sizeof tests / sizeof *tests; i++) {
        int before = fails;
        tests[i].fn();
        printf("%s  %s\n", fails == before ? "pass" : "FAIL", tests[i].name);
    }
    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
