# Retrieval

embdex retrieves independent associations using lexical, spelling, phrase,
and semantic evidence. It returns ranked candidates for applications to
interpret; it does not identify a single definitive intent or invoke tools.

The embedding component currently uses the embedded English-oriented
`bge-small-en-v1.5` model. Corpus design and optional build-time dictionaries
provide the application-specific associations that are searched.

## Candidate discovery

Search combines these independent retrieval mechanisms:

- BM25 ranks matching terms from the full query.
- The saved BM25 vocabulary supplies normalized exact term matches and
    postings, with no separate dictionary or Unicode analyzer.
- Unmatched terms may be compared with indexed words by bounded Unicode
    Levenshtein distance. Short terms and ambiguous single-word corrections
    are rejected.
- libngram finds complete normalized phrases and overlapping word windows.
- libemb obtains one embedding for queries requiring semantic retrieval.
    Cosine similarity scans small indexes directly; HNSW handles larger ones.

## Phrase coverage

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

## Acceptance and ranking

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

## Semantic fallback

Semantic inference is a fallback after literal retrieval. When the query
is fully covered by a literal phrase or a complete contiguous segmentation
of indexed associations, search ranks the lexical candidates without
calling `libemb`. This applies regardless of the requested result limit.
Literal matches from the compiled dictionary-normalized index also count
toward complete coverage. Incomplete coverage, including a query with
unmatched words, triggers semantic inference and ranking.

The fallback rule measures coverage rather than trying to understand
the query. A full set of literal associations may still be ambiguous;
`embdex` returns ranked candidates for the caller to interpret. Truncated
lexical queries conservatively continue to semantic inference. The
diagnostic trace reports `variants_evaluated: 0` when embeddings were
skipped and a positive count when inference ran.

The current acceptance boundaries and score weights are provisional
implementation policy. They require validation against positive and
negative queries; the diagnostic trace is available for that purpose.

## Dictionary alternatives

Dictionaries provide word-for-word substitutions without assigning language
identities or grammar rules. The first replacement for a source word is used
when preparing the stored embeddings and substituted lexical index.
Additional replacements can produce alternative query texts.

Search evaluates at most eight deterministic word-substitution combinations
when semantic fallback is necessary. It enumerates all combinations if they
fit within that budget and otherwise distributes selections across ambiguous
word positions. Duplicate substituted strings are embedded only once.
Multiple semantic variants combine the highest cosine similarity with
reciprocal-ranking agreement.

Dictionary-normalized phrases also provide lexical evidence. The original
and substituted BM25 structures remain independent and share the same
association ordinals.

## Diagnostics

Set `KC_EMBDEX_TRACE` to a writable JSON Lines file to append the
evidence of every association considered by each search:

```sh
KC_EMBDEX_TRACE=embdex-trace.jsonl \
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

## Next step

Inspect the [Binary index](index-format.md) to understand stored maps and
memory-mapped querying.
