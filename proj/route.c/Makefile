## Makefile
## Summary: Cross-compiles route using target-matched libhttp and libregex.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/lgpl-3.0.html

KCDIST_DIR ?=
BIN_DIR := bin
BUILD_DIR := .build
WINE ?= wine
WINE_X86_64_CC ?= x86_64-w64-mingw32-gcc
WASM_EMCC ?= emcc
WASM_EMAR ?= emar
WASM_NODE ?= node
BUILD_VERSION ?= $(shell date +%s)
OSXCROSS_ROOT ?= $(HOME)/.local/share/osxcross/target
OSXCROSS_X86_64_AR := $(shell ls "$(OSXCROSS_ROOT)"/bin/x86_64-apple-darwin*-ar 2>/dev/null | sort -V | tail -n 1)
OSXCROSS_AARCH64_AR := $(shell ls "$(OSXCROSS_ROOT)"/bin/aarch64-apple-darwin*-ar 2>/dev/null | sort -V | tail -n 1)
ANDROID_HOME ?= $(HOME)/.local/share/android-sdk
NDK_VERSION ?= 27.2.12479018
NDK_BIN ?= $(ANDROID_HOME)/ndk/$(NDK_VERSION)/toolchains/llvm/prebuilt/linux-x86_64/bin
ANDROID_API ?= 21

NATIVE_ARCH := $(shell uname -m)
NATIVE_SYSTEM := $(shell uname -s)
ifeq ($(NATIVE_ARCH),amd64)
NATIVE_ARCH := x86_64
endif
ifeq ($(NATIVE_ARCH),arm64)
NATIVE_ARCH := aarch64
endif
ifeq ($(NATIVE_ARCH),armv7l)
NATIVE_ARCH := armv7
endif
NATIVE_PLATFORM := linux
NATIVE_EXT := so
ifeq ($(NATIVE_SYSTEM),Darwin)
NATIVE_PLATFORM := macos
NATIVE_EXT := dylib
endif
ifneq ($(filter MINGW% MSYS% CYGWIN%,$(NATIVE_SYSTEM)),)
NATIVE_PLATFORM := windows
NATIVE_EXT := dll
endif
NATIVE_TARGET := $(NATIVE_PLATFORM)/$(NATIVE_ARCH)

LINUX_TARGETS := linux/x86_64 linux/i686 linux/aarch64 linux/armv7 \
    linux/armv7hf linux/riscv64 linux/powerpc64le linux/mips \
    linux/mipsel linux/mips64el linux/s390x linux/loongarch64
WINDOWS_TARGETS := windows/x86_64 windows/i686
MACOS_TARGETS := macos/x86_64 macos/aarch64
IOS_TARGETS := ios/aarch64 iossim/x86_64 iossim/aarch64
ANDROID_TARGETS := android/aarch64 android/armv7
WASM_TARGETS := wasm/wasm32
SUPPORTED_TARGETS := $(LINUX_TARGETS) $(WINDOWS_TARGETS) \
    $(MACOS_TARGETS) $(IOS_TARGETS) $(ANDROID_TARGETS) $(WASM_TARGETS)

.DEFAULT_GOAL := $(NATIVE_TARGET)
.PHONY: all test wine wasm clean build-target check-deps $(SUPPORTED_TARGETS)

all: $(SUPPORTED_TARGETS)

define linux_target
linux/$(1):
	@$(MAKE) -s --no-print-directory build-target TARGET=linux/$(1) \
		TARGET_CC="$(2)" TARGET_AR="$(4)" TARGET_EXT=so \
		TARGET_CFLAGS="-fPIC" TARGET_SHARED="-shared" \
		TARGET_SYSTEM_LIBS="-pthread -ldl -lm $(if $(filter armv7 armv7hf mips mipsel mips64el,$(1)),-latomic)"
endef

