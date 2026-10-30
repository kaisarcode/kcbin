## Makefile
## Summary: Cross-compiles semidx using precompiled target-matched kclibs.
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/lgpl-3.0.html

KCDIST_DIR ?=
BIN_DIR := bin
BUILD_DIR := .build
WINE ?= wine
WINE_X86_64_CC ?= x86_64-w64-mingw32-gcc
WINE_X86_64_CXX ?= x86_64-w64-mingw32-g++
WASM_EMCC ?= emcc
WASM_EMCXX ?= em++
WASM_NODE ?= node
BUILD_VERSION ?= $(shell date +%s)
OSXCROSS_ROOT ?= $(HOME)/.local/share/osxcross/target
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
ANDROID_TARGETS := android/aarch64 android/armv7
WASM_TARGETS := wasm/wasm32
SUPPORTED_TARGETS := $(LINUX_TARGETS) $(WINDOWS_TARGETS) \
    $(MACOS_TARGETS) $(ANDROID_TARGETS) $(WASM_TARGETS)

.DEFAULT_GOAL := $(NATIVE_TARGET)
.PHONY: all test wine wasm clean build-target check-deps $(SUPPORTED_TARGETS)

all: $(SUPPORTED_TARGETS)

define linux_target
linux/$(1):
	@$(MAKE) -s --no-print-directory build-target TARGET=linux/$(1) \
		TARGET_CC="$(2)" TARGET_CXX="$(3)" TARGET_EXT=so \
		TARGET_CFLAGS="-fPIC" TARGET_SHARED="-shared" \
		TARGET_SYSTEM_LIBS="-pthread -ldl -lm $(if $(filter armv7 armv7hf mips mipsel mips64el,$(1)),-latomic)"
endef

$(eval $(call linux_target,x86_64,gcc,g++))
$(eval $(call linux_target,i686,i686-linux-gnu-gcc,i686-linux-gnu-g++))
$(eval $(call linux_target,aarch64,aarch64-linux-gnu-gcc,aarch64-linux-gnu-g++))
$(eval $(call linux_target,armv7,arm-linux-gnueabi-gcc,arm-linux-gnueabi-g++))
$(eval $(call linux_target,armv7hf,arm-linux-gnueabihf-gcc,arm-linux-gnueabihf-g++))
$(eval $(call linux_target,riscv64,riscv64-linux-gnu-gcc,riscv64-linux-gnu-g++))
$(eval $(call linux_target,powerpc64le,powerpc64le-linux-gnu-gcc,powerpc64le-linux-gnu-g++))
$(eval $(call linux_target,mips,mips-linux-gnu-gcc,mips-linux-gnu-g++))
$(eval $(call linux_target,mipsel,mipsel-linux-gnu-gcc-13,mipsel-linux-gnu-g++-13))
$(eval $(call linux_target,mips64el,mips64el-linux-gnuabi64-gcc,mips64el-linux-gnuabi64-g++))
$(eval $(call linux_target,s390x,s390x-linux-gnu-gcc,s390x-linux-gnu-g++))
$(eval $(call linux_target,loongarch64,loongarch64-linux-gnu-gcc,loongarch64-linux-gnu-g++))

define windows_target
windows/$(1):
	@$(MAKE) -s --no-print-directory build-target TARGET=windows/$(1) \
		TARGET_CC="$(2)" TARGET_CXX="$(3)" TARGET_EXT=dll TARGET_CFLAGS="" \
		TARGET_SHARED="-shared -Wl,--out-implib,$(BIN_DIR)/windows/$(1)/libsemidx.dll.a" \
		TARGET_CLI_EXT=.exe TARGET_SYSTEM_LIBS="-lm"
endef

$(eval $(call windows_target,x86_64,x86_64-w64-mingw32-gcc,x86_64-w64-mingw32-g++))
$(eval $(call windows_target,i686,i686-w64-mingw32-gcc,i686-w64-mingw32-g++))

