# API and CLI

embdex indexes independent ID/text associations and searches them with one
automatic retrieval strategy. The caller provides content and a query;
normalization, ranking, candidate handling, and resource management belong
to the library.

## Associations

Each entry has an ID and UTF-8 content string. IDs are not unique keys:
repeated IDs represent separate associations. The source application
determines how to split documents and what IDs mean.

## C API

```c
#include "libembdex.h"

kc_embdex_document_t documents[] = {
    {"icon-laugh", "Humor, laughter and something amusing"},
    {"icon-laugh", "Joy and a cheerful smile"},
    {"icon-alert", "Warning, danger and caution"}
};

int status = kc_embdex_build("icons.bin", documents, 3);
```

Building writes the index directly to the specified path. Input strings
are borrowed only for the call. There is no separate save operation.

To query the saved index:

```c
#include "libembdex.h"
#include <stdio.h>

static int show(const kc_embdex_result_t *result, void *userdata)
{
    (void)userdata;
    printf("%s\t%.6f\t%s\n",
        result->id, result->score, result->content);
    return 0;
}

int search_icons(void)
{
    kc_embdex_t *index = NULL;
    size_t limit = 5;
    kc_embdex_search_options_t options = { .limit = &limit };
    int status = kc_embdex_open(&index, "icons.bin");

    if (status == KC_EMBDEX_OK) {
        status = kc_embdex_search(index, "something funny",
            &options, show, NULL);
    }
    kc_embdex_close(index);
    return status;
}
```

A callback receives one borrowed result at a time, already ordered by
ranking score. Its result pointer and strings remain valid only during
the invocation; copy them if they must outlive it. Return zero to keep
receiving matches, or nonzero to stop delivery successfully.

An omitted limit defaults to ten. A limit of zero delivers no matches.
Results are not caller-owned and require no free operation. The opened
index remains usable for additional searches until closed.

The public operations are kc_embdex_build, kc_embdex_open,
kc_embdex_search, kc_embdex_close, kc_embdex_strerror, and
kc_embdex_version. There are no public search modes.

## Retrieval

The current implementation independently retrieves BM25 and HNSW
candidates, joins them by internal ordinal, and merges their rankings
with reciprocal-rank fusion. Semantic candidates below an initial
cosine-similarity cutoff are excluded. The cutoff and fusion are
provisional, not a validated universal relevance policy.

Further stages will add vocabulary-based spelling recovery, phrase
matching, evidence accumulation, and resource-aware candidate selection.
Those stages do not change the intent-level public API.

## CLI

Build reads tab-separated ID/content associations from standard input
or a file:

```sh
embdex build corpus.bin -i documents.tsv
printf 'doc-1\tAuthentication between peers\n' | embdex build corpus.bin
```

The first tab separates ID from content. Blank lines are ignored;
nonblank lines must contain nonempty ID and content. Embedded tabs,
line breaks and binary NUL bytes are not supported by the text CLI.
The C API does not require TSV.

Search a saved index:

```sh
embdex search corpus.bin "peer authentication"
embdex search corpus.bin "security" --limit 5
```

Each output row contains ID, score and content separated by tabs.
Diagnostic messages go to standard error, with a nonzero exit status
for invalid arguments or failed operations. -h/--help and
-v/--version follow the standard kclib CLI conventions.

## Next step

Continue with the [Binary index](index-format.md).
