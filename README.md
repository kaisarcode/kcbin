# kclib

**Independent native primitives for the KaisarCode ecosystem**

kclib is a collection of reusable C libraries, each responsible for one concrete capability. Libraries are independently buildable and distributable. They do not depend on one another: applications combine capabilities through explicit composition.

The repository also includes independent native tools. It is not a monolithic framework or a shared runtime.

## Example

Build and test one library from the repository root:

```sh
./scripts/build.sh demo
./scripts/test.sh demo
```

Or build and test an individual project from its own directory:

```sh
cd proj/libdemo.c
make
make test
```

Find the right capability in the **[Project catalog](doc/catalog.md)**. Each package links to its own README and public interface.

## Documentation

- **[Project catalog](doc/catalog.md)** - browse every available capability and open its project README.
- [Getting started](doc/getting-started.md) - build and test your first project.
- [Build and distribution](doc/build.md) - repository-wide build, test, and distribution commands.
- [Scripting projection](doc/scripting-projection.md) - how C capabilities map to Lua and JavaScript.
- [Advanced documentation](doc/advanced/index.md) - repository architecture, project conventions, and maintenance.

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

kclib is distributed under the **GNU General Public License version 3 (GPLv3)** for its repository infrastructure and root tooling. Each nested project retains its own license, as documented in that project's README and license file.
