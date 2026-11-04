# Build and distribution

kcbin follows the kclib monorepo's per-project build and publication conventions. Every implemented tool owns its Makefile, source, tests, metadata, and published artifacts.

## Build and test

Build or test all discovered projects, or select one by directory name:

```sh
./scripts/build.sh
./scripts/build.sh NAME
./scripts/test.sh
./scripts/test.sh NAME
```

Use `-l` with the build or test command for the full log. A project's Makefile must expose the supported `all` and `test` contracts. Projects are discovered by their `proj/NAME/Makefile`, not by a registry.

Build outputs go to `proj/NAME/bin/<platform>/<arch>/`. Individual Makefiles define the supported platforms and use installed LV0 artifacts/headers as explicit build dependencies; no project should rely on another LV1 package.

## Target-matched LV0 dependencies

Each kcbin project owns its target matrix, using the same platform/architecture naming and cross-toolchains as kclib. `make all` compiles the LV1 project's code for its supported targets; it does not build any kclib.

Set `KCDIST_DIR` to the shared distribution root containing the **existing** precompiled kclib artifacts. For example:

```sh
KCDIST_DIR="$HOME/kclib/dist" ./scripts/build.sh http-server
```

The example uses a proposed project name; no `http-server` implementation exists yet. Kclib artifacts are grouped by target: for example, `linux-x86_64/libhttp.so` and `linux-x86_64/libhttp.h` live alongside other libraries for the same architecture. Windows additionally needs its import library `.dll.a`. Fail when a required target-specific artifact is absent.

`KCDIST_DIR` is used for both **reading precompiled kclib dependencies** and **publishing kcbin artifacts**. The shared target directories may contain binaries from both repositories. The outputs of one project never embed independent LV0 packages. Full details: [Cross-compilation and dynamic linking](advanced/cross-compilation.md).

## WebAssembly JavaScript interfaces

A project supporting `wasm/wasm32` must publish its projected JavaScript
interface alongside its WebAssembly binary. A JavaScript consumer imports
`libNAME.js`; the `libNAME.mjs` generated runtime and `libNAME.wasm`
are implementation artifacts loaded by the public module. `make test wasm`
runs both the compiled C contract tests and JavaScript projection tests.

This uses the project's precompiled kclib dependencies for the same WASM
target. Dependencies are never rebuilt or modified by the consumer.

## Distribution

Publish the compiled artifacts of all projects or one project:

```sh
./scripts/dist.sh
./scripts/dist.sh NAME
```

Source `proj/NAME/manifest.json` files continue to declare project metadata and LV0 dependencies; they are not copied or transformed into the published distribution. The distribution is grouped by target, alongside the matching kclib artifacts:

```text
dist/
├── linux-x86_64/
│   ├── libemb.so
│   ├── libemb.h
│   ├── libembdex.so
│   ├── libembdex.h
│   ├── libroute.so
│   ├── libroute.h
│   ├── LICENSE
│   ├── COPYING
│   └── SHA256SUMS
└── wasm-wasm32/
    ├── libembdex.js
    ├── libembdex.mjs
    ├── libembdex.wasm
    ├── libroute.js
    ├── libroute.mjs
    ├── libroute.wasm
    ├── package.json
    ├── LICENSE
    ├── COPYING
    └── SHA256SUMS
```

Each `dist/<platform>-<arch>/` directory contains the binaries and public headers of all available libraries, with **one** `LICENSE`, **one** `COPYING`, and **one** `SHA256SUMS` shared by the target. The `SHA256SUMS` file is the only published artifact inventory. No README files, per-project directories or generated JSON manifests are published. `package.json` is shared by WASM projects.

`./scripts/dist.sh NAME` updates only that project's artifacts and leaves the others untouched. `./scripts/dist.sh all` merges all available projects into the target directories, removes old kcbin per-project layouts and JSON manifests, and detects unchanged files from build checksums without recopying them.

`KCDIST_DIR` selects an optional shared distribution directory. When it is set, the script leaves other repositories' packages untouched. Repository-local `dist/` is generated and ignored by source control.

There is no central `kcbin` package manifest, and the build and distribution tooling does not depend on `kc`.

## Next step

Continue with [Project catalog](catalog.md).