define macos_target
macos/$(1):
	@$(MAKE) -s --no-print-directory build-target TARGET=macos/$(1) \
		TARGET_CC="$(OSXCROSS_ROOT)/bin/$(2)" \
		TARGET_CXX="$(OSXCROSS_ROOT)/bin/$(3)" TARGET_EXT=dylib \
		TARGET_CFLAGS="-fPIC" TARGET_SYSTEM_LIBS="-lm" \
		TARGET_SHARED="-dynamiclib -Wl,-install_name,@rpath/libsemidx.dylib"
endef

$(eval $(call macos_target,x86_64,o64-clang,o64-clang++))
$(eval $(call macos_target,aarch64,oa64-clang,oa64-clang++))

define android_target
android/$(1):
	@$(MAKE) -s --no-print-directory build-target TARGET=android/$(1) \
		TARGET_CC="$(NDK_BIN)/$(2)$(ANDROID_API)-clang" \
		TARGET_CXX="$(NDK_BIN)/$(2)$(ANDROID_API)-clang++" \
		TARGET_EXT=so TARGET_CFLAGS="-fPIC" TARGET_SHARED="-shared" \
		TARGET_SYSTEM_LIBS="-ldl -lm"
endef

$(eval $(call android_target,aarch64,aarch64-linux-android))
$(eval $(call android_target,armv7,armv7a-linux-androideabi))

wasm/wasm32:
	@$(MAKE) -s --no-print-directory build-target TARGET=wasm/wasm32 \
		TARGET_CC="$(WASM_EMCC)" TARGET_CXX="$(WASM_EMCXX)" \
		TARGET_EXT=wasm TARGET_CFLAGS="" TARGET_SYSTEM_LIBS="-lm" \
		TARGET_SHARED="--no-entry -sSTANDALONE_WASM=1 -sINITIAL_MEMORY=268435456 -sSTACK_SIZE=4194304 -sEXPORTED_FUNCTIONS=_kc_semidx_build,_kc_semidx_open,_kc_semidx_save,_kc_semidx_search,_kc_semidx_count,_kc_semidx_free,_kc_semidx_close,_kc_semidx_strerror,_kc_semidx_version"

DEP_EMB = $(KCDIST_DIR)/libemb.c/$(TARGET)
DEP_HNSW = $(KCDIST_DIR)/libhnsw.c/$(TARGET)
DEP_MMAP = $(KCDIST_DIR)/libmmap.c/$(TARGET)
DEP_DIRS = $(DEP_EMB) $(DEP_HNSW) $(DEP_MMAP)
DEP_LIBS = $(DEP_EMB)/libemb.a \
    $(DEP_HNSW)/libhnsw.a \
    $(DEP_MMAP)/libmmap.a
DEP_HEADERS = $(DEP_EMB)/libemb.h \
    $(DEP_HNSW)/libhnsw.h $(DEP_MMAP)/libmmap.h
DEP_INCLUDES = $(addprefix -I,$(DEP_DIRS))
DEP_LINKS = $(DEP_LIBS) $(TARGET_SYSTEM_LIBS)
OUT_DIR = $(BIN_DIR)/$(TARGET)
OUT_SHARED = $(OUT_DIR)/libsemidx.$(TARGET_EXT)
OUT_CLI = $(OUT_DIR)/semidx$(TARGET_CLI_EXT)
OUT_OBJECT = $(BUILD_DIR)/$(TARGET)/libsemidx.o
TARGET_PRODUCT = $(if $(filter wasm/%,$(TARGET)),$(OUT_SHARED),$(OUT_CLI))

check-deps:
	@test -n "$(KCDIST_DIR)" || { echo "semidx: set KCDIST_DIR to the precompiled kclib distribution" >&2; exit 1; }
	@command -v "$(TARGET_CC)" >/dev/null 2>&1 || { echo "semidx: missing target compiler $(TARGET_CC)" >&2; exit 1; }
	@command -v "$(TARGET_CXX)" >/dev/null 2>&1 || { echo "semidx: missing target C++ linker $(TARGET_CXX)" >&2; exit 1; }
	@for path in $(DEP_LIBS) $(DEP_HEADERS); do \
		test -f "$$path" || { echo "semidx: missing dependency for $(TARGET): $$path" >&2; exit 1; }; \
	done

