# kcbin repository instructions

kcbin is the KaisarCode monorepo for **level-1 (LV1) composable tools**. Each project lives in `proj/<name>/` and combines one or more independent **level-0 (LV0) primitives** from kclib. A kcbin project may expose an executable, a shared C library and public header, or an explicit flow-based tool, according to its actual purpose. These are free-software tools, not an application framework.

## Dependency levels

- LV0 kclibs are autonomous primitives and never depend on other kclibs.
- LV1 kcbin packages may depend on LV0 primitives and external system libraries.
- An LV1 package must not depend on an LV1 package or anything at a higher level.
- Generally, a package at level N may depend only on packages with level lower than N. A project's effective level is one plus its highest KaisarCode dependency level.
- Do not reclassify an LV1 composition as a kclib, and do not add any kcbin dependency to kclib.
- Declare all KaisarCode dependencies explicitly in each project's `manifest.json`; do not silently install them or copy them into the source tree.

## Project structure

Each independently distributable project owns its `README.md`, `Makefile`, `manifest.json`, `src/`, tests, and LGPLv3 license files (`LICENSE` with the LGPLv3 text and `COPYING` with the accompanying GPLv3 text, matching kclib project conventions). Use `doc/` only when the project requires more documentation. A project may provide a native CLI, shared library, or both, without an obligatory common API.

Source metadata fields are `name`, `description`, `author`, `license`, `website`, `repository`, `level`, and `dependencies`. For LV1, use `"level": 1` and a nonempty `"dependencies"` array of names identifying LV0 kclibs. See `doc/manifest.md`. The distribution script retains source metadata and adds built artifact hashes. The current project-manifest consumer does not yet install declared dependencies automatically; never claim it does.

Use existing `kclib` public APIs according to their README, public headers, and documentation on the selected branch. Do not assume that a kclib catalog is available at a historical path. Any connection between APIs, CLI processes, and .flow files belongs at the composition boundary.

## Build, test, distribution

Follow the kclib monorepo conventions:

- `scripts/build.sh [all|NAME]` invokes each project's `make all`.
- `scripts/test.sh [all|NAME]` runs project-local tests; standard output includes named `[PASS]` cases where applicable.
- `scripts/dist.sh [all|NAME]` publishes project-scoped distribution manifests and verified artifacts into the ignored `dist/`.
- Project build outputs belong in `proj/<name>/bin/<platform>/<arch>/`; source metadata belongs in `proj/<name>/manifest.json`.
- `KCDIST_DIR` may select a shared distribution root; never prune packages belonging to other repositories.

The scripts discover projects by `Makefile`; do not hardcode an inventory. Each project defines its own supported target matrix and link flags.

## Cross-compilation and linking

Use the same platform/architecture names, cross-compilers, toolchains, SDKs, sysroots and output layout as the relevant kclib projects. An LV1 project declares and builds **only its own supported targets**, and should follow the corresponding kclib Makefiles for their platform configuration.

**Never build, rebuild or vendor LV0 kclib sources as a side effect of a kcbin build.** Consume target-specific precompiled shared libraries and matching public headers. Build-time dependency input is the existing kclib distribution (configure its root with `KCLIB_DIST_DIR`; `KCDIST_DIR` is the separate output publication root). Require the exact `platform/arch`; missing or incompatible dependencies must fail with a clear error rather than searching host libraries, silently changing target, or falling back to static linking.

Linux and Android normally link with `.so`; macOS uses `.dylib`; Windows links to `.dll.a` and needs the corresponding `.dll` at runtime. Other targets, especially iOS and WASM, require explicit confirmation that the selected kclib artifacts support the intended link/runtime model. Do not claim cross-compilation support solely because a kclib has a matching target directory.

The package manifest declares dependency names, while the build selects actual target-specific artifacts. Publish only the LV1 project's own outputs; do not bundle a separate copy of each kclib dependency. Distinguish build-time library search paths from deployment-time dynamic loader resolution. A runner/package manager could arrange runtime discovery but is **not currently required or guaranteed**; see `doc/advanced/cross-compilation.md`.

## Source control and documentation

Use the branch selected by the user; do not create branches, tags, or refs without a request. Keep root ignore patterns at the monorepo root. The repository root infrastructure and root tooling are GPLv3; **all** projects under `proj/` are LGPLv3, use `"license": "LGPL-3.0-only"` in their source manifests, and document LGPLv3 in their project READMEs. Do not inherit the repository's GPLv3 license for a nested project.

Keep `README.md` focused on first use; put longer architecture and build details under `doc/`. The canonical project inventory is `doc/catalog.md`, linking to self-contained project README files. Do not create placeholder packages solely to fill the catalog.

The `kc` command and package-manager design are independent, still evolving concerns. Do not make kcbin builds, packages, or manifests require `kc`, and do not describe `kc` as specific to kclibs.
