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
    CHECK(pmu_hold_apply(0x54, 0x29) == 0xFF, "charge cold limit opened");
    CHECK(pmu_hold_apply(0x55, 0x58) == 0x00, "charge hot limit opened");
    CHECK(pmu_hold_apply(0x58, 0x01) == 0x00, "JEITA off");
    CHECK(pmu_hold_apply(0x61, 0x05) == 0x02, "precharge becomes 50 mA");
    CHECK(pmu_hold_apply(0x62, 0x1F) == 0x08, "fast charge becomes 200 mA");
    CHECK(pmu_hold_apply(0x64, 0x00) == 0x03, "target becomes 4.2 V");
    CHECK(pmu_hold_apply(0x67, 0xFF) == 0xFB, "precharge timer off, other bits stay");
    CHECK(pmu_detect_set(0x00, 1) == 0x01, "detection can be turned on");
    CHECK(pmu_detect_set(0x01, 0) == 0x00, "detection can be forced present");
    CHECK(pmu_charge_active(0x05) == 0, "not charging is idle");
    CHECK(pmu_charge_active(0x01) == 1, "precharge is charging");
    CHECK(pmu_charge_active(0x22) == 1, "constant current is charging");
    CHECK(pmu_charge_active(0x04) == 0, "charge done is not current");
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
        0x14, 0x16, 0x18, 0x1A, 0x23, 0x24, 0x50,
        0x54, 0x55, 0x56, 0x57, 0x58, 0x61, 0x62, 0x63, 0x64, 0x67, 0x68
    };
    for (unsigned i = 0; i < sizeof addrs; i++) {
        same_twice(addrs[i], 0x00);
        same_twice(addrs[i], 0xFF);
        same_twice(addrs[i], 0xA5);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
