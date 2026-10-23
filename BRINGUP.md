# Bring-up

> **Status: everything below is implemented.** Display, touch, audio, RTC,
> IMU, both buttons, light sleep and persistence are all wired in
> `arduino_src/board_arduino.cpp`. The notes below are kept as the record of
> what each peripheral needs and how to tell it apart when something breaks.
>
> Facts worth not re-deriving:
> - **BOOT is the LOWER button** (GPIO0, pulled up, reads low when pressed).
> - **PWR is the UPPER button**, readable on **TCA9554 P4 / EXIO4 at 0x20**,
>   and it reads **HIGH while pressed** — opposite polarity to BOOT. No
>   AXP2101 transaction needed.
> - **PA on GPIO46 is the amplifier enable.** Silence without it, however
>   correct the I2S and codec are.
> - Touch needs `setMaxCoordinates()` **and `setMirrorXY(true, true)`** or
>   presses land on the wrong side of the screen.
> - I2S opens as **stereo, both slots** — mono samples play at double speed.
> - Neither PWR nor the IMU can wake the chip from sleep: PWR lives behind
>   the expander and QMI_INT1 is on expander P6. Only touch (GPIO11) and BOOT
>   (GPIO0) are real wake pins.
> - **Wake pads need RTC-domain pull-ups.** `pinMode(..., INPUT_PULLUP)`
>   lives in the digital domain, which light sleep powers down - the pads
>   float and the wake becomes unreliable. Use `rtc_gpio_pullup_en()`.
> - The touch interrupt alone still misses taps, so the 500 ms timer wake
>   also POLLS touch, BOOT, PWR and the IMU. Belt and braces; a toy you have
>   to fight to wake is a broken toy.
> - **Battery** is AXP2101 at 0x34. Charging is 200 mA to 4.2 V. There is no
>   thermistor, so the TS pin must not be allowed to stop the power stage when
>   USB is unplugged. Those bits are written on the bus (see `pmu_hold.h`),
>   including a 2.6 V power-off floor and charge-done leaving the cell
>   connected. A cell under 3600 mV is seen but too empty to hold the 3.3 V
>   rails; leave USB in until the boot line passes that. A flat cell can
>   latch the charger off (safety timer or the missing thermistor); boot
>   clears that latch and prints vbus and status bytes. USB powers a fully
>   off board on. Holding PWR about 6 seconds powers it off at the chip.
>   Red wire to the + mark.
> - **There is no clock-driven bedtime.** She sleeps only when napped or when
>   out of energy. An earlier 20:00-07:00 window made every button look dead
>   for anyone who flashed after 8pm, because the next tick re-slept her.

Everything above `board_esp32.c` is verified on a PC: 128 unit checks and 28
pixel-exact reference frames. So from here on, **if something misbehaves it is
in `board_esp32.c`**. That is the whole point of the structure.

Fill the `TODO(bsp)` holes in this order. Each one is independently
observable, and none of them can break the ones before it.

    idf.py set-target esp32s3
    idf.py add-dependency "waveshare/esp32_s3_touch_amoled_1_75"
    idf.py build flash monitor

Keep the factory recovery image from Waveshare's Firmware Releases to hand
before you flash anything.

---

## 1. `board_panel_push` + `board_brightness`

**Do:** wire to `esp_lcd_panel_draw_bitmap` from the BSP. Set brightness to 35.

**Expect:** Sprinkles on screen, breathing, blinking every few seconds, tail
swishing. The 64×64 world is scaled to 448 px and centred on the 466 circle
of the Waveshare 1.75" panel. The gap between that square and the round
bezel is the same background colour, and pixels outside the circle stay off,
so the glass shows a round scene rather than a square inset. The 9 px of
slack is still there: it is what the burn-in drift moves through.

**If she's the wrong size or offset:** the blit is fine (it's tested), so it's
the panel's own origin or a rotation setting in the BSP init.

**If colours are wrong:** check the RGB565 byte order. `gfx_blit565` packs
`RRRRRGGG GGGBBBBB`; some panels want the two bytes swapped. That is a
one-line change in the blit, not in the sprites.

**Gate:** leave it running for ten minutes and watch her drift a pixel or two.
If she never moves, `gfx_drift` isn't being applied.

---

## 2. `board_button_pressed`

**Do:** BOOT on GPIO0 is already implemented. Add PWR via EXIO4 through the
expander. Waveshare's button example does exactly this.

**Expect:** BOOT summons the two big buttons; pressing it again dismisses
them. PWR sends her for a visible nap — dimmed, breathing, Z's, energy bar
centred. Either button wakes her.

**CONFIRMED ON HARDWARE:** BOOT is the **lower** side button; PWR is the
upper one. (The product render suggested the opposite — trust the board.)

