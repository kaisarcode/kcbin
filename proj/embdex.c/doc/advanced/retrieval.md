# Retrieval

embdex retrieves independent associations using lexical, spelling, phrase,
and semantic evidence. It returns ranked candidates for applications to
interpret; it does not identify a single definitive intent or invoke tools.

The embedding component currently uses the embedded English-oriented
`bge-small-en-v1.5` model. Corpus design and optional build-time dictionaries
provide the application-specific associations that are searched.

## Candidate discovery

Search combines these independent retrieval mechanisms:

- BM25 ranks matching tokens from the query and provides indexed postings.
- UTF-8 dictionary source and target terms are validated independently of BM25;
    their recognition preserves words, punctuation, numbers, and symbols.
- Before lookup, unknown query words may be corrected against dictionary
    sources and indexed corpus words. Three or more repeated ASCII letters
    can be reduced to one or two only when the result exists in a searchable
    vocabulary. Unique dictionary-source spelling corrections use bounded
    Levenshtein distance. The existing corpus-vocabulary Levenshtein retrieval
    remains available for terms not otherwise matched.
- Morphological variants and regional expressions belong in the dictionary;
    this correction does not apply language-specific stemming rules.
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

The engine ranks every discovered association, including candidates
whose evidence does not meet the strong-acceptance criteria. Those criteria
remain available for diagnostics; they do not truncate the returned
candidate set. BM25 supplies discovery order, while the lexical and
semantic evidence determine the final ranking. HNSW retrieval likewise
uses an unrestricted similarity threshold so its nearest neighbors are
available to the ranker even when their similarity is weak. Repeated
associations with the same ID remain eligible but receive a progressively
smaller ranking score, preventing one ID from crowding out the candidate
list. The requested result limit is applied after ranking.

Lexical coverage is weighted by the rarity of the original normalized
query words. Terms absent from the index are assigned singleton rarity
rather than an artificial maximum. Approximate matches do not alter
the document frequency of the original term, and repeated evidence
for one query position is counted once.

A complete phrase, strong multi-term lexical coverage or independently
strong semantic similarity can justify a strong-acceptance diagnostic. Additionally, an
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

Semantic inference is a fallback after literal retrieval. A complete
contiguous phrase match in the original or dictionary-normalized text
can skip `libemb`, regardless of the requested result limit. Covering
the query with several independent associations does not stop semantic
retrieval: those matches may explain individual terms without finding
other relevant associations. The covering still contributes to ranking.

A query with unmatched words may still avoid embeddings when translated
lexical evidence already finds at least the requested number of distinct
candidate IDs. Otherwise semantic retrieval remains available. Truncated
lexical queries conservatively continue to semantic inference. The
diagnostic trace reports `variants_evaluated: 0` when embeddings were
skipped and a positive count when inference ran.

The current acceptance boundaries and score weights are provisional
implementation policy. They require validation against positive and
negative queries; the diagnostic trace is available for that purpose.

## Dictionary alternatives

Dictionaries substitute complete UTF-8 terms, including multiword expressions
and symbols, without assigning language identities or grammar rules. The
longest source match takes precedence. The first replacement of a source term
is used when preparing the stored embeddings and substituted lexical index.
Additional replacements can produce alternative query texts. Substitutions
are applied once to the original text, without recursively rewriting output.

Search evaluates at most eight deterministic term-substitution combinations
when semantic fallback is necessary. It enumerates all combinations if they
fit within that budget and otherwise distributes selections across ambiguous
term occurrences. Duplicate substituted strings are embedded only once.
Multiple semantic variants combine the highest cosine similarity with
reciprocal-ranking agreement.

Dictionary-normalized phrases also provide lexical evidence. The original
and substituted BM25 structures remain independent and share the same
association ordinals. Translated matches have their own token-position masks
and coverage measurements: replacing three tokens with one does not require
an artificial one-to-one position mapping. Positional correspondence is used
only when the original and transformed tokens can be verified to align.

Complete original and translated phrase matches preserve UTF-8 punctuation,
including operators such as C++. A translated target made entirely of
symbols remains searchable through literal matching even when BM25 yields
no tokens. Translated lexical coverage contributes to candidate ranking, but
does not by itself suppress semantic retrieval unless a complete
translated phrase is matched.

## Diagnostics

Set `KC_EMBDEX_TRACE` to a writable JSON Lines file to append the
evidence of every association considered by each search:

```sh
KC_EMBDEX_TRACE=embdex-trace.jsonl \
    embdex search corpus.bin "peer authentication" --limit 5
```

The opt-in trace includes corpus ordinals, IDs, original content,
bitmasks of original and translated lexical matches, phrase matches,
BM25 and semantic ranks, measured cosine similarities, independent original
and translated coverage, acceptance reasons, scores and ranks
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
