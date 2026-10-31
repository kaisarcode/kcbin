# semidx - Semantic text indexing

semidx builds portable indexes from text associations and searches them using
semantic embeddings, BM25 lexical ranking, or both. Each entry has an ID and
text content. IDs may repeat, and each association remains independently
searchable. Indexes work locally without a database or server.

## Quick start

Build an index from tab-separated ID and content lines:

```sh
printf 'smile\tLaughter, joy, humor and something funny\n' |
    semidx build icons.bin
```

Search the saved index:

```sh
semidx search icons.bin "that made me laugh"
semidx search icons.bin "smile" --mode hybrid
```

Results are tab-separated lines containing ID, score, and original content.
The default semantic mode reports cosine similarity. Lexical mode reports BM25
scores; hybrid mode reports reciprocal-rank-fusion scores; rerank mode reports
cosine similarity for BM25-selected candidates.

## Native library

An application can pass an in-memory array of associations directly:

```c
#include "libsemidx.h"

kc_semidx_document_t corpus[] = {
    {"document", "Authentication between peers"},
    {"document", "Exchanging public keys"}
};
kc_semidx_t *index = NULL;
kc_semidx_result_t *results = NULL;
size_t count = 0;

if (kc_semidx_build(&index, corpus, 2) == KC_SEMIDX_OK) {
    kc_semidx_save(index, "corpus.bin");
    if (kc_semidx_search(index, "secure connections", NULL,
        &results, &count) == KC_SEMIDX_OK) {
        kc_semidx_free(results);
    }
    kc_semidx_close(index);
}
```

The library exposes build, save, open, search, search_mode, and close. The
index owns its associations, so the source array may be released after
building. Applications choose their own document boundaries and preprocessing.
The existing search API remains semantic; search_mode selects the other
ranking strategies.

## Build

Set `KCDIST_DIR` to a precompiled kclib distribution containing matching
`libemb`, `libhnsw`, `libbm25`, and `libmmap` artifacts and headers for the target:

```sh
export KCDIST_DIR="$HOME/kclib/dist"
make
make test
make all
make test wine
make test wasm
```

The default `make` builds the native target. `make all` builds the supported
cross-compilation targets, including `wasm/wasm32` with Emscripten.
`make test wine` runs Windows contract tests through Wine after the Windows
target has been built; `make test wasm` runs the library contract tests with
Emscripten and Node.js. The project keeps test binaries in `.build/`,
outside the distributable `bin/` tree.

From the kcbin repository root, use the standard build, test, and distribution
scripts to generate artifact checksums and publish the project:

```sh
export KCDIST_DIR="$HOME/kclib/dist"
./scripts/build.sh semidx
./scripts/test.sh semidx
./scripts/dist.sh semidx
```

The distribution script publishes only semidx artifacts and metadata to the
shared distribution root. It never compiles or copies the dependent kclibs.
All targets link against the target-matched precompiled `libemb.a`,
`libhnsw.a`, `libbm25.a`, and `libmmap.a` from `KCDIST_DIR`; no separate kclib shared
libraries are required at runtime. Every target also produces `libsemidx.a`
alongside its existing shared library or WASM module. This static archive
contains semidx's C code; applications linking it must also link the three
precompiled kclib archives, using a C++ linker for the GGML code in `libemb`.
The WASM build produces `libsemidx.wasm` and `libsemidx.a`.
Native CLI executables are linked with `libsemidx.a` and the four kclib
static archives, so they do not need `libsemidx.so` or kclib shared libraries
at runtime. The shared library is still published for other consumers.

## Documentation

- [API and CLI](doc/api.md) - usage, associations, options, and lifecycle.
- [Binary index](doc/index-format.md) - portability and persistence contract.

## Notice

No guarantees are provided regarding stability or future support. You may test,
use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
