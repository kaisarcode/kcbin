# embdex.c - Text association retrieval

embdex builds portable indexes of text associations and retrieves relevant
matches from natural-language queries. Each association has an ID and content;
IDs may repeat, and each entry remains independently searchable.

The search strategy is automatic. The consumer does not select algorithms or
manage intermediate embeddings, search results, or index serialization.
Everything runs locally without a database or server.

## Quick start

Create an index from tab-separated ID/content pairs:

```sh
printf 'smile\tLaughter, joy, humor and something funny\n' |
    embdex build icons.bin
```

Search the saved index:

```sh
embdex search icons.bin "something funny"
embdex search icons.bin "smile" --limit 5
```

Output is tab-separated: ID, ranking score, and original content.

## Native library

The library accepts the associations directly. Building persists the index;
opening it allows repeated searches.

```c
#include "libembdex.h"
#include <stdio.h>

static int visit(const kc_embdex_result_t *result, void *userdata)
{
    (void)userdata;
    printf("%s: %s\n", result->id, result->content);
    return 0;
}

int main(void)
{
    const kc_embdex_document_t corpus[] = {
        {"document", "Authentication between peers"},
        {"document", "Exchanging public keys"}
    };
    kc_embdex_t *index = NULL;
    int status = kc_embdex_build("corpus.bin", corpus, 2);

    if (status != KC_EMBDEX_OK) return 1;
    status = kc_embdex_open(&index, "corpus.bin");
    if (status == KC_EMBDEX_OK)
        status = kc_embdex_search(index, "secure connections",
            NULL, visit, NULL);
    kc_embdex_close(index);
    return status == KC_EMBDEX_OK ? 0 : 1;
}
```
The callback receives borrowed results in score order. Returning nonzero
stops delivery; no result-release operation is needed. An optional limit
controls the maximum number delivered.

The current automatic ranking combines BM25 and embedding candidates with
reciprocal-rank fusion. Further spelling and phrase retrieval, evidence-based
selection, and adaptive execution remain planned; the ranking should not be
considered calibrated for all datasets.

## Build

Use a precompiled kclib distribution containing target-matched libemb,
libhnsw, libbm25 and libmmap artifacts:

```sh
export KCDIST_DIR="$HOME/kclib/dist"
make
make test
make all
make test wine
make test wasm
```

From the kcbin root:

```sh
./scripts/build.sh embdex
./scripts/test.sh embdex
./scripts/dist.sh embdex
```

The project links precompiled static kclibs for its supported targets; it
does not build or vendor them. See the root build documentation for the
cross-compilation and distribution contracts.

## Documentation

- [API and CLI](doc/api.md)
- [Binary index](doc/index-format.md)

## Notice

No guarantees are provided regarding stability or future support. You may
test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
