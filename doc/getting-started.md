# Getting started

Each kcbin tool is an independent project under `proj/`. The repository currently contains no implementation projects, so start by adding one project with a specific, reusable LV1 responsibility.

A typical native project contains:

```text
proj/NAME/
├── README.md
├── LICENSE
├── COPYING
├── Makefile
├── manifest.json
└── src/
```

Give the project a narrow purpose, then select the LV0 kclibs needed to implement it. License every project under `proj/` as LGPLv3, with its own `LICENSE` and `COPYING` files following the kclib format; declare `"license": "LGPL-3.0-only"` in its manifest. The repository root remains GPLv3. Document its public API or CLI and actual link and runtime requirements in its own README. Consume precompiled LV0 artifacts for each target and never invoke a kclib build from the LV1 Makefile. See [Cross-compilation](advanced/cross-compilation.md). Only publish target platforms that the project builds and tests.

Declare metadata and dependencies in `manifest.json`, using the [manifest guide](manifest.md). For a tool that needs two kclibs, neither kclib becomes dependent on the other: the LV1 project links or invokes both.

Build and test it from the repository root:

```sh
./scripts/build.sh NAME
./scripts/test.sh NAME
./scripts/dist.sh NAME
```

Or locally from `proj/NAME` with `make` and `make test`. A project Makefile controls its actual targets; the scripts provide common discovery and reporting.

## Next step

Continue with [Package manifests](manifest.md).
