# Host-side build. No hardware, no ESP-IDF: this is milestones 2 and 3.
#   make test    state machine unit tests
#   make golden  render reference scenes and pixel-diff them
#   make         both
CC      = gcc
CFLAGS  = -O2 -std=c11 -Wall -Wextra -Isprinkles
LDLIBS  = -lm
CORE    = sprinkles/gfx.c sprinkles/pet.c sprinkles/sprites.c sprinkles/wallclock.c
GOLDEN ?= ../golden-frames

all: test test-app test-clock golden

test_pet: host/test_pet.c sprinkles/pet.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

host_golden: host/host_golden.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_clock: host/test_clock.c sprinkles/wallclock.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test-clock: test_clock
	./test_clock

test_app: host/test_app.c host/board_host.c $(CORE) sprinkles/app.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: test_pet
	./test_pet

test-app: test_app
	./test_app

golden: host_golden
	@mkdir -p out
	./host_golden out
	python3 host/diff.py out $(GOLDEN)

clean:
	rm -rf test_pet test_app test_clock host_golden out diff.png

.PHONY: all test test-app test-clock golden clean
