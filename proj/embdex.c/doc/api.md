# API and CLI

embdex indexes ID/text associations and supports semantic, lexical, hybrid,
and lexical-first reranked searches. Embeddings and lexical postings are
generated internally.

## Data contract

Every association contains an ID string and a content string. IDs are not keys:
multiple entries may use the same ID and each remains independently searchable.
The application decides which text to index, whether to split documents, and
what an ID represents. embdex does not read source document formats.

## C API

```c
#include "libembdex.h"

kc_embdex_document_t corpus[] = {
    {"icon-laugh", "Humor, laughter and something amusing"},
    {"icon-laugh", "Joy and a cheerful smile"},
    {"icon-alert", "Warning, danger and caution"}
};

kc_embdex_t *index = NULL;
int status = kc_embdex_build(&index, corpus, 3);
if (status == KC_EMBDEX_OK) {
    status = kc_embdex_save(index, "icons.bin");
}
kc_embdex_close(index);
```

To query the stored index:

```c
kc_embdex_t *index = NULL;
kc_embdex_result_t *results = NULL;
size_t count = 0;
size_t limit = 5;
double threshold = 0.4;
kc_embdex_search_options_t options = {
    .limit = &limit,
    .threshold = &threshold
};

if (kc_embdex_open(&index, "icons.bin") == KC_EMBDEX_OK) {
    if (kc_embdex_search(index, "something funny", &options,
        &results, &count) == KC_EMBDEX_OK) {
        for (size_t i = 0; i < count; i++) {
            printf("%s: %.3f\n", results[i].id, results[i].score);
        }
        kc_embdex_free(results);
    }
    kc_embdex_close(index);
}
```

The default search returns up to ten entries with a minimum cosine score of
-1. Set limit to zero to request no results. The cosine threshold must be in
the range -1 to 1. Results own their array, but result strings borrow storage
from the open index. Close the index only after consuming the results.

An index created with build may be searched immediately and saved. An index
loaded with open is read-only and may be searched repeatedly.

## Search modes

The existing `kc_embdex_search()` remains semantic. Use
`kc_embdex_search_mode()` with a mode constant for another strategy.

| Mode | Ranking | Candidate selection |
| :--- | :--- | :--- |
| `KC_EMBDEX_MODE_SEMANTIC` | Cosine similarity via HNSW | Vector neighbors |
| `KC_EMBDEX_MODE_LEXICAL` | Okapi BM25 | Matching lexical terms |
| `KC_EMBDEX_MODE_HYBRID` | Reciprocal rank fusion | Union of BM25 and HNSW |
| `KC_EMBDEX_MODE_RERANK` | Cosine similarity | BM25 candidates only |

Hybrid mode retrieves up to 100 candidates per source, or the requested limit
if larger, and sums `1 / (60 + rank)` for each source where a document appears.
Ranks start at one. Hybrid scores are RRF scores, not BM25 or cosine scores.
Rerank mode selects the same candidate window with BM25, then compares the
query embedding against those documents' stored vectors directly. Documents
excluded by BM25 cannot be recovered by reranking.

The default result limit is 10. The `threshold` option filters cosine
similarity in semantic and rerank modes and applies only to the HNSW branch
in hybrid mode. Lexical mode ignores this threshold. The public result fields
remain ID, content, and score. Repeated public IDs remain distinct through
internal insertion ordinals; exact score ties use ordinal order.

```c
kc_embdex_result_t *results = NULL;
size_t count = 0;
size_t limit = 5;
kc_embdex_search_options_t options = {.limit = &limit};

if (kc_embdex_search_mode(index, "authentication",
    KC_EMBDEX_MODE_HYBRID, &options, &results, &count) == KC_EMBDEX_OK) {
    kc_embdex_free(results);
}
```

## JavaScript projection

The intended JavaScript-facing shape is:

```javascript
const index = await embdex.build([
    { id: "file-a", content: "Authentication between peers" },
    { id: "file-a", content: "Exchanging public keys" }
]);

await index.save("corpus.bin");
const matches = await index.search("secure peer communication", {
    mode: "hybrid",
    limit: 5
});
await index.close();

const restored = await embdex.open("corpus.bin");
const results = await restored.search("encryption");
await restored.close();
```

This is the conceptual projection of the C API. The native project does not
bundle a JavaScript engine or runtime-specific binding.

## CLI

Build reads one tab-separated pair per line from standard input or a file:

```sh
embdex build corpus.bin -i documents.tsv
printf 'doc-1\tAuthentication between peers\n' | embdex build corpus.bin
```

The first tab separates the ID and content. Blank lines are ignored. Each
nonblank row requires nonempty ID and content. Embedded tabs, embedded newlines,
and binary NUL bytes are not supported by the CLI text input. The library API
does not impose the tab-separated input representation.

Search an existing binary index:

```sh
embdex search corpus.bin "peer authentication"
embdex search corpus.bin "security" --limit 5 --threshold 0.3
embdex search corpus.bin "peer authentication" --mode lexical
embdex search corpus.bin "peer authentication" --mode hybrid
embdex search corpus.bin "peer authentication" --mode rerank
```

Each output line uses the same tab delimiter with fields
`id<TAB>score<TAB>content`. The default mode is `semantic`.
The CLI writes errors to standard error and exits nonzero on invalid
input, incompatible indexes, or failed operations.

## Next step

Continue with the [Binary index](index-format.md).
