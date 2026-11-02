## Makefile
## Summary: Builds route using precompiled target-matched libhttp headers and archives.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/lgpl-3.0.html

KCDIST_DIR ?=
BIN_DIR := bin
BUILD_DIR := .build
BUILD_VERSION ?= $(shell date +%s)
OSXCROSS_ROOT ?= $(HOME)/.local/share/osxcross/target

NATIVE_SYSTEM := $(shell uname -s)
NATIVE_ARCH := $(shell uname -m)
ifeq ($(NATIVE_ARCH),arm64)
NATIVE_ARCH := aarch64
endif
NATIVE_PLATFORM := linux
NATIVE_EXT := so
ifeq ($(NATIVE_SYSTEM),Darwin)
NATIVE_PLATFORM := macos
NATIVE_EXT := dylib
endif
NATIVE_TARGET := $(NATIVE_PLATFORM)/$(NATIVE_ARCH)

LINUX_TARGETS := linux/x86_64 linux/aarch64
MACOS_TARGETS := macos/x86_64 macos/aarch64
SUPPORTED_TARGETS := $(LINUX_TARGETS) $(MACOS_TARGETS)

.DEFAULT_GOAL := $(NATIVE_TARGET)
.PHONY: all test clean check-deps build-target $(SUPPORTED_TARGETS)

all: $(SUPPORTED_TARGETS)

linux/x86_64:
	@$(MAKE) -s --no-print-directory build-target TARGET=$@ \
		TARGET_CC=gcc TARGET_AR=ar TARGET_EXT=so \
		TARGET_SHARED="-shared" TARGET_CFLAGS="-fPIC"

linux/aarch64:
	@$(MAKE) -s --no-print-directory build-target TARGET=$@ \
		TARGET_CC=aarch64-linux-gnu-gcc TARGET_AR=aarch64-linux-gnu-ar \
		TARGET_EXT=so TARGET_SHARED="-shared" TARGET_CFLAGS="-fPIC"

macos/x86_64:
	@$(MAKE) -s --no-print-directory build-target TARGET=$@ \
		TARGET_CC="$(OSXCROSS_ROOT)/bin/o64-clang" \
		TARGET_AR="$(shell ls "$(OSXCROSS_ROOT)"/bin/x86_64-apple-darwin*-ar 2>/dev/null | sort -V | tail -n 1)" \
		TARGET_EXT=dylib TARGET_SHARED="-dynamiclib" TARGET_CFLAGS="-fPIC"

macos/aarch64:
	@$(MAKE) -s --no-print-directory build-target TARGET=$@ \
		TARGET_CC="$(OSXCROSS_ROOT)/bin/oa64-clang" \
		TARGET_AR="$(shell ls "$(OSXCROSS_ROOT)"/bin/aarch64-apple-darwin*-ar 2>/dev/null | sort -V | tail -n 1)" \
		TARGET_EXT=dylib TARGET_SHARED="-dynamiclib" TARGET_CFLAGS="-fPIC"

DEP_DIR = $(KCDIST_DIR)/libhttp.c/$(TARGET)
DEP_HEADER = $(DEP_DIR)/libhttp.h
DEP_LIB = $(DEP_DIR)/libhttp.a
OUT_DIR = $(BIN_DIR)/$(TARGET)
OUT_OBJECT = $(BUILD_DIR)/$(TARGET)/libroute.o
OUT_STATIC = $(OUT_DIR)/libroute.a
OUT_SHARED = $(OUT_DIR)/libroute.$(TARGET_EXT)
OUT_CLI = $(OUT_DIR)/route

check-deps:
	@test -n "$(KCDIST_DIR)" || { echo "route: set KCDIST_DIR to the precompiled kclib distribution" >&2; exit 1; }
	@command -v "$(TARGET_CC)" >/dev/null 2>&1 || { echo "route: missing compiler $(TARGET_CC)" >&2; exit 1; }
	@command -v "$(TARGET_AR)" >/dev/null 2>&1 || { echo "route: missing archiver $(TARGET_AR)" >&2; exit 1; }
	@test -f "$(DEP_HEADER)" || { echo "route: missing dependency header $(DEP_HEADER)" >&2; exit 1; }
	@test -f "$(DEP_LIB)" || { echo "route: missing dependency archive $(DEP_LIB)" >&2; exit 1; }

build-target: check-deps
	@mkdir -p "$(OUT_DIR)"
	@$(MAKE) -s --no-print-directory "$(OUT_STATIC)" "$(OUT_SHARED)" "$(OUT_CLI)" \
		TARGET="$(TARGET)" TARGET_CC="$(TARGET_CC)" TARGET_AR="$(TARGET_AR)" \
		TARGET_EXT="$(TARGET_EXT)" TARGET_SHARED="$(TARGET_SHARED)" \
		TARGET_CFLAGS="$(TARGET_CFLAGS)" KCDIST_DIR="$(KCDIST_DIR)" \
		BUILD_VERSION="$(BUILD_VERSION)"
	@echo "OK $(TARGET)"

$(OUT_OBJECT): src/libroute.c src/libroute.h Makefile $(DEP_HEADER)
	@mkdir -p "$(dir $@)"
	@$(TARGET_CC) -std=c11 -O2 -Wall -Wextra -Werror $(TARGET_CFLAGS) \
		-DKC_ROUTE_BUILD_VERSION=$(BUILD_VERSION) -Isrc -I"$(DEP_DIR)" \
		-c -o "$@" src/libroute.c

$(OUT_STATIC): $(OUT_OBJECT)
	@mkdir -p "$(OUT_DIR)"
	@$(TARGET_AR) rcs "$@" "$(OUT_OBJECT)"

$(OUT_SHARED): $(OUT_OBJECT)
	@mkdir -p "$(OUT_DIR)"
	@$(TARGET_CC) $(TARGET_SHARED) -o "$@" "$(OUT_OBJECT)"

$(OUT_CLI): src/route.c $(OUT_STATIC)
	@mkdir -p "$(BUILD_DIR)/$(TARGET)"
	@$(TARGET_CC) -std=c11 -O2 -Wall -Wextra -Werror -Isrc -I"$(DEP_DIR)" \
		-c -o "$(BUILD_DIR)/$(TARGET)/route.o" src/route.c
	@$(TARGET_CC) -o "$@" "$(BUILD_DIR)/$(TARGET)/route.o" "$(OUT_STATIC)"

test:
	@$(MAKE) -s --no-print-directory "$(NATIVE_TARGET)" || exit 1
	@mkdir -p "$(BUILD_DIR)/test"
	@$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Isrc \
		-I"$(KCDIST_DIR)/libhttp.c/$(NATIVE_TARGET)" \
		-o "$(BUILD_DIR)/test/route-test" src/test.c \
		"$(BIN_DIR)/$(NATIVE_TARGET)/libroute.a" || exit 1
	@cp "$(BIN_DIR)/$(NATIVE_TARGET)/route" "$(BUILD_DIR)/test/route" || exit 1
	@(cd "$(BUILD_DIR)/test" && ./route-test)

clean:
	@rm -rf "$(BIN_DIR)" "$(BUILD_DIR)"
	@echo "OK clean"
