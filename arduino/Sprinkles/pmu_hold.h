/* AXP2101 register values that keep this board alive on the cell.
 *
 * The Waveshare board has no battery thermistor. With the TS pin left as a
 * temperature sensor, pulling USB switches the chip to battery and a floating
 * TS pin looks out of range, so it stops the power stage. Charge-done also
 * opens the battery path, and the power-off voltage can ship from the factory
 * as high as 3.3 V, which a 3.7 V cell hits during that handover.
 *
 * These bits are written on the bus. Library method names differ between
 * XPowersLib installs, and a skipped call leaves the old behaviour in place.
 */
#ifndef PMU_HOLD_H
#define PMU_HOLD_H

#include <stdint.h>

static inline uint8_t pmu_hold_apply(uint8_t addr, uint8_t cur)
{
    switch (addr) {
    case 0x14: return (uint8_t)(cur & 0x8F);          /* Vsys floor 4.1 V */
    case 0x16: return (uint8_t)((cur & 0xF8) | 0x04); /* USB input 1500 mA */
    case 0x18: return (uint8_t)((cur & 0xFE) | 0x02); /* watchdog off, charge on */
    case 0x1A: return (uint8_t)(cur & 0xF0);          /* gauge shutdown at 0% */
    case 0x23: return (uint8_t)(cur & 0xE0);          /* a dip must not latch off */
    case 0x24: return (uint8_t)(cur & 0xF8);          /* power off at 2.6 V */
    case 0x50: return (uint8_t)((cur & 0xE0) | 0x10); /* TS is not a thermistor */
    case 0x54: return 0xFF;                           /* charge cold limit open */
    case 0x55: return 0x00;                           /* charge hot limit open */
    case 0x56: return 0xFF;                           /* cold limit out of reach */
    case 0x57: return 0x00;                           /* hot limit out of reach */
    case 0x58: return (uint8_t)(cur & 0xFE);          /* no JEITA without a sensor */
    case 0x61: return (uint8_t)((cur & 0xF0) | 0x02); /* precharge 50 mA */
    case 0x62: return (uint8_t)((cur & 0xE0) | 0x08); /* constant 200 mA */
    case 0x63: return (uint8_t)(cur & 0xEF);          /* do not open the cell */
    case 0x64: return (uint8_t)((cur & 0xF8) | 0x03); /* stop at 4.2 V */
    case 0x67: return (uint8_t)((cur & 0xFB) | 0x40); /* precharge timer off */
    default:   return cur;
    }
}

/* Battery detection is chosen at boot. On lets the chip see the cell.
 * Off forces "always present" when that check refuses to start the charger. */
static inline uint8_t pmu_detect_set(uint8_t cur, int on)
{
    return on ? (uint8_t)(cur | 0x01) : (uint8_t)(cur & 0xFE);
}

/* reg 0x01: direction bits 6:5, state bits 2:0. Trickle through constant
 * voltage count as charging. "Done" and "not charging" do not. */
static inline int pmu_charge_active(int stat1)
{
    if (stat1 < 0) return 0;
    if (((stat1 >> 5) & 3) == 1) return 1;
    return (stat1 & 7) <= 3;
}

/* The board makes 3.3 V for the computer from the cell. Under about 3.6 V
 * the rail collapses as soon as the cable is pulled, which looks like a
 * dead battery. Under 2.0 V the pin is empty, not a flat cell. */
#define PMU_CELL_PRESENT_MV 2000
#define PMU_CELL_READY_MV   3600

/* 0: nothing on the connector. 1: connected, too flat to run. 2: ready. */
static inline int pmu_cell_level(int mv)
{
    if (mv < PMU_CELL_PRESENT_MV) return 0;
    if (mv < PMU_CELL_READY_MV) return 1;
    return 2;
}

/* reg 0x01 bits 2:0. Names a parent can read on the boot line. */
static inline const char *pmu_chg_name(int stat1)
{
    if (stat1 < 0) return "unread";
    switch (stat1 & 7) {
    case 0: return "trickle";
    case 1: return "precharge";
    case 2: return "charging";
    case 3: return "topping";
    case 4: return "full";
    case 5: return "not charging";
    default: return "unknown";
    }
}

#endif
