## Makefile
## Summary: Cross-compiles embdex using precompiled target-matched kclibs.
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
		TARGET_CC="$(2)" TARGET_CXX="$(3)" TARGET_AR="$(4)" TARGET_EXT=so \
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
		TARGET_CC="$(2)" TARGET_CXX="$(3)" TARGET_AR="$(4)" TARGET_EXT=dll TARGET_CFLAGS="" \
		TARGET_SHARED="-shared -Wl,--out-implib,$(BIN_DIR)/windows/$(1)/libembdex.dll.a" \
		TARGET_CLI_EXT=.exe TARGET_SYSTEM_LIBS="-lm"
endef

$(eval $(call windows_target,x86_64,x86_64-w64-mingw32-gcc,x86_64-w64-mingw32-g++,x86_64-w64-mingw32-ar))
$(eval $(call windows_target,i686,i686-w64-mingw32-gcc,i686-w64-mingw32-g++,i686-w64-mingw32-ar))

define macos_target
macos/$(1):
	@$(MAKE) -s --no-print-directory build-target TARGET=macos/$(1) \
		TARGET_CC="$(OSXCROSS_ROOT)/bin/$(2)" \
		TARGET_CXX="$(OSXCROSS_ROOT)/bin/$(3)" TARGET_AR="$(4)" TARGET_EXT=dylib \
		TARGET_CFLAGS="-fPIC" TARGET_SYSTEM_LIBS="-lm" \
		TARGET_SHARED="-dynamiclib -Wl,-install_name,@rpath/libembdex.dylib"
endef

$(eval $(call macos_target,x86_64,o64-clang,o64-clang++,$(OSXCROSS_X86_64_AR)))
$(eval $(call macos_target,aarch64,oa64-clang,oa64-clang++,$(OSXCROSS_AARCH64_AR)))

define android_target
android/$(1):
	@$(MAKE) -s --no-print-directory build-target TARGET=android/$(1) \
		TARGET_CC="$(NDK_BIN)/$(2)$(ANDROID_API)-clang" \
		TARGET_CXX="$(NDK_BIN)/$(2)$(ANDROID_API)-clang++" \
		TARGET_AR="$(NDK_BIN)/llvm-ar" \
		TARGET_EXT=so TARGET_CFLAGS="-fPIC" TARGET_SHARED="-shared" \
		TARGET_SYSTEM_LIBS="-ldl -lm"
endef

$(eval $(call android_target,aarch64,aarch64-linux-android))
$(eval $(call android_target,armv7,armv7a-linux-androideabi))

wasm/wasm32:
	@$(MAKE) -s --no-print-directory build-target TARGET=wasm/wasm32 \
		TARGET_CC="$(WASM_EMCC)" TARGET_CXX="$(WASM_EMCXX)" \
		TARGET_AR="$(WASM_EMAR)" \
		TARGET_EXT=mjs TARGET_CFLAGS="" TARGET_SYSTEM_LIBS="-lm" \
		TARGET_SHARED="--no-entry -sMODULARIZE=1 -sEXPORT_ES6=1 -sWASM_BIGINT=1 -sALLOW_MEMORY_GROWTH=1 -sALLOW_TABLE_GROWTH=1 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8,getValue,setValue,HEAPU8,HEAPF32,addFunction,removeFunction,FS -sINITIAL_MEMORY=268435456 -sSTACK_SIZE=4194304 -sEXPORTED_FUNCTIONS=_kc_embdex_build,_kc_embdex_build_with_dictionaries,_kc_embdex_open,_kc_embdex_search,_kc_embdex_close,_kc_embdex_strerror,_kc_embdex_version,_malloc,_free"

DEP_EMB = $(KCDIST_DIR)/libemb.c/$(TARGET)
DEP_HNSW = $(KCDIST_DIR)/libhnsw.c/$(TARGET)
DEP_BM25 = $(KCDIST_DIR)/libbm25.c/$(TARGET)
DEP_MMAP = $(KCDIST_DIR)/libmmap.c/$(TARGET)
DEP_NGRAM = $(KCDIST_DIR)/libngram.c/$(TARGET)
DEP_LEVEN = $(KCDIST_DIR)/libleven.c/$(TARGET)
DEP_DIRS = $(DEP_EMB) $(DEP_HNSW) $(DEP_BM25) $(DEP_MMAP) $(DEP_NGRAM) $(DEP_LEVEN)
DEP_LIBS = $(DEP_EMB)/libemb.a \
    $(DEP_HNSW)/libhnsw.a \
    $(DEP_BM25)/libbm25.a \
    $(DEP_MMAP)/libmmap.a \
    $(DEP_NGRAM)/libngram.a \
    $(DEP_LEVEN)/libleven.a
DEP_HEADERS = $(DEP_EMB)/libemb.h \
    $(DEP_HNSW)/libhnsw.h $(DEP_BM25)/libbm25.h \
    $(DEP_MMAP)/libmmap.h \
    $(DEP_NGRAM)/libngram.h $(DEP_LEVEN)/libleven.h
