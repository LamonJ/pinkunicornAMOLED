# Host-side build. No hardware, no ESP-IDF: this is milestones 2 and 3.
#   make test    state machine unit tests
#   make golden  render reference scenes and pixel-diff them
#   make         both
CC      = gcc
CFLAGS  = -O2 -std=c11 -Wall -Wextra -Isprinkles
LDLIBS  = -lm
CORE    = sprinkles/gfx.c sprinkles/pet.c sprinkles/sprites.c sprinkles/wallclock.c
GOLDEN ?= ../golden-frames

all: test test-app test-clock test-blit golden

test_pet: host/test_pet.c sprinkles/pet.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

host_golden: host/host_golden.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_clock: host/test_clock.c sprinkles/wallclock.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test-clock: test_clock
	./test_clock

test_blit: host/test_blit.c sprinkles/gfx.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test-blit: test_blit
	./test_blit

test_app: host/test_app.c host/board_host.c $(CORE) sprinkles/app.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_pmu: host/test_pmu_hold.c arduino_src/pmu_hold.h
	$(CC) $(CFLAGS) -o $@ host/test_pmu_hold.c

test_splash_png: host/test_splash_png.c arduino_src/splash_fit.c arduino_src/pngle.c arduino_src/miniz.c
	$(CC) $(CFLAGS) -Iarduino_src -o $@ host/test_splash_png.c arduino_src/splash_fit.c arduino_src/pngle.c arduino_src/miniz.c

test: test_pet test_pmu test_splash_png
	./test_pet
	./test_pmu
	./test_splash_png docs/splash-src.png /tmp/splash_card.ppm
	python3 host/check_splash_ppm.py docs/splash-src.png /tmp/splash_card.ppm

test-app: test_app
	./test_app

golden: host_golden
	@mkdir -p out
	./host_golden out
	python3 host/diff.py out $(GOLDEN)

round_preview: host/round_preview.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

round: round_preview
	@mkdir -p out
	./round_preview out

clean:
	rm -rf test_pet test_pmu test_splash_png test_app test_clock test_blit host_golden round_preview out diff.png

.PHONY: all test test-app test-clock test-blit golden round clean
