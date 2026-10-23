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
    uint8_t once = pmu_restore_apply(addr, cur);
    uint8_t twice = pmu_restore_apply(addr, once);
    CHECK(once == twice, "0x%02X not stable: %02X then %02X", addr, once, twice);
}

int main(void)
{
    /* Bytes an earlier flash changed, put back to what the chip had before. */
    CHECK(pmu_restore_apply(0x14, 0x05) == 0x65, "4.1 V floor back to 4.7 V");
    CHECK(pmu_restore_apply(0x14, 0x65) == 0x65, "4.7 V floor stays");
    CHECK(pmu_restore_apply(0x1A, 0xA0) == 0xA1, "shutdown nibble restored");
    CHECK(pmu_restore_apply(0x23, 0x20) == 0x3F, "undervoltage window restored");
    CHECK(pmu_restore_apply(0x54, 0xFF) == 0x29, "charge cold limit restored");
    CHECK(pmu_restore_apply(0x55, 0x00) == 0x58, "charge hot limit restored");
    CHECK(pmu_restore_apply(0x56, 0xFF) == 0x3E, "cold limit restored");
    CHECK(pmu_restore_apply(0x57, 0x00) == 0x4C, "hot limit restored");
    CHECK(pmu_restore_apply(0x63, 0x01) == 0x11, "termination back on at 25 mA");
    CHECK(pmu_restore_apply(0x63, 0x11) == 0x11, "termination stays on");
    CHECK(pmu_restore_apply(0x67, 0xE2) == 0xE6, "precharge timer bit restored");
    CHECK(pmu_restore_apply(0x68, 0x00) == 0x01, "battery detection on");
    CHECK(pmu_restore_apply(0x68, 0x01) == 0x01, "battery detection stays on");

    /* Left to the library, or not our business. */
    CHECK(pmu_restore_apply(0x12, 0x00) == 0x00, "battery switch is not rewritten");
    CHECK(pmu_restore_apply(0x12, 0x08) == 0x08, "battery switch left as it is");
    CHECK(pmu_restore_apply(0x27, 0x1B) == 0x1B, "power-off timer untouched");
    CHECK(pmu_restore_apply(0x24, 0x00) == 0x00, "power-off voltage left alone");
    CHECK(pmu_restore_apply(0x16, 0x04) == 0x04, "input limit left alone");
    CHECK(pmu_restore_apply(0x18, 0x0A) == 0x0A, "charge enable left alone");
    CHECK(pmu_restore_apply(0x50, 0x10) == 0x10, "TS setting left to the library");
    CHECK(pmu_restore_apply(0x58, 0x00) == 0x00, "JEITA left alone");
    CHECK(pmu_restore_apply(0x61, 0x06) == 0x06, "precharge current left to the library");
    CHECK(pmu_restore_apply(0x62, 0x08) == 0x08, "200 mA left to the library");
    CHECK(pmu_restore_apply(0x64, 0x03) == 0x03, "4.2 V left to the library");
    CHECK(pmu_restore_apply(0x99, 0x5A) == 0x5A, "unknown register left alone");

    CHECK(pmu_switch_on(0x38) == 1, "topping sample has the path on");
    CHECK(pmu_switch_on(0x28) == 0, "idle sample has the path off");
    CHECK(pmu_detect_set(0x00, 1) == 0x01, "detection can be turned on");
    CHECK(pmu_detect_set(0x01, 0) == 0x00, "detection can be forced present");
    CHECK(pmu_charge_active(0x05) == 0, "not charging is idle");
    CHECK(pmu_charge_active(0x01) == 1, "precharge is charging");
    CHECK(pmu_charge_active(0x22) == 1, "constant current is charging");
    CHECK(pmu_charge_active(0x04) == 0, "charge done is not current");

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
    CHECK(strcmp(pmu_when_idle(4470), "the cell is full, so the charger is idle.") == 0,
          "a full cell idles the charger");
    CHECK(strcmp(pmu_when_idle(3493),
                 "charger is idle. Trust the cell= line. Leave USB in if it stays under 3600.") == 0,
          "a low first sample is not the verdict");
    CHECK(strcmp(pmu_when_idle(0), "red wire goes on the + mark.") == 0,
          "no cell still names the wire");

    static const uint8_t addrs[] = {
        0x12, 0x14, 0x16, 0x18, 0x1A, 0x23, 0x24, 0x27, 0x50,
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
