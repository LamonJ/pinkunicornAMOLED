#!/usr/bin/env python3
"""Generate the Arduino sketch folder from the shared sources.

One source of truth: the .c/.h files under sprinkles/ are used by the host
tests, the ESP-IDF build and Arduino alike. Arduino only compiles files in the
sketch root, so this flattens the includes rather than forking the code.

    python3 tools/make_arduino.py        ->  arduino/Sprinkles/
"""
import os, re, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT  = os.path.join(ROOT, "arduino", "Sprinkles")

SHARED = ["gfx.h", "gfx.c", "pet.h", "pet.c",
          "sprites.h", "sprites.c", "app.h", "app.c", "board.h",
          "wallclock.h", "wallclock.c"]
EXTRA  = [("arduino_src/Sprinkles.ino",     "Sprinkles.ino"),
          ("arduino_src/board_arduino.cpp", "board_arduino.cpp"),
          ("arduino_src/pmu_hold.h",        "pmu_hold.h"),
          ("arduino_src/splash_565.c",      "splash_565.c"),
          ("arduino_src/splash_565.h",      "splash_565.h"),
          # Espressif's ES8311 driver, Apache-2.0, verbatim from Waveshare's
          # 08_ES8311 example. Vendored so the sketch is self-contained.
          ("arduino_src/es8311.c",          "es8311.c"),
          ("arduino_src/es8311.h",          "es8311.h"),
          ("arduino_src/es8311_reg.h",      "es8311_reg.h")]
DOCS   = ["BRINGUP.md"]

def flatten(text):
    # "sprinkles/gfx.h" -> "gfx.h": Arduino gives you no subfolder include path
    return re.sub(r'#include\s+"sprinkles/([^"]+)"', r'#include "\1"', text)

def main():
    # pin_config.h is board wiring, not generated from sprinkles/. The sketch
    # folder is otherwise wiped and rewritten, so keep this file across a run.
    pin_path = os.path.join(OUT, "pin_config.h")
    pin = open(pin_path).read() if os.path.exists(pin_path) else None
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    if pin is not None:
        open(pin_path, "w").write(pin)
    for name in SHARED:
        src = os.path.join(ROOT, "sprinkles", name)
        open(os.path.join(OUT, name), "w").write(flatten(open(src).read()))
    for src, dst in EXTRA:
        open(os.path.join(OUT, dst), "w").write(
            flatten(open(os.path.join(ROOT, src)).read()))
    for d in DOCS:
        p = os.path.join(ROOT, d)
        if os.path.exists(p):
            shutil.copy(p, os.path.join(OUT, d))
    print("wrote %s (%d files)" % (os.path.relpath(OUT, ROOT),
                                   len(os.listdir(OUT))))
    print("open arduino/Sprinkles/Sprinkles.ino in the Arduino IDE")

if __name__ == "__main__":
    sys.exit(main())
