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

A map can be compiled from a dataset and multiple independent dictionaries:

```sh
embdex build mapa.bin -i asociaciones.tsv \
    -d spanish.tsv -d rioplatense.tsv --progress
embdex search mapa.bin "guardar mis claves"
```

Dictionaries contain word-to-word replacements, including alternative
replacements for the same word. The binary embeds their merged contents,
the original associations, and the precomputed retrieval structures.
Search only needs the map; it does not load the input TSV files or
interpret language or regional metadata. All builds produce the same binary
format, with or without dictionaries. The optional `--progress` flag
prints build stages and refreshes embedding progress every 1% on a single
terminal line through standard error.

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

The retrieval strategy is automatic. embdex uses BM25's Unicode analyzer
and indexed vocabulary for exact terms, uses libleven for bounded UTF-8
spelling recovery, finds phrases with libngram, and also retrieves semantic
candidates through libemb. Candidate evidence is joined by insertion ordinal.
Query coverage, phrase completeness, spelling uncertainty, BM25 rank and
semantic similarity influence the final ranking. Matches without sufficient
evidence are rejected rather than returning the nearest neighbor unconditionally.

Search evaluates literal terms and n-grams first. A complete query
covered by a phrase or a contiguous combination of indexed associations
returns ranked lexical candidates without generating query embeddings.
Partial coverage triggers semantic fallback. Dictionary substitutions
also provide normalized literal matches, and ambiguous substitutions
can generate a bounded set of embeddings when needed. Indexes with up
to 256 associations use exact cosine similarity and skip HNSW; larger
indexes construct HNSW when opened.

The lexical analyzer folds accents, and spelling correction considers only
words in the dataset's own vocabulary. Its edit-distance limits and the
semantic relevance thresholds are fixed initial policies; relevance and
false-positive rates depend on the collection and should be measured against
representative queries. The public API does not expose search modes.

## Build

Use a precompiled kclib distribution containing target-matched libemb,
libhnsw, libbm25, libmmap, libngram and libleven artifacts:

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