DEP_INCLUDES = $(addprefix -I,$(DEP_DIRS))
DEP_LINKS = $(DEP_LIBS) $(TARGET_SYSTEM_LIBS)
OUT_DIR = $(BIN_DIR)/$(TARGET)
OUT_SHARED = $(OUT_DIR)/libembdex.$(TARGET_EXT)
OUT_STATIC = $(OUT_DIR)/libembdex.a
OUT_CLI = $(OUT_DIR)/embdex$(TARGET_CLI_EXT)
OUT_OBJECT = $(BUILD_DIR)/$(TARGET)/libembdex.o
PARSON_OBJECT = $(BUILD_DIR)/$(TARGET)/parson.o
TARGET_PRODUCT = $(if $(filter wasm/%,$(TARGET)),$(OUT_SHARED),$(OUT_CLI))

check-deps:
	@test -n "$(KCDIST_DIR)" || { echo "embdex: set KCDIST_DIR to the precompiled kclib distribution" >&2; exit 1; }
	@command -v "$(TARGET_CC)" >/dev/null 2>&1 || { echo "embdex: missing target compiler $(TARGET_CC)" >&2; exit 1; }
	@command -v "$(TARGET_CXX)" >/dev/null 2>&1 || { echo "embdex: missing target C++ linker $(TARGET_CXX)" >&2; exit 1; }
	@command -v "$(TARGET_AR)" >/dev/null 2>&1 || { echo "embdex: missing target archiver $(TARGET_AR)" >&2; exit 1; }
	@for path in $(DEP_LIBS) $(DEP_HEADERS); do \
		test -f "$$path" || { echo "embdex: missing dependency for $(TARGET): $$path" >&2; exit 1; }; \
	done

build-target: check-deps
	@mkdir -p "$(OUT_DIR)"
	@$(MAKE) -s --no-print-directory "$(OUT_STATIC)" "$(TARGET_PRODUCT)" TARGET="$(TARGET)" \
		TARGET_CC="$(TARGET_CC)" TARGET_CXX="$(TARGET_CXX)" \
		TARGET_AR="$(TARGET_AR)" \
		TARGET_EXT="$(TARGET_EXT)" TARGET_CLI_EXT="$(TARGET_CLI_EXT)" \
		TARGET_CFLAGS="$(TARGET_CFLAGS)" TARGET_SHARED="$(TARGET_SHARED)" \
		TARGET_SYSTEM_LIBS="$(TARGET_SYSTEM_LIBS)" \
		KCDIST_DIR="$(KCDIST_DIR)" BUILD_VERSION="$(BUILD_VERSION)"
	@if [ "$(TARGET)" = "wasm/wasm32" ]; then \
		cp src/libembdex.js "$(OUT_DIR)/libembdex.js" && \
		printf '%s\n' '{"type":"module"}' > "$(OUT_DIR)/package.json"; \
	fi
	@echo "OK $(TARGET)"

$(OUT_OBJECT): src/libembdex.c src/libembdex.h lib/parson/parson.h Makefile $(DEP_HEADERS)
	@mkdir -p "$(dir $@)"
	@$(TARGET_CC) -std=c11 -O2 -Wall -Wextra -Werror $(TARGET_CFLAGS) \
		-DKC_EMBDEX_BUILD_VERSION=$(BUILD_VERSION) -Isrc -Ilib/parson $(DEP_INCLUDES) \
		-c -o "$@" src/libembdex.c

$(PARSON_OBJECT): lib/parson/parson.c lib/parson/parson.h Makefile
	@mkdir -p "$(dir $@)"
	@$(TARGET_CC) -std=c11 -O2 -w $(TARGET_CFLAGS) -Ilib/parson \
		-c -o "$@" lib/parson/parson.c

$(OUT_STATIC): $(OUT_OBJECT) $(PARSON_OBJECT)
	@mkdir -p "$(OUT_DIR)"
	@$(TARGET_AR) rcs "$@" $(OUT_OBJECT) $(PARSON_OBJECT)

$(OUT_SHARED): $(OUT_OBJECT) $(PARSON_OBJECT) $(DEP_LIBS)
	@mkdir -p "$(OUT_DIR)"
	@$(TARGET_CXX) $(TARGET_SHARED) -o "$@" "$(OUT_OBJECT)" "$(PARSON_OBJECT)" $(DEP_LINKS)

$(OUT_CLI): src/embdex.c src/libembdex.h lib/parson/parson.h $(OUT_STATIC) $(OUT_SHARED) $(DEP_LIBS)
	@mkdir -p "$(BUILD_DIR)/$(TARGET)"
	@$(TARGET_CC) -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Ilib/parson \
		-c -o "$(BUILD_DIR)/$(TARGET)/embdex.o" src/embdex.c
	@$(TARGET_CXX) -o "$@" "$(BUILD_DIR)/$(TARGET)/embdex.o" \
		"$(OUT_STATIC)" $(DEP_LINKS)

