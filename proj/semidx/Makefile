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
SUPPORTED_TARGETS := $(LINUX_TARGETS) $(WINDOWS_TARGETS) \
    $(MACOS_TARGETS) $(ANDROID_TARGETS)

.DEFAULT_GOAL := $(NATIVE_TARGET)
.PHONY: all test wine clean build-target check-deps $(SUPPORTED_TARGETS)

all: $(SUPPORTED_TARGETS)

define linux_target
linux/$(1):
	@$(MAKE) --no-print-directory build-target TARGET=linux/$(1) \
		TARGET_CC="$(2)" TARGET_EXT=so TARGET_CFLAGS="-fPIC" \
		TARGET_SHARED="-shared"
endef

$(eval $(call linux_target,x86_64,gcc))
$(eval $(call linux_target,i686,i686-linux-gnu-gcc))
$(eval $(call linux_target,aarch64,aarch64-linux-gnu-gcc))
$(eval $(call linux_target,armv7,arm-linux-gnueabi-gcc))
$(eval $(call linux_target,armv7hf,arm-linux-gnueabihf-gcc))
$(eval $(call linux_target,riscv64,riscv64-linux-gnu-gcc))
$(eval $(call linux_target,powerpc64le,powerpc64le-linux-gnu-gcc))
$(eval $(call linux_target,mips,mips-linux-gnu-gcc))
$(eval $(call linux_target,mipsel,mipsel-linux-gnu-gcc-13))
$(eval $(call linux_target,mips64el,mips64el-linux-gnuabi64-gcc))
$(eval $(call linux_target,s390x,s390x-linux-gnu-gcc))
$(eval $(call linux_target,loongarch64,loongarch64-linux-gnu-gcc))

define windows_target
windows/$(1):
	@$(MAKE) --no-print-directory build-target TARGET=windows/$(1) \
		TARGET_CC="$(2)" TARGET_EXT=dll TARGET_CFLAGS="" \
		TARGET_SHARED="-shared -Wl,--out-implib,$(BIN_DIR)/windows/$(1)/libsemidx.dll.a" \
		TARGET_CLI_EXT=.exe
endef

$(eval $(call windows_target,x86_64,x86_64-w64-mingw32-gcc))
$(eval $(call windows_target,i686,i686-w64-mingw32-gcc))

define macos_target
macos/$(1):
	@$(MAKE) --no-print-directory build-target TARGET=macos/$(1) \
		TARGET_CC="$(OSXCROSS_ROOT)/bin/$(2)" TARGET_EXT=dylib \
		TARGET_CFLAGS="-fPIC" \
		TARGET_SHARED="-dynamiclib -Wl,-install_name,@rpath/libsemidx.dylib"
endef

$(eval $(call macos_target,x86_64,o64-clang))
$(eval $(call macos_target,aarch64,oa64-clang))

define android_target
android/$(1):
	@$(MAKE) --no-print-directory build-target TARGET=android/$(1) \
		TARGET_CC="$(NDK_BIN)/$(2)$(ANDROID_API)-clang" \
		TARGET_EXT=so TARGET_CFLAGS="-fPIC" TARGET_SHARED="-shared"
endef

$(eval $(call android_target,aarch64,aarch64-linux-android))
$(eval $(call android_target,armv7,armv7a-linux-androideabi))

DEP_EMB = $(KCDIST_DIR)/libemb.c/$(TARGET)
DEP_HNSW = $(KCDIST_DIR)/libhnsw.c/$(TARGET)
DEP_MMAP = $(KCDIST_DIR)/libmmap.c/$(TARGET)
DEP_DIRS = $(DEP_EMB) $(DEP_HNSW) $(DEP_MMAP)
DEP_LIBS = $(DEP_EMB)/libemb.$(TARGET_EXT) \
    $(DEP_HNSW)/libhnsw.$(TARGET_EXT) \
    $(DEP_MMAP)/libmmap.$(TARGET_EXT)
DEP_HEADERS = $(DEP_EMB)/libemb.h \
    $(DEP_HNSW)/libhnsw.h $(DEP_MMAP)/libmmap.h
DEP_IMPORTS = $(DEP_EMB)/libemb.dll.a \
    $(DEP_HNSW)/libhnsw.dll.a $(DEP_MMAP)/libmmap.dll.a
DEP_INCLUDES = $(addprefix -I,$(DEP_DIRS))
DEP_LINKS = $(addprefix -L,$(DEP_DIRS)) -lemb -lhnsw -lmmap -lm
OUT_DIR = $(BIN_DIR)/$(TARGET)
OUT_SHARED = $(OUT_DIR)/libsemidx.$(TARGET_EXT)
OUT_CLI = $(OUT_DIR)/semidx$(TARGET_CLI_EXT)

check-deps:
	@test -n "$(KCDIST_DIR)" || { echo "semidx: set KCDIST_DIR to the precompiled kclib distribution" >&2; exit 1; }
	@command -v "$(TARGET_CC)" >/dev/null 2>&1 || { echo "semidx: missing target compiler $(TARGET_CC)" >&2; exit 1; }
	@for path in $(DEP_LIBS) $(DEP_HEADERS) $(if $(filter windows/%,$(TARGET)),$(DEP_IMPORTS)); do \
		test -f "$$path" || { echo "semidx: missing dependency for $(TARGET): $$path" >&2; exit 1; }; \
	done

