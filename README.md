# kcbin

**Composable level-1 tools built from KaisarCode primitives**

kcbin is a collection of free-software tools that combine independent kclibs into reusable capabilities. kclibs are level-0 (LV0) primitives; each kcbin project is a level-1 (LV1) composition and declares its LV0 dependencies explicitly.

Projects are individually buildable and distributable. Depending on their purpose, they may provide a command-line executable, a native shared library with an API, or a flow-based tool. kcbin is not an application framework, and it does not require the `kc` command.

The **[Project catalog](doc/catalog.md)** lists the available tools and links to their own documentation.

## Build and test

Build or test every available project:

```sh
./scripts/build.sh
./scripts/test.sh
```

For a specific project, once added:

```sh
./scripts/build.sh NAME
./scripts/test.sh NAME
./scripts/dist.sh NAME
```

Each project also defines its own `make` targets and supported platforms. See [Build and distribution](doc/build.md).

## Documentation

- [Project catalog](doc/catalog.md) — discover the available LV1 tools.
- [Getting started](doc/getting-started.md) — add and build an independent project.
- [Package manifests](doc/manifest.md) — declare project dependencies and metadata.
- [Architecture](doc/advanced/architecture.md) — LV0/LV1 boundaries and composition rules.
- [Cross-compilation](doc/advanced/cross-compilation.md) — link against precompiled LV0 artifacts for each target.

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

kcbin is distributed under the **GNU General Public License version 3 (GPLv3)** for its repository infrastructure and root tooling.
Each nested project retains its own license, as documented in that project's README and license file.