$(eval $(call linux_target,x86_64,gcc,g++,ar))
$(eval $(call linux_target,i686,i686-linux-gnu-gcc,i686-linux-gnu-g++,i686-linux-gnu-ar))
$(eval $(call linux_target,aarch64,aarch64-linux-gnu-gcc,aarch64-linux-gnu-g++,aarch64-linux-gnu-ar))
$(eval $(call linux_target,armv7,arm-linux-gnueabi-gcc,arm-linux-gnueabi-g++,arm-linux-gnueabi-ar))
$(eval $(call linux_target,armv7hf,arm-linux-gnueabihf-gcc,arm-linux-gnueabihf-g++,arm-linux-gnueabihf-ar))
$(eval $(call linux_target,riscv64,riscv64-linux-gnu-gcc,riscv64-linux-gnu-g++,riscv64-linux-gnu-ar))
$(eval $(call linux_target,powerpc64le,powerpc64le-linux-gnu-gcc,powerpc64le-linux-gnu-g++,powerpc64le-linux-gnu-ar))
$(eval $(call linux_target,mips,mips-linux-gnu-gcc,mips-linux-gnu-g++,mips-linux-gnu-ar))
$(eval $(call linux_target,mipsel,mipsel-linux-gnu-gcc-13,mipsel-linux-gnu-g++-13,mipsel-linux-gnu-ar))
$(eval $(call linux_target,mips64el,mips64el-linux-gnuabi64-gcc,mips64el-linux-gnuabi64-g++,mips64el-linux-gnuabi64-ar))
$(eval $(call linux_target,s390x,s390x-linux-gnu-gcc,s390x-linux-gnu-g++,s390x-linux-gnu-ar))
$(eval $(call linux_target,loongarch64,loongarch64-linux-gnu-gcc,loongarch64-linux-gnu-g++,loongarch64-linux-gnu-ar))

define windows_target
windows/$(1):
	@$(MAKE) -s --no-print-directory build-target TARGET=windows/$(1) \
		TARGET_CC="$(2)" TARGET_AR="$(4)" TARGET_EXT=dll TARGET_CFLAGS="" \
		TARGET_SHARED="-shared -Wl,--out-implib,$(BIN_DIR)/windows/$(1)/libroute.dll.a" \
		TARGET_CLI_EXT=.exe TARGET_SYSTEM_LIBS="-lm"
endef

$(eval $(call windows_target,x86_64,x86_64-w64-mingw32-gcc,x86_64-w64-mingw32-g++,x86_64-w64-mingw32-ar))
$(eval $(call windows_target,i686,i686-w64-mingw32-gcc,i686-w64-mingw32-g++,i686-w64-mingw32-ar))

define macos_target
macos/$(1):
	@$(MAKE) -s --no-print-directory build-target TARGET=macos/$(1) \
		TARGET_CC="$(OSXCROSS_ROOT)/bin/$(2)" \
		TARGET_AR="$(4)" TARGET_EXT=dylib \
		TARGET_CFLAGS="-fPIC" TARGET_SYSTEM_LIBS="-lm" \
		TARGET_SHARED="-dynamiclib -Wl,-install_name,@rpath/libroute.dylib"
endef

$(eval $(call macos_target,x86_64,o64-clang,o64-clang++,$(OSXCROSS_X86_64_AR)))
$(eval $(call macos_target,aarch64,oa64-clang,oa64-clang++,$(OSXCROSS_AARCH64_AR)))

define android_target
android/$(1):
	@$(MAKE) -s --no-print-directory build-target TARGET=android/$(1) \
		TARGET_CC="$(NDK_BIN)/$(2)$(ANDROID_API)-clang" \
		TARGET_AR="$(NDK_BIN)/llvm-ar" \
		TARGET_EXT=so TARGET_CFLAGS="-fPIC" TARGET_SHARED="-shared" \
		TARGET_SYSTEM_LIBS="-ldl -lm"
endef

$(eval $(call android_target,aarch64,aarch64-linux-android))
$(eval $(call android_target,armv7,armv7a-linux-androideabi))

IOS_SDK ?= $(shell ls -d "$(OSXCROSS_ROOT)"/SDK/iPhoneOS*.sdk 2>/dev/null | sort -V | tail -n 1)
IOSSIM_SDK ?= $(shell ls -d "$(OSXCROSS_ROOT)"/SDK/iPhoneSimulator*.sdk 2>/dev/null | sort -V | tail -n 1)

define ios_target
$(2)/$(1):
	@$(MAKE) -s --no-print-directory build-target TARGET=$(2)/$(1) \
		TARGET_CC="$(OSXCROSS_ROOT)/bin/$(3)" \
		TARGET_AR="$(OSXCROSS_ROOT)/bin/$(4)-apple-darwin25.1-ar" \
		TARGET_EXT=dylib TARGET_CFLAGS="-fPIC -isysroot $(5)" \
		TARGET_SHARED="-dynamiclib -isysroot $(5)" TARGET_LIBRARY_ONLY=1 \
		TARGET_SDK="$(5)"