build-target: check-deps
	@mkdir -p "$(OUT_DIR)"
	@$(MAKE) -s --no-print-directory "$(TARGET_PRODUCT)" TARGET="$(TARGET)" \
		TARGET_CC="$(TARGET_CC)" TARGET_CXX="$(TARGET_CXX)" \
		TARGET_EXT="$(TARGET_EXT)" TARGET_CLI_EXT="$(TARGET_CLI_EXT)" \
		TARGET_CFLAGS="$(TARGET_CFLAGS)" TARGET_SHARED="$(TARGET_SHARED)" \
		TARGET_SYSTEM_LIBS="$(TARGET_SYSTEM_LIBS)" \
		KCDIST_DIR="$(KCDIST_DIR)" BUILD_VERSION="$(BUILD_VERSION)"
	@echo "OK $(TARGET)"

$(OUT_OBJECT): src/libsemidx.c src/libsemidx.h Makefile $(DEP_HEADERS)
	@mkdir -p "$(dir $@)"
	@$(TARGET_CC) -std=c11 -O2 -Wall -Wextra -Werror $(TARGET_CFLAGS) \
		-DKC_SEMIDX_BUILD_VERSION=$(BUILD_VERSION) -Isrc $(DEP_INCLUDES) \
		-c -o "$@" src/libsemidx.c

$(OUT_SHARED): $(OUT_OBJECT) $(DEP_LIBS)
	@mkdir -p "$(OUT_DIR)"
	@$(TARGET_CXX) $(TARGET_SHARED) -o "$@" "$(OUT_OBJECT)" $(DEP_LINKS)

$(OUT_CLI): src/semidx.c src/libsemidx.h $(OUT_SHARED)
	@$(TARGET_CC) -std=c11 -O2 -Wall -Wextra -Werror -Isrc \
		-o "$@" src/semidx.c -L"$(OUT_DIR)" -lsemidx

$(BUILD_DIR)/test/semidx-test: src/test.c src/libsemidx.h $(BIN_DIR)/$(NATIVE_TARGET)/libsemidx.$(NATIVE_EXT)
	@mkdir -p "$(BUILD_DIR)/test"
	@$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Isrc \
		-o "$@" src/test.c -L"$(BIN_DIR)/$(NATIVE_TARGET)" \
		-lsemidx

