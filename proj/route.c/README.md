# route.c - HTTP route selection

`route` matches request methods and normalized URI paths against ordered patterns. Its native API dispatches parsed requests to callbacks. Its CLI reads HTTP requests from stdin and invokes the command registered for each matching route, without opening sockets.

Routes use the portable `libregex.c` API for compiled expressions, capture groups and full-path matching. A callback can accept a match or return zero to continue to the next registered route. The C callback receives the complete borrowed `kc_http_request_t` alongside match captures. The CLI uses `libhttp.c` to parse incoming HTTP requests and `libregex.c` to match paths. Neither dependency is modified.

## Quick start

Select a route directly:

```sh
route match GET '/notes/42?view=full' \
    -r GET '/notes/([0-9]+)' note \
    -r ALL '/.*' fallback
```

The result is one JSON object:

```json
{"id":"note","method":"GET","path":"/notes/42","query":"view=full","captures":["42"]}
```

Unmatched requests print `null`. The tool also supports repeated `--route` options. Patterns and IDs are configuration supplied by the caller; the CLI does not execute destination commands.

## Documentation

- [C API and routing behavior](doc/api.md) - registry lifetime, callbacks, patterns and normalization.
- [CLI](doc/cli.md) - command syntax and output.

## Build

Set `KCDIST_DIR` to a precompiled kclib distribution containing `libhttp.c` and `libregex.c` for the selected target:

```sh
export KCDIST_DIR="$HOME/kclib/dist"
make
make test
make test wasm  # requires Emscripten, Node.js and WASM libhttp/libregex artifacts
```

The target matrix follows kclib: Linux, Windows, macOS, iOS, iOS Simulator, Android and WASM. Each target requires its toolchain and matching precompiled libhttp and libregex artifacts. iOS and iOS Simulator publish the native library only; WASM exports the native API without a command-line executable. These target recipes have not yet been verified by cross-compilation. The build does not rebuild or copy kclib dependencies.

## Notice

No guarantees are provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com). Pull requests are not accepted; the project avoids depending on GitHub as long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
