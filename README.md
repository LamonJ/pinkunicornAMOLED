# Sprinkles — C core

Milestones 2 and 3 of `sprinkles-port-spec.md`, done and verified.

## What's here

    sprinkles/gfx.[ch]        64x64 pixel world + primitives
    sprinkles/pet.[ch]        state machine, no hardware
    sprinkles/sprites.[ch]    the unicorn cat and every prop
    sprinkles/app.[ch]        interaction layer: animation scheduling,
                              triggers, menu, touch, frame composition
    sprinkles/board.h         the ONLY hardware interface
    sprinkles/board_esp32.c   its ESP-IDF implementation (TODO(bsp) holes)
    host/board_host.c         a board that isn't there, for testing
    host/test_pet.c           42 checks on the state machine
    host/test_app.c           86 checks on the interaction layer
    host/host_golden.c        renders reference scenes to PPM
    host/diff.py              pixel-diffs them against golden-frames/
    main/main.c               the loop

`BRINGUP.md` has the ordered checklist for the hardware holes.

## Verify before you flash

Put `golden-frames/` next to this directory, then:

    make

- `make test` — 42 checks on the state machine
- `make test-app` — 86 checks on the interaction layer
- `make golden` — renders 28 scenes and diffs them against the reference
  frames **pixel for pixel**. Exit code is non-zero on any difference, and it
  writes `diff.png` (gold | ours | diff) for anything that fails.

All 28 currently match exactly. If you touch `gfx.c` or `sprites.c` and this
goes red, you have introduced a rounding bug — `diff.png` will show you where.

## On device — three ways, same sources

The `.c` files under `sprinkles/` are shared by all three. There is no fork.

### PlatformIO (recommended)

Install the "PlatformIO IDE" extension in VS Code, open this folder, Build.
It installs its own toolchain into the project and has none of the ESP-IDF
extension's self-repair behaviour.

    pio run -t upload
    pio device monitor

### Arduino IDE

    python3 tools/make_arduino.py

Writes `arduino/Sprinkles/` with the includes flattened, because Arduino only
compiles files in the sketch root. Open `Sprinkles.ino` and Upload.

Board: **ESP32S3 Dev Module**. Settings that matter:

| Setting | Value |
|---|---|
| Flash Size | 16MB (128Mb) |
| PSRAM | **OPI PSRAM** — required, the frame buffer needs it |
| USB CDC On Boot | Enabled, so `Serial` works |

Re-run the generator after editing anything under `sprinkles/`.

### ESP-IDF

    idf.py set-target esp32s3
    idf.py add-dependency "waveshare/esp32_s3_touch_amoled_1_75"
    idf.py build flash monitor

## Animations

All ported and verified: idle, nap, sad, eat, love, petting, dance, the front
flip, peekaboo and the five-beat transformation. **All 28 reference frames
match pixel for pixel.**

The flip renders her flat into `gfx_alt`, then `gfx_rot_blit()` maps each
destination pixel centre back through the inverse rotation and floors it —
which is what canvas does with smoothing off. 4096 samples per frame.

## Architecture

    main.c        the loop: input, tick, draw, persist
      |
    app.c         WHEN things happen - animation scheduling, cooldowns,
      |           triggers, menu rules, touch routing
    pet.c         WHAT she is - needs, stages, moods
    sprites.c     WHAT she looks like
    gfx.c         pixels
      |
    board.h       the only hardware interface

Nothing above `board.h` includes an `esp_*` header. If something does, it has
leaked, and the host tests will stop building — which is the point.

`app_tick` draws all four of its random values up front rather than inside
branches, so behaviour doesn't depend on control flow. That was not
aesthetics: with a variable number of draws per tick, the trigger conditions
were untestable.

## Hardware status

All implemented in `arduino_src/board_arduino.cpp`:

| Peripheral | How |
|---|---|
| Display | Arduino_GFX, `Arduino_ESP32QSPI` + `Arduino_CO5300` |
| Touch | SensorLib `TouchDrvCST92xx`, mirrored X and Y |
| Audio | `ESP_I2S` + Espressif's ES8311 driver (vendored), PA on GPIO46 |
| RTC | SensorLib `SensorPCF85063`, seeded from `__DATE__` if never set |
| IMU | SensorLib `SensorQMI8658`, pickup by acceleration delta |
| BOOT button | GPIO0, active low |
| PWR button | TCA9554 P4 at 0x20, **active high** |
| Sleep | light sleep, wake on touch/BOOT, 2 s poll for pickup |
| Persistence | Preferences (NVS) |
| Battery | AXP2101 via XPowersLib, read-only |
| Grown-up screen | both buttons held 5 s, native-resolution text |

**Libraries to install:** *GFX Library for Arduino*, *SensorLib* and *XPowersLib*.
`pin_config.h` must be copied in from
`examples/arduino/libraries/Mylibrary/`.

`board_esp32.c` is the ESP-IDF variant and still has its `TODO(bsp)` holes —
the Arduino path is the maintained one.

## Three bugs the golden test caught

Worth reading before you touch the drawing code, because all three are the
kind that ship as "looks slightly off" and are miserable to find later.

1. **Ear geometry** — three transcription errors in one block (0.62 vs 0.55,
   wrong vertical anchor, height vs height+2). The blob matched perfectly and
   every eared stage failed, which pointed straight at it.
2. **Ray sampling** — the reference starts its radius loop at a *fractional*
   value and steps by 1. Truncating to int first shifts every sample along the
   spoke. Cost: 21 pixels in one transformation frame.
3. **Per-pixel truncation** — the reference truncates each plotted coordinate
   with `x|0`, not the sprite's origin. For negative coordinates those differ,
   because truncation is toward zero rather than floor. This only shows up
   when an effect drifts off the left edge, and it was worth exactly one pixel
   in one frame. `gfx_map*` therefore take `double` origins on purpose — do
   not "tidy" them to int.
