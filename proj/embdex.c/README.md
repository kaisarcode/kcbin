# embdex.c - Text association retrieval

`embdex` is a candidate narrower. It compiles ID/text associations into
binary maps and reduces a large search space to a ranked subset of candidates
for each query. Maps contain their own indexes and optional term dictionaries,
so applications can search them locally without the original input files,
a database, or a server.

Search checks literal terms and phrases first, then uses approximate matching
and semantic embeddings when needed. Each association remains independently
searchable; repeated IDs are allowed.

The returned candidates are not definitive answers. A query outside the corpus
can still produce results, even when none is correct. Scores express relative
ranking evidence, not confidence or a probability of correctness. The caller
is responsible for any filtering, grouping, disambiguation, or final selection.

## Quick start

Build a map from a JSON array of ID/text associations, then search it:

```sh
printf '[{"id":"find","text":"find files by name"}]\n' | embdex build commands.bin
embdex search commands.bin "find files by name"
```

Search prints a JSON array of ranked associations with ID, score, and text.
The map can be reused for later queries.

For larger collections, provide a JSON file and optional dictionaries:

```sh
embdex build commands.bin -i corpus.json -d dictionary.json --progress
embdex search commands.bin "search for a file" --limit 10
```

A dictionary relates arbitrary UTF-8 source/target terms, including phrases, numbers,
and symbols. It is compiled
into the map. `--progress` shows build stages and embedding completion on
standard error; without it, building is silent.

## Documentation

- [Preparing data](doc/data-preparation.md) - design, generate, and test corpora and dictionaries for any domain.
- [CLI](doc/cli.md) - corpus and dictionary input, commands, and progress.
- [C API](doc/api.md) - build, search, callbacks, and ownership.
- [Retrieval](doc/advanced/retrieval.md) - candidate discovery and ranking.
- [Binary index](doc/advanced/index-format.md) - persistence and memory layout.

## JavaScript

The WASM target publishes `libembdex.js` (public ES module),
`libembdex.mjs` (Emscripten loader), `libembdex.wasm`, and the
precompiled static archive. The public JavaScript module implements the
project's projection without exposing pointers or raw callbacks.

```js
import { embdex } from "./libembdex.js";
const map = embdex.build([{id: "find", content: "find files"}]);
```

Use `make test wasm` to run both the C and JavaScript contract tests.
File-backed operations use the Emscripten virtual filesystem; JavaScript
callers can pass portable binary values without configuring native paths.

## Build

From the project directory, point `KCDIST_DIR` to a precompiled kclib
distribution containing matching artifacts for libemb, libhnsw, libbm25,
libmmap, libngram, and libleven:

```sh
export KCDIST_DIR="$HOME/kclib/dist"
make
make test
```

The directory shown is an example; use the location of your distribution.
`make` builds for the native platform. `make all` builds the configured
cross-platform targets when the corresponding toolchains and precompiled
dependencies are available. The project uses the supplied kclib artifacts
without rebuilding them.

## Notice

No guarantees are provided regarding stability or future support. You may
test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