endef

$(eval $(call ios_target,aarch64,ios,ios64-clang,aarch64,$(IOS_SDK)))
$(eval $(call ios_target,x86_64,iossim,iossimx64-clang,x86_64,$(IOSSIM_SDK)))
$(eval $(call ios_target,aarch64,iossim,iossim64-clang,aarch64,$(IOSSIM_SDK)))

wasm/wasm32:
	@$(MAKE) -s --no-print-directory build-target TARGET=wasm/wasm32 \
		TARGET_CC="$(WASM_EMCC)" \
		TARGET_AR="$(WASM_EMAR)" \
		TARGET_EXT=wasm TARGET_CFLAGS="" TARGET_SYSTEM_LIBS="-lm" \
		TARGET_SHARED="--no-entry -sSTANDALONE_WASM=1 -sINITIAL_MEMORY=268435456 -sSTACK_SIZE=4194304 -sEXPORTED_FUNCTIONS=_kc_route_open,_kc_route_add,_kc_route_run,_kc_route_http,_kc_route_close,_kc_route_strerror,_kc_route_version"

DEP_HTTP = $(KCDIST_DIR)/libhttp.c/$(TARGET)
DEP_REGEX = $(KCDIST_DIR)/libregex.c/$(TARGET)
DEP_HEADERS = $(DEP_HTTP)/libhttp.h $(DEP_REGEX)/libregex.h
DEP_LIBS = $(DEP_HTTP)/libhttp.a $(DEP_REGEX)/libregex.a
DEP_INCLUDES = -I"$(DEP_HTTP)" -I"$(DEP_REGEX)"
OUT_DIR = $(BIN_DIR)/$(TARGET)
OUT_OBJECT = $(BUILD_DIR)/$(TARGET)/libroute.o
OUT_STATIC = $(OUT_DIR)/libroute.a
OUT_SHARED = $(OUT_DIR)/libroute.$(TARGET_EXT)
OUT_CLI = $(OUT_DIR)/route$(TARGET_CLI_EXT)
OUT_PRODUCT = $(if $(filter wasm/%,$(TARGET)),$(OUT_SHARED),$(if $(TARGET_LIBRARY_ONLY),$(OUT_STATIC),$(OUT_CLI)))

check-deps:
	@test -n "$(KCDIST_DIR)" || { echo "route: set KCDIST_DIR to the precompiled kclib distribution" >&2; exit 1; }
	@command -v "$(TARGET_CC)" >/dev/null 2>&1 || { echo "route: missing target compiler $(TARGET_CC)" >&2; exit 1; }
	@command -v "$(TARGET_AR)" >/dev/null 2>&1 || { echo "route: missing target archiver $(TARGET_AR)" >&2; exit 1; }
	@for file in $(DEP_HEADERS) $(DEP_LIBS); do \
		test -f "$$file" || { echo "route: missing dependency for $(TARGET): $$file" >&2; exit 1; }; \
	done
	@if [ -n "$(TARGET_SDK)" ]; then test -d "$(TARGET_SDK)" || { echo "route: missing SDK $(TARGET_SDK)" >&2; exit 1; }; fi

build-target: check-deps
	@mkdir -p "$(OUT_DIR)"
	@$(MAKE) -s --no-print-directory "$(OUT_STATIC)" "$(OUT_PRODUCT)" \
		$(if $(filter wasm/%,$(TARGET)),,$(if $(TARGET_LIBRARY_ONLY),,$(OUT_SHARED))) \
		TARGET="$(TARGET)" TARGET_CC="$(TARGET_CC)" TARGET_AR="$(TARGET_AR)" \
		TARGET_EXT="$(TARGET_EXT)" TARGET_CLI_EXT="$(TARGET_CLI_EXT)" \
		TARGET_CFLAGS="$(TARGET_CFLAGS)" TARGET_SHARED="$(TARGET_SHARED)" \
		KCDIST_DIR="$(KCDIST_DIR)" BUILD_VERSION="$(BUILD_VERSION)"
	@echo "OK $(TARGET)"

