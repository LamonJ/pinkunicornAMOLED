/* The battery-path register math, with no chip attached. */
#include "../arduino_src/pmu_hold.h"
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; \
    printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static void same_twice(uint8_t addr, uint8_t cur)
{
    uint8_t once = pmu_hold_apply(addr, cur);
    uint8_t twice = pmu_hold_apply(addr, once);
    CHECK(once == twice, "0x%02X not stable: %02X then %02X", addr, once, twice);
}

int main(void)
{
    /* A factory image with every shutdown armed and TS still a thermistor. */
    CHECK(pmu_hold_apply(0x24, 0x07) == 0x00, "3.3 V cutoff becomes 2.6 V");
    CHECK(pmu_hold_apply(0x16, 0x01) == 0x04, "500 mA input becomes 1500 mA");
    CHECK(pmu_hold_apply(0x14, 0x60) == 0x00, "4.7 V rail floor becomes 4.1 V");
    CHECK(pmu_hold_apply(0x63, 0x15) == 0x05, "termination off, 25 mA kept");
    CHECK(pmu_hold_apply(0x50, 0x0A) == 0x10, "TS stops being a thermistor");
    CHECK(pmu_hold_apply(0x23, 0x3F) == 0x20, "undervoltage latch off, overvoltage stays");
    CHECK(pmu_hold_apply(0x18, 0x0B) == 0x0A, "watchdog off, charge and gauge stay");
    CHECK(pmu_hold_apply(0x1A, 0xA1) == 0xA0, "shutdown percent cleared, warning kept");
    CHECK(pmu_hold_apply(0x68, 0x01) == 0x00, "detection off so the cell counts");
    CHECK(pmu_hold_apply(0x56, 0x3E) == 0xFF, "cold limit opened");
    CHECK(pmu_hold_apply(0x57, 0x4C) == 0x00, "hot limit opened");
    CHECK(pmu_hold_apply(0x99, 0x5A) == 0x5A, "unknown register left alone");

    /* 2923 mV is a real flat cell, not a missing wire. 3.3 V rails need
     * headroom, so "safe to unplug" starts at 3600 mV. */
    CHECK(pmu_cell_level(0) == 0, "0 mV is no cell");
    CHECK(pmu_cell_level(1999) == 0, "just under 2 V is no cell");
    CHECK(pmu_cell_level(2000) == 1, "2.0 V is a flat cell");
    CHECK(pmu_cell_level(2923) == 1, "2923 mV stays connected");
    CHECK(pmu_cell_level(3599) == 1, "just under 3.6 V is still too flat");
    CHECK(pmu_cell_level(3600) == 2, "3.6 V can hold the rails");
    CHECK(pmu_cell_level(4200) == 2, "a full cell is ready");
    CHECK(pmu_cell_level(-1) == 0, "a failed read is not a cell");
    CHECK(strcmp(pmu_chg_name(0x01), "precharge") == 0, "precharge code");
    CHECK(strcmp(pmu_chg_name(0x22), "charging") == 0, "constant-current code");
    CHECK(strcmp(pmu_chg_name(0x05), "not charging") == 0, "not-charging code");
    CHECK(strcmp(pmu_chg_name(-1), "unread") == 0, "failed status read");

    static const uint8_t addrs[] = {
        0x14, 0x16, 0x18, 0x1A, 0x23, 0x24, 0x50, 0x56, 0x57, 0x63, 0x68
    };
    for (unsigned i = 0; i < sizeof addrs; i++) {
        same_twice(addrs[i], 0x00);
        same_twice(addrs[i], 0xFF);
        same_twice(addrs[i], 0xA5);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