$(BUILD_DIR)/test/embdex-test: src/test.c src/libembdex.h $(BIN_DIR)/$(NATIVE_TARGET)/libembdex.$(NATIVE_EXT)
	@mkdir -p "$(BUILD_DIR)/test"
	@$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Isrc \
		-o "$@" src/test.c -L"$(BIN_DIR)/$(NATIVE_TARGET)" \
		-lembdex

test:
	@if [ -n "$(filter wasm,$(MAKECMDGOALS))" ]; then \
		if ! command -v "$(WASM_EMCC)" >/dev/null 2>&1 || \
			! command -v "$(WASM_EMCXX)" >/dev/null 2>&1; then \
			echo "Missing Emscripten compiler or linker" >&2; exit 1; \
		fi; \
		node="$(WASM_NODE)"; \
		if [ -n "$(EMSDK_NODE)" ]; then node="$(EMSDK_NODE)"; fi; \
		if ! command -v "$$node" >/dev/null 2>&1; then \
			echo "Missing Emscripten test runtime: $$node" >&2; exit 1; \
		fi; \
		$(MAKE) -s --no-print-directory wasm/wasm32 || exit 1; \
		mkdir -p "$(BUILD_DIR)/test-wasm" || exit 1; \
		"$(WASM_EMCC)" -std=c11 -O2 -Wall -Wextra -Werror -Isrc \
			-c -o "$(BUILD_DIR)/test-wasm/embdex-test.o" src/test.c || exit 1; \
		"$(WASM_EMCXX)" -o "$(BUILD_DIR)/test-wasm/embdex-test.js" \
			"$(BUILD_DIR)/test-wasm/embdex-test.o" \
			"$(BIN_DIR)/wasm/wasm32/libembdex.a" \
			"$(KCDIST_DIR)/libemb.c/wasm/wasm32/libemb.a" \
			"$(KCDIST_DIR)/libhnsw.c/wasm/wasm32/libhnsw.a" \
			"$(KCDIST_DIR)/libbm25.c/wasm/wasm32/libbm25.a" \
			"$(KCDIST_DIR)/libmmap.c/wasm/wasm32/libmmap.a" \
			"$(KCDIST_DIR)/libngram.c/wasm/wasm32/libngram.a" \
			"$(KCDIST_DIR)/libleven.c/wasm/wasm32/libleven.a" \
			-lm -sINITIAL_MEMORY=268435456 -sSTACK_SIZE=4194304 || exit 1; \
		(cd "$(BUILD_DIR)/test-wasm" && "$$node" embdex-test.js all) && "$$node" src/testjs.mjs; \
	elif [ -n "$(filter wine,$(MAKECMDGOALS))" ]; then \
		if ! command -v "$(WINE)" >/dev/null 2>&1; then \
			echo "Missing Wine runtime: $(WINE)" >&2; exit 1; \
		fi; \
		if ! command -v "$(WINE_X86_64_CC)" >/dev/null 2>&1 || \
			! command -v "$(WINE_X86_64_CXX)" >/dev/null 2>&1; then \
			echo "Missing Windows cross-compilers" >&2; exit 1; \
		fi; \
		for file in libembdex.dll libembdex.dll.a embdex.exe; do \
			if [ ! -f "$(BIN_DIR)/windows/x86_64/$$file" ]; then \
				echo "Missing Windows artifact: $$file; run make windows/x86_64" >&2; \
				exit 1; \
			fi; \
		done; \
		mkdir -p "$(BUILD_DIR)/test-wine" || exit 1; \
		"$(WINE_X86_64_CC)" -std=c11 -O2 -Wall -Wextra -Werror -Isrc \
			-o "$(BUILD_DIR)/test-wine/embdex-test.exe" src/test.c \
			-L"$(BIN_DIR)/windows/x86_64" -lembdex \
			|| exit 1; \
		cp "$(BIN_DIR)/windows/x86_64/libembdex.dll" \
			"$(BIN_DIR)/windows/x86_64/embdex.exe" \
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
			EGL_LOG_LEVEL=fatal WINEDEBUG=-all "$(WINE)" embdex-test.exe all); \
	else \
		$(MAKE) -s --no-print-directory $(NATIVE_TARGET) || exit 1; \
		$(MAKE) -s --no-print-directory "$(BUILD_DIR)/test/embdex-test" || exit 1; \
		cp "$(BIN_DIR)/$(NATIVE_TARGET)/embdex$(TARGET_CLI_EXT)" \
			"$(BUILD_DIR)/test/" || exit 1; \
		(cd "$(BUILD_DIR)/test" && \
			LD_LIBRARY_PATH="$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET):$$LD_LIBRARY_PATH" \
			DYLD_LIBRARY_PATH="$(CURDIR)/$(BIN_DIR)/$(NATIVE_TARGET):$$DYLD_LIBRARY_PATH" \
			./embdex-test all); \
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