$(OUT_OBJECT): src/libroute.c src/libroute.h Makefile $(DEP_HEADERS)
	@mkdir -p "$(dir $@)"
	@$(TARGET_CC) -std=c11 -O2 -Wall -Wextra -Werror $(TARGET_CFLAGS) \
		-DKC_ROUTE_BUILD_VERSION=$(BUILD_VERSION) -Isrc $(DEP_INCLUDES) \
		-c -o "$@" src/libroute.c

$(OUT_STATIC): $(OUT_OBJECT)
	@mkdir -p "$(OUT_DIR)"
	@$(TARGET_AR) rcs "$@" "$(OUT_OBJECT)"

$(OUT_SHARED): $(OUT_OBJECT) $(DEP_LIBS)
	@mkdir -p "$(OUT_DIR)"
	@$(TARGET_CC) $(TARGET_SHARED) -o "$@" "$(OUT_OBJECT)" "$(DEP_REGEX)/libregex.a"

$(OUT_CLI): src/route.c $(OUT_STATIC) $(DEP_LIBS)
	@mkdir -p "$(BUILD_DIR)/$(TARGET)"
	@$(TARGET_CC) -std=c11 -O2 -Wall -Wextra -Werror -Isrc $(DEP_INCLUDES) \
		-c -o "$(BUILD_DIR)/$(TARGET)/route.o" src/route.c
	@$(TARGET_CC) -o "$@" "$(BUILD_DIR)/$(TARGET)/route.o" \
		"$(OUT_STATIC)" "$(DEP_REGEX)/libregex.a"

test:
	@if [ -n "$(filter wasm,$(MAKECMDGOALS))" ]; then \
		command -v "$(WASM_EMCC)" >/dev/null 2>&1 || { echo "route: missing Emscripten compiler $(WASM_EMCC)" >&2; exit 1; }; \
		command -v "$(WASM_EMAR)" >/dev/null 2>&1 || { echo "route: missing Emscripten archiver $(WASM_EMAR)" >&2; exit 1; }; \
		node="$(WASM_NODE)"; if [ -n "$(EMSDK_NODE)" ]; then node="$(EMSDK_NODE)"; fi; \
		command -v "$$node" >/dev/null 2>&1 || { echo "route: missing WASM test runtime $$node" >&2; exit 1; }; \
		$(MAKE) -s --no-print-directory wasm/wasm32 || exit 1; \
		mkdir -p "$(BUILD_DIR)/test-wasm" || exit 1; \
		"$(WASM_EMCC)" -std=c11 -O2 -Wall -Wextra -Werror -Isrc \
			-I"$(KCDIST_DIR)/libhttp.c/wasm/wasm32" \
			-I"$(KCDIST_DIR)/libregex.c/wasm/wasm32" \
			-o "$(BUILD_DIR)/test-wasm/route-test.js" src/test.c \
			"$(BIN_DIR)/wasm/wasm32/libroute.a" \
			"$(KCDIST_DIR)/libregex.c/wasm/wasm32/libregex.a" \
			-sINITIAL_MEMORY=268435456 -sSTACK_SIZE=4194304 || exit 1; \
		(cd "$(BUILD_DIR)/test-wasm" && "$$node" route-test.js); \
	else \
		$(MAKE) -s --no-print-directory "$(NATIVE_TARGET)" || exit 1; \
		mkdir -p "$(BUILD_DIR)/test" || exit 1; \
		$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Isrc \
			-I"$(KCDIST_DIR)/libhttp.c/$(NATIVE_TARGET)" \
			-I"$(KCDIST_DIR)/libregex.c/$(NATIVE_TARGET)" \
			-o "$(BUILD_DIR)/test/route-test" src/test.c \
			"$(BIN_DIR)/$(NATIVE_TARGET)/libroute.a" \
			"$(KCDIST_DIR)/libregex.c/$(NATIVE_TARGET)/libregex.a" || exit 1; \
		cp "$(BIN_DIR)/$(NATIVE_TARGET)/route$(TARGET_CLI_EXT)" \
			"$(BUILD_DIR)/test/route" || exit 1; \
		(cd "$(BUILD_DIR)/test" && ./route-test); \
	fi

wasm:
	@if [ -z "$(filter test,$(MAKECMDGOALS))" ]; then \
		echo "Use 'make test wasm' to run tests through Emscripten and Node.js." >&2; \
		exit 1; \
	fi
	@:

clean:
	@rm -rf "$(BIN_DIR)" "$(BUILD_DIR)"
	@echo "OK clean"
