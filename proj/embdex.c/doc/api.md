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

The public operations are kc_embdex_build,
kc_embdex_build_with_dictionaries, kc_embdex_open, kc_embdex_search,
kc_embdex_close, kc_embdex_strerror, and kc_embdex_version.
There are no public search modes.

## Retrieval

Search automatically combines independent candidate discovery:

- BM25 ranks matching terms from the full query.
- The saved BM25 vocabulary supplies normalized exact term matches and
    postings, with no separate dictionary or Unicode analyzer.
- Unmatched terms may be compared with indexed words by bounded Unicode
    Levenshtein distance. Short terms and ambiguous single-word corrections
    are rejected.
- libngram finds complete normalized phrases and overlapping word windows.
- libemb obtains one embedding for queries requiring semantic retrieval.
    Cosine similarity scans small indexes directly; HNSW handles larger ones.

All evidence is accumulated under the internal document ordinal.
When whole associations match contiguous spans of a query, a bounded
literal-covering pass finds the two highest-scoring segmentations.
Longer phrases and fewer fragments are preferred, while component words
remain independently eligible for retrieval. For example, a map with
`quiero`, `tomar`, `agua`, and `tomar agua` favors the association
`tomar agua` in the full covering `quiero + tomar agua` over separate
word hits `quiero + tomar + agua`. Dictionary-normalized spans also
participate, with original-text literals breaking ties.

The coverings contribute ranking evidence to the associations. The public
search API still returns a flat, ranked set of independent associations,
not an orchestration graph or an array of grouped paths.

The engine otherwise first decides whether the evidence is sufficient,
then ranks accepted associations. BM25 supplies discovery order; a BM25
hit alone cannot turn an unrelated association into an accepted result.

Lexical coverage is weighted by the rarity of the original normalized
query words. Terms absent from the index are assigned singleton rarity
rather than an artificial maximum. Approximate matches do not alter
the document frequency of the original term, and repeated evidence
for one query position is counted once.

A complete phrase, strong multi-term lexical coverage or independently
strong semantic similarity can justify acceptance. Additionally, an
unambiguous spelling correction supported by exact terms can qualify
without relying on semantic inference, and a candidate close to the
top semantic rank can qualify when corroborated by lexical evidence.
A semantic candidate ranked far below the top cannot bypass the
query-relative threshold merely by having a BM25 hit.

For smaller indexes the semantic rank is computed from all indexed
cosine similarities; for larger indexes it comes from HNSW's candidate
ordering. The relative similarity threshold comes from a bounded sample
of the corpus, and no search mode is exposed.

For a single requested result, a complete normalized phrase match can
avoid model inference. Queries with insufficient evidence can return
no results. These rules still cannot guarantee correct interpretation
of contradictions or negations, and the quality of the embedding model
strongly affects paraphrase retrieval, particularly in Spanish.

The current acceptance boundaries and score weights are provisional
implementation policy. They require validation against positive and
negative queries; the diagnostic trace is available for that purpose.

### Diagnostic trace

Set `KC_EMBDEX_TRACE` to a writable JSON Lines file to append the
evidence of every association considered by each search:

```sh
KC_EMBDEX_TRACE=/tmp/embdex-trace.jsonl \
    embdex search corpus.bin "peer authentication" --limit 5
```

The opt-in trace includes corpus ordinals, IDs, original content,
bitmasks of lexical and phrase matches, BM25 and semantic ranks, measured cosine
similarities, weighted coverage, acceptance reasons, scores and ranks
before result-limit truncation. It is intended for benchmarks, not
production logging: it includes source text and queries, and is appended
to the specified file. Failure to write diagnostics does not affect
search results. It does not add a public API operation.

The index stores normalized terms and postings inside BM25's persistent
section. Search builds temporary word coverage and phrase evidence only
for the duration of each call. A query's lexical analysis is limited to
its first 64 tokens to bound worst-case work; semantic inference, when
used, still receives the complete original query. The entire input is
checked for valid UTF-8 before lexical retrieval.

Indexes up to 256 associations use exact cosine and skip a resident
HNSW graph; larger indexes construct HNSW at open time.

## Dictionary-compiled maps

A compiled map can be built from one dataset and zero or more independent
word-substitution dictionaries. Each dictionary is a UTF-8 TSV file with
one source word and one replacement word per line:

```text
claves	passwords
claves	keys
```

The input order matters: the first replacement for each source word is
used for document embeddings and the additional normalized BM25 index.
Further replacements become alternatives when processing queries.
Dictionary files can overlap; duplicate pairs are ignored, while
different replacements for the same source word remain available.
No language identifier, locale, grammar or word meanings are required.

```sh
embdex build mapa.bin -i asociaciones.tsv \
    -d spanish.tsv -d rioplatense.tsv
embdex search mapa.bin "guardar mis claves"
```

The output binary embeds its merged word dictionary, original associations,
their normalized vectors, and both original and substituted lexical indexes.
Opening and searching the map requires only the binary, even when the
original TSV files have been deleted. Independent maps can use entirely
different datasets and dictionary combinations.

The C API also accepts explicit dictionary inputs:

```c
const char *dictionaries[] = {"spanish.tsv", "rioplatense.tsv"};
int status = kc_embdex_build_with_dictionaries(
    "mapa.bin", documents, document_count, dictionaries, 2);
```

The existing `kc_embdex_build()` remains available for maps that do not
specify dictionary inputs. An older `KC_EMBDEX_WORDMAP` environment
configuration is still recognized for compatibility with experimental
indexes, but explicit dictionary files are the preferred build interface.

This is literal substitution rather than sentence translation. Unmatched
words are preserved. Queries can test up to 16 deterministic combinations
when words have alternative replacements. If the complete combination set
fits the budget it is enumerated; otherwise candidate combinations are
distributed across the ambiguous positions. Duplicate resulting strings
are embedded only once.

With multiple query variants, semantic ranking combines the highest cosine
with reciprocal-ranking agreement across variants. Both BM25 structures
also contribute lexical evidence. A map is an approximate candidate
narrower for downstream classification; it does not assign final meaning
to a word or guarantee that an arbitrary query has a correct result.

Set `KC_EMBDEX_WORDMAP_VARIANTS` to change the variant budget (default 8,
maximum 16). `KC_EMBDEX_TRACE` records candidate evidence, including
`mapped`, `mapped_bm25_rank`, `composition_rank`,
`literal_starts`, `mapped_starts`, `fusion_rrf`, and `variant_scores`.
`KC_EMBDEX_WORDMAP_TRACE` records the actual substituted query strings.
These traces are optional and contain the input text.

## CLI

Build reads tab-separated ID/content associations from standard input
or a file. Optional `-d` / `--dictionary` flags are repeatable and
provide independent dictionary files in priority order:

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
