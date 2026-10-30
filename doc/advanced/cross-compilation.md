# Cross-compilation and dynamic linking

kcbin uses the cross-compilation conventions of kclib while compiling **only the level-1 composition**. Its level-0 kclib dependencies are already compiled and are not vendored, rebuilt, or copied into kcbin's source tree.

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

Point `KCLIB_DIST_DIR` at an existing kclib distribution root. It contains the published per-project manifests, shared libraries, import libraries where applicable, and public headers for each target.

For a hypothetical `http-server` composition targeting `linux/x86_64`, the needed inputs include:

```text
$KCLIB_DIST_DIR/libhttp.c/linux/x86_64/libhttp.so
$KCLIB_DIST_DIR/libhttp.c/linux/x86_64/libhttp.h
$KCLIB_DIST_DIR/libnetl.c/linux/x86_64/libnetl.so
$KCLIB_DIST_DIR/libnetl.c/linux/x86_64/libnetl.h
```

A Windows build typically uses `libhttp.dll.a` and `libnetl.dll.a` at link time, and the corresponding `.dll` files at runtime. For macOS the shared artifact is generally `.dylib`. Android commonly uses `.so`. For iOS and iOS Simulator, packaging/signing and loader restrictions require an explicitly supported deployment model. The current WASM output is not a drop-in native shared library; do not use ordinary ELF/PE/Mach-O dynamic-link assumptions for `wasm/wasm32`.

Treat the library header and binary as one compatible interface. Confirm the ABI, compiler target, and any platform-specific link requirements. Do not link a target against host libraries or silently switch from shared to static linking. If the exact target's artifacts are absent, fail and identify the missing package and target.

## LV1 build and publication

The LV1 project's `Makefile` performs cross-compilation for its own sources, using kclib's target naming and output convention:

```text
proj/http-server/bin/linux/x86_64/http-server
proj/http-server/bin/windows/x86_64/http-server.exe
```

These paths are examples, not files already built.

The project's `manifest.json` declares its dependencies by their logical package names (for example `libhttp` and `libnetl`). The published artifact manifest provides the LV1 package's own files and their checksums. **Do not duplicate dependent kclib shared libraries in the LV1 package.**

`KCLIB_DIST_DIR` is the **input** root for precompiled LV0 dependencies. `KCDIST_DIR` is the optional **output** root used by `scripts/dist.sh`; it can be configured to a shared distribution but does not implicitly build or resolve LV0.

## Runtime discovery

Link-time success does not make an installed program runnable. A shared library may live in a separate package directory from the consuming executable.

On Linux, the executable may need a suitable relative RPATH/RUNPATH or a launcher-configured library search path. On macOS, loader paths and any signing requirements need to be satisfied. On Windows, the installed DLL directory must be in the executable's permitted DLL search locations. Android and Apple mobile targets have additional packaging restrictions.

A future general-purpose `kc` runner or package manager could resolve manifest dependencies, choose the correct target, install shared libraries separately and arrange runtime loader paths. **The current `kc` implementation does not yet provide complete automatic dependency installation or loader configuration**, and kcbin packages must not require `kc` to build.

## Next step

Return to [Architecture](architecture.md).
