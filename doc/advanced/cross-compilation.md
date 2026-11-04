# Cross-compilation and linking

kcbin uses the cross-compilation conventions of kclib while compiling **only the level-1 composition**. Its level-0 kclib dependencies are already compiled and are not vendored, rebuilt, or copied into kcbin's source tree. Each project explicitly selects static or dynamic linking for its target.

## Platform and architecture matrix

Projects use the same normalized target names as kclib:

| Platform | Architectures |
| :--- | :--- |
| linux | x86_64, i686, aarch64, armv7, armv7hf, riscv64, powerpc64le, mips, mipsel, mips64el, s390x, loongarch64 |
| windows | x86_64, i686 |
| macos | x86_64, aarch64 |
| ios | aarch64 |
| iossim | x86_64, aarch64 |
| android | aarch64, armv7 |
| wasm | wasm32, subject to a compatible component/linking model |

This is **the set of recognized kclib targets, not a claim that every kcbin tool supports them all**. Each LV1 project specifies only its working targets; the associated compiler, cross-toolchain, sysroot and SDK settings follow the corresponding kclib Makefiles. A missing toolchain or dependent artifact is an error, not grounds for silently building for the host.

## Precompiled dependencies

Point `KCDIST_DIR` at an existing kclib distribution root. It groups all libraries and their public headers under one directory per `platform-arch`, with the per-library manifests at the distribution root.

For a hypothetical `http-server` composition targeting `linux/x86_64`, the needed inputs include:

```text
$KCDIST_DIR/linux-x86_64/libhttp.so
$KCDIST_DIR/linux-x86_64/libhttp.h
$KCDIST_DIR/linux-x86_64/libnetl.so
$KCDIST_DIR/linux-x86_64/libnetl.h
```

A dynamically linked Windows build typically uses `libhttp.dll.a` and `libnetl.dll.a` at link time, and the corresponding `.dll` files at runtime. For macOS the dynamic library is generally `.dylib`. Android commonly uses `.so`. Static compositions instead link the matching target's `.a` archives. Emscripten `wasm/wasm32` archives contain WASM-compatible object files, and its `.so` files are side modules rather than native ELF/PE/Mach-O shared libraries. For iOS and iOS Simulator, packaging/signing and loader restrictions require an explicitly supported deployment model.

Treat the library header and binary as one compatible interface. Confirm the ABI, compiler target, and any platform-specific link requirements. Do not link a target against host libraries or silently switch between static and dynamic linking. If the exact target's artifacts are absent, fail and identify the missing package and target.

## LV1 build and publication

The LV1 project's `Makefile` performs cross-compilation for its own sources, using kclib's target naming and output convention:

```text
proj/http-server/bin/linux/x86_64/http-server
proj/http-server/bin/windows/x86_64/http-server.exe
```

These paths are examples, not files already built.

The project's `manifest.json` declares its dependencies by their logical package names (for example `libhttp` and `libnetl`). The published artifact manifest provides the LV1 package's own files and their checksums. **Do not duplicate dependent kclib artifacts in the LV1 package.**

`KCDIST_DIR` is the shared root for both precompiled LV0 dependency input and kcbin publication output. Builds use only existing target-specific artifacts; `scripts/dist.sh` publishes the LV1 project's outputs without building or altering LV0 packages.

## Runtime discovery

Link-time success does not make a dynamically linked installed program runnable. A shared library may need an explicit runtime search path even when all kclibs are published together for one platform-architecture. Static compositions such as `semidx` incorporate the required kclib object code at link time and do not need separate kclib shared libraries at runtime.

On Linux, the executable may need a suitable relative RPATH/RUNPATH or a launcher-configured library search path. On macOS, loader paths and any signing requirements need to be satisfied. On Windows, the installed DLL directory must be in the executable's permitted DLL search locations. Android and Apple mobile targets have additional packaging restrictions.

A future general-purpose `kc` runner or package manager could resolve manifest dependencies, choose the correct target, install shared libraries separately and arrange runtime loader paths. **The current `kc` implementation does not yet provide complete automatic dependency installation or loader configuration**, and kcbin packages must not require `kc` to build.

## Next step

Return to the [kcbin README](../../README.md).
