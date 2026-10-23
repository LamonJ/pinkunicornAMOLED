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
    case 0x56: return 0xFF;                           /* cold limit out of reach */
    case 0x57: return 0x00;                           /* hot limit out of reach */
    case 0x63: return (uint8_t)(cur & 0xEF);          /* do not open the cell */
    case 0x68: return (uint8_t)(cur & 0xFE);          /* cell counts as present */
    default:   return cur;
    }
}

#endif
