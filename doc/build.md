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

## Distribution

Publish the compiled artifacts of all projects or one project:

```sh
./scripts/dist.sh
./scripts/dist.sh NAME
```

A source `proj/NAME/manifest.json` becomes the `metadata` section of `dist/NAME/manifest.json`. The distribution script adds an `artifacts` inventory, checksums, and per-platform files. Source manifests declare LV0 dependencies; generated artifacts are target-specific.

`KCDIST_DIR` selects an optional shared distribution directory. When it is set, the script leaves other repositories' packages untouched. Repository-local `dist/` is generated and ignored by source control.

There is no central `kcbin` package manifest, and the build and distribution tooling does not depend on `kc`.

## Next step

Continue with [Project catalog](catalog.md).
