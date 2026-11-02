/* The power-button hold, with no board attached. */
#include "../sprinkles/pwr_sleep.h"
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; \
    printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static int step(pwr_hold_t *s, int64_t t, int pwr, int boot)
{
    return pwr_hold_step(s, t, pwr, boot, PWR_SLEEP_MS);
}

int main(void)
{
    pwr_hold_t s;
    memset(&s, 0, sizeof s);

    CHECK(pwr_hold_step(NULL, 0, 1, 0, PWR_SLEEP_MS) == 0, "null is quiet");

    /* Already high at boot, and it stays high. That is the chatter, not a press. */
    for (int64_t t = 0; t <= 10000; t += 66)
        CHECK(!step(&s, t, 1, 0), "stuck high at %lld", (long long)t);
    CHECK(!s.seen_up, "a stuck line has never been released");

    /* Seen up, then a real hold. 2999 ms waits. 3000 ms fires once. */
    memset(&s, 0, sizeof s);
    CHECK(!step(&s, 1000, 0, 0), "released arms the next hold");
    CHECK(s.seen_up, "armed");
    CHECK(!step(&s, 5000, 1, 0), "the press frame is not yet three seconds");
    CHECK(!step(&s, 5000 + PWR_SLEEP_MS - 1, 1, 0), "one millisecond early");
    CHECK(step(&s, 5000 + PWR_SLEEP_MS, 1, 0), "three seconds fires");
    CHECK(!step(&s, 5000 + PWR_SLEEP_MS + 500, 1, 0), "holding longer does not fire again");

    /* Let go, then hold again. */
    CHECK(!step(&s, 9000, 0, 0), "release clears it");
    CHECK(!step(&s, 10000, 1, 0), "the new press starts over");
    CHECK(step(&s, 10000 + PWR_SLEEP_MS, 1, 0), "the second hold fires");

    /* A blip in the middle starts the three seconds over. */
    memset(&s, 0, sizeof s);
    step(&s, 0, 0, 0);
    step(&s, 100, 1, 0);
    CHECK(!step(&s, 2100, 1, 0), "two seconds is not enough");
    CHECK(!step(&s, 2200, 0, 0), "a blip releases");
    CHECK(!step(&s, 2300, 1, 0), "down again");
    CHECK(!step(&s, 2300 + PWR_SLEEP_MS - 1, 1, 0), "the blip ate the first stretch");
    CHECK(step(&s, 2300 + PWR_SLEEP_MS, 1, 0), "three seconds from the new press");

    /* The lower button held with it is the grown-up screen. */
    memset(&s, 0, sizeof s);
    step(&s, 0, 0, 0);
    for (int64_t t = 100; t <= 8000; t += 100)
        CHECK(!step(&s, t, 1, 1), "both buttons at %lld", (long long)t);

    /* Lower button joins late, then leaves. The power count starts over. */
    memset(&s, 0, sizeof s);
    step(&s, 0, 0, 0);
    step(&s, 100, 1, 0);
    CHECK(!step(&s, 2000, 1, 1), "lower button cancels the count");
    CHECK(!step(&s, 2100, 1, 0), "power still down, count is new");
    CHECK(!step(&s, 2100 + PWR_SLEEP_MS - 1, 1, 0), "still waiting");
    CHECK(step(&s, 2100 + PWR_SLEEP_MS, 1, 0), "fires from the restart");

    /* Samples like the dark-screen poll, half a second apart. */
    memset(&s, 0, sizeof s);
    step(&s, 0, 0, 0);
    CHECK(!step(&s, 500, 1, 0), "poll 0");
    CHECK(!step(&s, 1000, 1, 0), "poll 1");
    CHECK(!step(&s, 1500, 1, 0), "poll 2");
    CHECK(!step(&s, 2000, 1, 0), "poll 3");
    CHECK(!step(&s, 2500, 1, 0), "poll 4");
    CHECK(!step(&s, 3000, 1, 0), "poll 5 is 2.5 s");
    CHECK(step(&s, 3500, 1, 0), "poll 6 is 3 s");

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