**PWR held for ~6 s powers the board off at the PMIC.** Not maskable, which
is why the child's frequent action lives on BOOT.

**Interim:** BOOT alone covers both actions — tap for the menu, hold half a
second for the nap. Once the expander read works, move the nap to PWR and
drop the hold.

**Gate:** the menu must stay open indefinitely. If it vanishes after a minute,
`app_tick`'s idle handling is being bypassed.

---

## 3. `board_touch`

**Do:** read the CST9217. Start from `examples/arduino/10_Touch_CST9217/`
in Waveshare's repo — that example exists specifically for this chip.

Use the first contact point only: it reports two, and small hands rest a palm
on the glass, which must not read as a second tap.

Pins are `TP_INT 11` and `TP_RESET 40` from `pin_config.h`.

The world-coordinate conversion is already written; it needs `board_drift_x/y`,
which `main.c` sets every frame. Don't skip that or taps land a couple of
pixels off.

**Expect:** tapping her makes her jump with sparkles, and lands a heart at
most once every ten seconds. With the menu open, the cupcake feeds her and the
broom sweeps, and the menu closes so you can watch her react.

**Gate:** hand it to your kid and see whether she can reliably hit an 18 mm
target. **This is the single measurement the whole control design rests on**
and it cannot be answered from a desk. If she can't, the fallback is an I2C
GPIO expander on the bus that already exists, giving physical buttons without
consuming a pin.

---

## 4. `board_rtc_get` / `board_rtc_set`

**Do:** read the PCF85063. Return **local** time — the only consumer is the
bedtime window, and a five-year-old does not care about time zones. Return
`false` if the clock has never been set; `app.c` treats that as a newborn
rather than inventing a date.

**Expect:** she ages while powered off. `main.c` re-bases the saved timestamps
onto the boot clock and lets `pet_tick` catch up.

**Gate, and do actually run it:** note her stage and meters, pull power for an
hour, boot again. She should be exactly as hungry as an hour would make her.
The catch-up correctness is unit-tested (`catch-up == stepwise`), so a
discrepancy here means the timestamp re-basing in `load_state`, not the maths.

---

## 5. `board_load` / `board_save`

Already implemented against NVS. Nothing to do unless you change `pet_t`, in
which case **bump `SAVE_VERSION` in main.c** so old blobs are discarded rather
than misread.

**Gate:** feed her, reboot, confirm she remembers.

---

## 6. `board_picked_up`

**Do:** read the QMI8658 and trigger on a jump in acceleration magnitude. The
prototype used a delta of about 2.2 m/s² between consecutive reads, which felt
right on a phone. Expect to retune once she's in a case with a battery.

**Expect:** the panel is off, you lift the device, and she wakes and greets
you with a hop and sparkles.

This is the nicest thing on the device and worth the tuning time. It is also
what turns the battery limitation into the actual charm: she can't be
always-on, so instead she notices you.

---

## 7. `board_beep`

The waveform generator is written — square waves with an exponential decay,
matching the prototype's envelope. Only the I2S write is missing.

**Do:** `i2s_channel_write` to the ES8311. Blocking is fine; the longest
sequence is under a second and only runs on a press.

**Remember there is no speaker on the board.** Develop against the bundled
external one. If nothing fits in the case alongside the battery, ship silent —
`board_audio_enable(false)` and nothing else changes.

**Gate:** feeding a *hungry* cat plays five rising notes; feeding a full one
plays three. If those sound the same, the note sequences aren't being walked.

---

## 8. `board_sleep_until_touched`

**Do:** turn the panel off, enable the touch and IMU interrupt lines as EXT1
wake sources, `esp_light_sleep_start()`.

Light sleep rather than deep: she keeps her RAM, so waking is instant and
feels like she noticed you rather than like a device booting.

**Expect:** after a minute untouched, the screen goes black except one gold
pixel breathing at her horn. Touch or lift wakes her.

**Gate:** measure current draw. If it's still too high, deep sleep is the
bigger win, and `pet_tick` already handles arbitrarily long catch-ups
correctly.

---

## Then: the physical build

Not code, and the part I'd take slowest.

- Protected pouch cell, mechanically secured so it cannot be poked or
  punctured, no accessible leads. The supplied case is basic isolation, not a
  toy enclosure.
- Fit the battery **first**, then measure what's left, then buy a speaker to
  fit the hole — checking the connector pitch and the amplifier's rated
  impedance in the schematic before ordering.
- A sealed case and a working battery are worth more than beeps.

---

## Regression

Run `make` before every commit. Three suites: 42 checks on the state machine,
86 on the interaction layer, 28 frames pixel-diffed. If the frames go red you
have introduced a rounding bug, and `diff.png` shows you where.
