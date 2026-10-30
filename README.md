# kcbin

**Reusable tools built by composing kclibs**

kcbin is a collection of free-software tools that combine independent kclibs into higher-level capabilities. Each project declares the kclibs it needs and can be built, tested, and distributed independently.

Depending on its purpose, a project may provide a command-line executable, a native shared library with an API, or a flow-based tool. kcbin is not an application framework and does not require the `kc` command.

The **[Project catalog](doc/catalog.md)** lists the available tools and links to their own documentation.

## Build and test

Build or test every available project:

```sh
./scripts/build.sh
./scripts/test.sh
```

For a specific project, once added:

```sh
export KCLIB_DIST_DIR="$HOME/kclib/dist"
./scripts/build.sh NAME
./scripts/test.sh NAME
./scripts/dist.sh NAME
```

Each project defines its own `make` targets and supported platforms. Builds use the precompiled kclibs in `KCLIB_DIST_DIR` without rebuilding them. See [Build and distribution](doc/build.md).

## Documentation

- [Getting started](doc/getting-started.md) — add and build an independent project.
- [Package manifests](doc/manifest.md) — declare project dependencies and metadata.
- [Build and distribution](doc/build.md) — build, test, and publish projects.
- [Project catalog](doc/catalog.md) — discover the available tools.
- [Architecture](doc/advanced/architecture.md) — project boundaries and composition rules.
- [Cross-compilation](doc/advanced/cross-compilation.md) — link against precompiled kclibs for each target.

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

kcbin is distributed under the **GNU General Public License version 3 (GPLv3)** for its repository infrastructure and root tooling.
All projects under `proj/` are distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**, independently of the GPLv3 license of the repository infrastructure.