build-target: check-deps
	@mkdir -p "$(OUT_DIR)"
	@$(MAKE) --no-print-directory "$(OUT_CLI)" TARGET="$(TARGET)" \
		TARGET_CC="$(TARGET_CC)" TARGET_EXT="$(TARGET_EXT)" \
		TARGET_CLI_EXT="$(TARGET_CLI_EXT)" TARGET_CFLAGS="$(TARGET_CFLAGS)" \
		TARGET_SHARED="$(TARGET_SHARED)" \
		KCDIST_DIR="$(KCDIST_DIR)" BUILD_VERSION="$(BUILD_VERSION)"
	@echo "OK $(TARGET)"

$(OUT_SHARED): src/libsemidx.c src/libsemidx.h Makefile $(DEP_LIBS) $(DEP_HEADERS)
	@mkdir -p "$(OUT_DIR)"
	@$(TARGET_CC) -std=c11 -O2 -Wall -Wextra -Werror $(TARGET_CFLAGS) \
		-DKC_SEMIDX_BUILD_VERSION=$(BUILD_VERSION) -Isrc $(DEP_INCLUDES) \
		$(TARGET_SHARED) -o "$@" src/libsemidx.c $(DEP_LINKS)

$(OUT_CLI): src/semidx.c src/libsemidx.h $(OUT_SHARED)
	@$(TARGET_CC) -std=c11 -O2 -Wall -Wextra -Werror -Isrc \
		-o "$@" src/semidx.c -L"$(OUT_DIR)" -lsemidx $(DEP_LINKS)

NATIVE_DEP_LINKS = -L"$(KCDIST_DIR)/libemb.c/$(NATIVE_TARGET)" \
    -L"$(KCDIST_DIR)/libhnsw.c/$(NATIVE_TARGET)" \
    -L"$(KCDIST_DIR)/libmmap.c/$(NATIVE_TARGET)" \
    -lemb -lhnsw -lmmap -lm

$(BUILD_DIR)/test/semidx-test: src/test.c src/libsemidx.h $(BIN_DIR)/$(NATIVE_TARGET)/libsemidx.$(NATIVE_EXT)
	@mkdir -p "$(BUILD_DIR)/test"
	@$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Isrc \
		-o "$@" src/test.c -L"$(BIN_DIR)/$(NATIVE_TARGET)" \
		-lsemidx $(NATIVE_DEP_LINKS)

test:
	@if [ -n "$(filter wine,$(MAKECMDGOALS))" ]; then \
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
			-L"$(KCDIST_DIR)/libemb.c/windows/x86_64" \
			-L"$(KCDIST_DIR)/libhnsw.c/windows/x86_64" \
			-L"$(KCDIST_DIR)/libmmap.c/windows/x86_64" \
			-lemb -lhnsw -lmmap -lm || exit 1; \
		cp "$(BIN_DIR)/windows/x86_64/libsemidx.dll" \
			"$(BIN_DIR)/windows/x86_64/semidx.exe" \
			"$(BUILD_DIR)/test-wine/" || exit 1; \
		for dep in libemb libhnsw libmmap; do \
			file="$(KCDIST_DIR)/$$dep.c/windows/x86_64/$$dep.dll"; \
			if [ ! -f "$$file" ]; then \
				echo "Missing Windows runtime dependency: $$file" >&2; exit 1; \
			fi; \
			cp "$$file" "$(BUILD_DIR)/test-wine/" || exit 1; \
		done; \
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
		$(MAKE) --no-print-directory $(NATIVE_TARGET) || exit 1; \
		$(MAKE) --no-print-directory "$(BUILD_DIR)/test/semidx-test" || exit 1; \
		cp "$(BIN_DIR)/$(NATIVE_TARGET)/semidx$(TARGET_CLI_EXT)" \
			"$(BUILD_DIR)/test/" || exit 1; \
		(cd "$(BUILD_DIR)/test" && \
			LD_LIBRARY_PATH="$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET):$(KCDIST_DIR)/libemb.c/$(NATIVE_TARGET):$(KCDIST_DIR)/libhnsw.c/$(NATIVE_TARGET):$(KCDIST_DIR)/libmmap.c/$(NATIVE_TARGET):$$LD_LIBRARY_PATH" \
			DYLD_LIBRARY_PATH="$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET):$(KCDIST_DIR)/libemb.c/$(NATIVE_TARGET):$(KCDIST_DIR)/libhnsw.c/$(NATIVE_TARGET):$(KCDIST_DIR)/libmmap.c/$(NATIVE_TARGET):$$DYLD_LIBRARY_PATH" \
			./semidx-test all); \
	fi

wine:
	@if [ -z "$(filter test,$(MAKECMDGOALS))" ]; then \
		echo "Use 'make test wine' to run tests through Wine." >&2; \
		exit 1; \
	fi
	@:

clean:
	@rm -rf "$(BIN_DIR)" "$(BUILD_DIR)"
	@echo "OK clean"
