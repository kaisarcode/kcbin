# C API

The library builds self-contained maps from ID/text associations and searches
them without a server. Applications own the input data and decide what to do
with the ranked results. All public declarations are in
[`libembdex.h`](../src/libembdex.h).

## Build and search

A map is created once and may be opened for repeated queries:

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

int main(void)
{
    const kc_embdex_document_t documents[] = {
        {"find", "find files by name"},
        {"grep", "search text inside files"}
    };
    kc_embdex_t *index = NULL;
    int status = kc_embdex_build("commands.bin", documents, 2);

    if (status == KC_EMBDEX_OK)
        status = kc_embdex_open(&index, "commands.bin");
    if (status == KC_EMBDEX_OK)
        status = kc_embdex_search(index, "find files by name",
            NULL, show, NULL);

    kc_embdex_close(index);
    return status == KC_EMBDEX_OK ? 0 : 1;
}
```

`kc_embdex_build()` borrows the input strings for the duration of the
call, writes the complete map to the specified path, and releases its
temporary build resources. IDs are not unique keys: each association
remains independent even when several entries share an ID.

`kc_embdex_open()` returns an index handle that can be searched multiple
times until `kc_embdex_close()` is called. Close also accepts NULL.

## Build with dictionaries

`kc_embdex_build_with_dictionaries()` adds an ordered list of dictionary
files and optional progress notifications:

```c
const char *dictionaries[] = {"spanish.json", "regional.json"};
int status = kc_embdex_build_with_dictionaries(
    "commands.bin", documents, count, dictionaries, 2, NULL);
```

Each dictionary is a JSON array of source/target string pairs.
Dictionary files must exist while building. They are merged into the
binary, so opening and searching requires only the resulting map.
Pass NULL and zero for the dictionary array and count to build without
substitutions.

## Build progress

A callback can observe the build without controlling its implementation:

```c
static void progress(kc_embdex_build_stage_t stage,
    size_t completed, size_t total, void *userdata)
{
    (void)userdata;

    if (stage == KC_EMBDEX_BUILD_EMBED && total != 0)
        printf("%.1f%%\n", 100.0 * (double)completed / (double)total);
}

int build_with_progress(const kc_embdex_document_t *documents, size_t count)
{
    kc_embdex_build_options_t options = {
        .progress = progress,
        .userdata = NULL
    };

    return kc_embdex_build_with_dictionaries(
        "commands.bin", documents, count, NULL, 0, &options);
}
```

The callback receives these phases in order:

| Phase | Meaning |
| :--- | :--- |
| `KC_EMBDEX_BUILD_PREPARE` | Initialize the index and prepare lexical data |
| `KC_EMBDEX_BUILD_EMBED` | Generate embeddings; `completed` advances from 0 to `total` |
| `KC_EMBDEX_BUILD_SAVE` | Serialize and write the binary map |
| `KC_EMBDEX_BUILD_DONE` | Map saved successfully |

Outside the embedding phase, `completed` and `total` are zero.
Notifications run synchronously in the calling thread. The callback
does not return a cancellation status; `DONE` is not emitted if the build
fails. Pass NULL for the options argument to disable notifications.

The API reports every completed association during embedding.
The CLI independently chooses how frequently to display that progress.

## Search results

`kc_embdex_search()` delivers one borrowed `kc_embdex_result_t` at a
time, ordered by score. A result contains the original association ID,
content, and ranking score. It does not transfer ownership to the
caller. Copy strings that must outlive their callback.

`kc_embdex_search_options_t` accepts an optional pointer to `limit`.
Omitting the options sets a default of 10 results. A limit of zero
delivers none. Returning a nonzero value from a result callback stops
delivery successfully; it does not turn the search into an error.

The search strategy is automatic. The library returns independent ranked
candidates, not a final classification or a sequence of operations.

## Errors and persistence

The public functions return `KC_EMBDEX_OK` on success or a negative
status on failure. `kc_embdex_strerror()` provides a static description
of a status.

| Status | Meaning |
| :--- | :--- |
| `KC_EMBDEX_EINVAL` | Invalid argument |
| `KC_EMBDEX_ENOMEM` | Allocation failure |
| `KC_EMBDEX_EIO` | File I/O failure |
| `KC_EMBDEX_EFORMAT` | Invalid or incompatible binary map |
| `KC_EMBDEX_EEMBED` | Embedding failure |
| `KC_EMBDEX_EINDEX` | Vector index failure |
| `KC_EMBDEX_ESTATE` | Invalid index state |
| `KC_EMBDEX_ELEXICAL` | Lexical processing failure |

Building replaces the destination file and is not an atomic transaction.
Applications needing uninterrupted readers should build under a separate
filename and publish the completed map after success.

`kc_embdex_version()` reports the generated build timestamp; it is
separate from the binary format version. During development, only the
current V1 layout is supported, so maps must be rebuilt when it changes.

## Next step

Read [Retrieval](advanced/retrieval.md) for the candidate-selection and
ranking behavior behind the API.