test:
	@if [ -n "$(filter wasm,$(MAKECMDGOALS))" ]; then \
		if ! command -v "$(WASM_EMCC)" >/dev/null 2>&1 || \
			! command -v "$(WASM_EMCXX)" >/dev/null 2>&1; then \
			echo "Missing Emscripten compiler or linker" >&2; exit 1; \
		fi; \
		node="$(WASM_NODE)"; \
		if [ -n "$(EMSDK_NODE)" ]; then node="$(EMSDK_NODE)"; fi; \
		if ! command -v "$node" >/dev/null 2>&1; then \
			echo "Missing Emscripten test runtime: $node" >&2; exit 1; \
		fi; \
		$(MAKE) -s --no-print-directory wasm/wasm32 || exit 1; \
		mkdir -p "$(BUILD_DIR)/test-wasm" || exit 1; \
		"$(WASM_EMCC)" -std=c11 -O2 -Wall -Wextra -Werror -Isrc \
			-c -o "$(BUILD_DIR)/test-wasm/semidx-test.o" src/test.c || exit 1; \
		"$(WASM_EMCXX)" -o "$(BUILD_DIR)/test-wasm/semidx-test.js" \
			"$(BUILD_DIR)/test-wasm/semidx-test.o" \
			"$(BUILD_DIR)/wasm/wasm32/libsemidx.o" \
			"$(KCDIST_DIR)/libemb.c/wasm/wasm32/libemb.a" \
			"$(KCDIST_DIR)/libhnsw.c/wasm/wasm32/libhnsw.a" \
			"$(KCDIST_DIR)/libmmap.c/wasm/wasm32/libmmap.a" \
			-lm -sINITIAL_MEMORY=268435456 -sSTACK_SIZE=4194304 || exit 1; \
		(cd "$(BUILD_DIR)/test-wasm" && "$node" semidx-test.js all); \
	elif [ -n "$(filter wine,$(MAKECMDGOALS))" ]; then \
		if ! command -v "$(WINE)" >/dev/null 2>&1; then \
			echo "Missing Wine runtime: $(WINE)" >&2; exit 1; \
		fi; \
		if ! command -v "$(WINE_X86_64_CC)" >/dev/null 2>&1 || \
			! command -v "$(WINE_X86_64_CXX)" >/dev/null 2>&1; then \
			echo "Missing Windows cross-compilers" >&2; exit 1; \
		fi; \
		for file in libsemidx.dll libsemidx.dll.a semidx.exe; do \
			if [ ! -f "$(BIN_DIR)/windows/x86_64/$$file" ]; then \
				echo "Missing Windows artifact: $$file; run make windows/x86_64" >&2; \
				exit 1; \
			fi; \
		done; \
		mkdir -p "$(BUILD_DIR)/test-wine" || exit 1; \
		"$(WINE_X86_64_CC)" -std=c11 -O2 -Wall -Wextra -Werror -Isrc \
			-o "$(BUILD_DIR)/test-wine/semidx-test.exe" src/test.c \
			-L"$(BIN_DIR)/windows/x86_64" -lsemidx \
			|| exit 1; \
		cp "$(BIN_DIR)/windows/x86_64/libsemidx.dll" \
			"$(BIN_DIR)/windows/x86_64/semidx.exe" \
			"$(BUILD_DIR)/test-wine/" || exit 1; \
		for dll in \
			"$$($(WINE_X86_64_CC) -print-file-name=libgcc_s_seh-1.dll)" \
			"$$($(WINE_X86_64_CXX) -print-file-name=libstdc++-6.dll)" \
			"$$($(WINE_X86_64_CXX) -print-file-name=libwinpthread-1.dll)"; do \
			if [ -f "$$dll" ]; then \
				cp "$$dll" "$(BUILD_DIR)/test-wine/" || exit 1; \
			fi; \
		done; \
		(cd "$(BUILD_DIR)/test-wine" && \
			EGL_LOG_LEVEL=fatal WINEDEBUG=-all "$(WINE)" semidx-test.exe all); \
	else \
		$(MAKE) -s --no-print-directory $(NATIVE_TARGET) || exit 1; \
		$(MAKE) -s --no-print-directory "$(BUILD_DIR)/test/semidx-test" || exit 1; \
		cp "$(BIN_DIR)/$(NATIVE_TARGET)/semidx$(TARGET_CLI_EXT)" \
			"$(BUILD_DIR)/test/" || exit 1; \
		(cd "$(BUILD_DIR)/test" && \
			LD_LIBRARY_PATH="$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET):$LD_LIBRARY_PATH" \
			DYLD_LIBRARY_PATH="$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET):$DYLD_LIBRARY_PATH" \
			./semidx-test all); \
	fi

wine:
	@if [ -z "$(filter test,$(MAKECMDGOALS))" ]; then \
		echo "Use 'make test wine' to run tests through Wine." >&2; \
		exit 1; \
	fi
	@:

wasm:
	@if [ -z "$(filter test,$(MAKECMDGOALS))" ]; then \
		echo "Use 'make test wasm' to run tests through Emscripten and Node.js." >&2; \
		exit 1; \
	fi
	@:

clean:
	@rm -rf "$(BIN_DIR)" "$(BUILD_DIR)"
	@echo "OK clean"
