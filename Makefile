CC      ?= cc
CFLAGS  ?= -O2 -g
BUILD   := build

WARN := -std=c11 -Wall -Wextra -Wpedantic -Werror \
        -D_POSIX_C_SOURCE=200809L -Isrc
SAN  := -fsanitize=address,undefined -fno-omit-frame-pointer

# Raven is the GUI library: tools/get_raven.sh fetches and builds it.
RAVEN_DIR   ?= build/raven
RAVEN_BUILD ?= build/raven-build
RAVEN_LIB   ?= $(RAVEN_BUILD)/libraven.a
X11_LIBDIR  ?= build/sysroot/usr/lib64

CORE_SRC := $(wildcard src/core/*.c) src/infra/util.c src/infra/json.c
APP_SRC  := $(wildcard src/desktop/*.c) $(CORE_SRC)
TEST_SRC := $(wildcard tests/*.c)

.PHONY: all run test asan clean fmt

all: $(BUILD)/habits

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/habits: $(APP_SRC) | $(BUILD)
	@test -f $(RAVEN_LIB) || { \
		echo "Raven is not built: run ./tools/get_raven.sh first"; exit 1; }
	$(CC) $(WARN) $(CFLAGS) -I$(RAVEN_DIR)/include -o $@ $(APP_SRC) \
		$(RAVEN_LIB) -L$(X11_LIBDIR) -lX11 -lpthread -ldl -lm

$(BUILD)/tests: $(CORE_SRC) $(TEST_SRC) | $(BUILD)
	$(CC) $(WARN) $(CFLAGS) -o $@ $(CORE_SRC) $(TEST_SRC)

$(BUILD)/tests-asan: $(CORE_SRC) $(TEST_SRC) | $(BUILD)
	$(CC) $(WARN) $(CFLAGS) $(SAN) -o $@ $(CORE_SRC) $(TEST_SRC)

run: $(BUILD)/habits
	./$(BUILD)/habits

test: $(BUILD)/tests
	./$(BUILD)/tests

asan: $(BUILD)/tests-asan
	./$(BUILD)/tests-asan

fmt:
	clang-format -i $(wildcard src/*.c src/*/*.c src/*/*.h tests/*.c tests/*.h)

clean:
	rm -rf $(BUILD)
