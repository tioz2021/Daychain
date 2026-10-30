CC      ?= cc
CFLAGS  ?= -O2 -g
BUILD   := build

WARN := -std=c11 -Wall -Wextra -Wpedantic -Werror \
        -D_POSIX_C_SOURCE=200809L -Isrc
SAN  := -fsanitize=address,undefined -fno-omit-frame-pointer

LIB_SRC  := $(wildcard src/infra/*.c) $(wildcard src/core/*.c) $(wildcard src/api/*.c)
TEST_SRC := $(wildcard tests/*.c)
WEB_FILES := $(shell find web -type f 2>/dev/null | sort)
WEB_BUNDLE := $(BUILD)/generated/web_bundle.c
APP_SRC  := src/main.c $(LIB_SRC) $(WEB_BUNDLE)

.PHONY: all run test asan clean fmt desktop

all: $(BUILD)/habits

$(BUILD):
	mkdir -p $(BUILD)

$(WEB_BUNDLE): $(WEB_FILES) tools/embed_web.sh
	@mkdir -p $(dir $@)
	sh tools/embed_web.sh $@ $(WEB_FILES)

# ---- native desktop client (optional, needs Raven) ----
RAVEN_DIR   ?= build/raven
RAVEN_BUILD ?= build/raven-build
RAVEN_LIB   ?= $(RAVEN_BUILD)/libraven.a
X11_LIBDIR  ?= build/sysroot/usr/lib64

DESKTOP_SRC  := $(wildcard src/desktop/*.c)
DESKTOP_CORE := $(wildcard src/core/*.c) src/infra/util.c src/infra/json.c

$(BUILD)/habits-desktop: $(DESKTOP_SRC) $(DESKTOP_CORE) $(RAVEN_LIB) | $(BUILD)
	@test -f $(RAVEN_LIB) || { echo "Raven is not built yet: run tools/get_raven.sh first"; exit 1; }
	$(CC) $(WARN) $(CFLAGS) -I$(RAVEN_DIR)/include -o $@ $(DESKTOP_SRC) $(DESKTOP_CORE) \
		$(RAVEN_LIB) -L$(X11_LIBDIR) -lX11 -lpthread -ldl -lm

desktop: $(BUILD)/habits-desktop

$(BUILD)/habits: $(APP_SRC) | $(BUILD)
	$(CC) $(WARN) $(CFLAGS) -o $@ $(APP_SRC)

$(BUILD)/tests: $(LIB_SRC) $(TEST_SRC) $(WEB_BUNDLE) | $(BUILD)
	$(CC) $(WARN) $(CFLAGS) -o $@ $(LIB_SRC) $(TEST_SRC) $(WEB_BUNDLE)

$(BUILD)/tests-asan: $(LIB_SRC) $(TEST_SRC) $(WEB_BUNDLE) | $(BUILD)
	$(CC) $(WARN) $(CFLAGS) $(SAN) -o $@ $(LIB_SRC) $(TEST_SRC) $(WEB_BUNDLE)

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
