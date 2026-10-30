# semidx - Semantic text indexing

semidx builds portable binary indexes from text associations and finds relevant
entries using natural-language queries. Each entry has an ID and text content.
IDs are arbitrary strings and may repeat. The library handles embeddings,
vector search, and persistence without a database or server.

## Quick start

Build an index from tab-separated ID and content lines:

```sh
printf 'smile\tLaughter, joy, humor and something funny\n' |
    semidx build icons.bin
```

Search the saved index:

```sh
semidx search icons.bin "that made me laugh"
```

Results are tab-separated lines containing ID, cosine similarity, and the
original indexed content. All entries are ranked independently, including
entries with the same ID.

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

The library exposes build, save, open, search, and close. The index
owns its associations, so the source array may be released after building.
Applications choose their own document boundaries and any preprocessing.

## Build

Set `KCLIB_DIST_DIR` to a precompiled kclib distribution containing matching
`libemb`, `libhnsw`, and `libmmap` artifacts and headers for the target:

```sh
KCLIB_DIST_DIR="$HOME/kclib/dist" make
KCLIB_DIST_DIR="$HOME/kclib/dist" make test
```

Use `make all` to request the configured cross-platform targets when their
toolchains and matching shared dependencies are available. semidx never
compiles or downloads kclibs. Runtime linking must locate those shared
libraries independently.

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
