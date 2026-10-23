/* AXP2101 values for this board, lined up with PrintSphere.
 *
 * PrintSphere runs the same Waveshare 1.75 AMOLED from the cell by leaving
 * the charger at its normal settings: TS pin off, 50 mA precharge, a modest
 * constant current, termination left on, stop at 4.2 V. It does not rewrite
 * the power chip on a timer.
 *
 * Earlier Sprinkles flashes wrote other bytes, and the chip keeps them
 * while USB power stays up across an upload. This puts those bytes back
 * once. It does not touch the battery-path bit (0x12), the power-off timer
 * (0x27), or the DC-DC rails. A cell already above 4.2 V stays idle.
 */
#ifndef PMU_HOLD_H
#define PMU_HOLD_H

#include <stdint.h>

/* cur is the byte in the chip right now. The result is what it should be
 * after the one-time undo. Registers this does not name come back unchanged. */
static inline uint8_t pmu_restore_apply(uint8_t addr, uint8_t cur)
{
    switch (addr) {
    case 0x14: return (uint8_t)((cur & 0x8F) | 0x60); /* Vsys floor 4.7 V */
    case 0x1A: return (uint8_t)((cur & 0xF0) | 0x01); /* gauge shutdown nibble */
    case 0x23: return (uint8_t)((cur & 0xE0) | 0x1F); /* undervoltage window */
    case 0x54: return 0x29;                           /* charge cold limit */
    case 0x55: return 0x58;                           /* charge hot limit */
    case 0x56: return 0x3E;                           /* cold limit */
    case 0x57: return 0x4C;                           /* hot limit */
    case 0x63: return (uint8_t)((cur & 0xF0) | 0x11); /* termination on, 25 mA */
    case 0x67: return (uint8_t)(cur | 0x04);          /* precharge timer bit */
    case 0x68: return (uint8_t)(cur | 0x01);          /* battery detection on */
    default:   return cur;                            /* 0x12, 0x24, 0x27, ... */
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

/* reg 0x00 bit 4. On this board the bit was set during a topping charge
 * and clear while the charger sat idle, so 1 means the path is conducting. */
static inline int pmu_switch_on(int stat0)
{
    return stat0 >= 0 && (stat0 & 0x10) != 0;
}

/* The charger sits still once the cell is already at the top. A first
 * sample under 3600 mV can be the meter before it settles; the repeating
 * cell= line is the one to trust. Under 2000 mV the connector is empty. */
static inline const char *pmu_when_idle(int cell_mv)
{
    if (pmu_cell_level(cell_mv) >= 2)
        return "the cell is full, so the charger is idle.";
    if (pmu_cell_level(cell_mv) == 1)
        return "charger is idle. Trust the cell= line. Leave USB in if it stays under 3600.";
    return "red wire goes on the + mark.";
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
