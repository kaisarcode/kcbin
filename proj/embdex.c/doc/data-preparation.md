# Preparing corpora and dictionaries

A useful embdex index starts with its associations. The index is a candidate
narrower: it retrieves possible matches for an application to interpret, not
a classifier expected to answer every query correctly.

A package may represent commands, pictograms, emojis, products, documents,
tools, or any other set of named entities. The same preparation rules apply
regardless of the IDs' meaning.

## Start with the associations

A corpus is a JSON array of independent ID/text pairs:

```json
[
  {"id":"🥶","text":"cold face"},
  {"id":"🥶","text":"feeling freezing cold"},
  {"id":"🌧","text":"rain cloud"},
  {"id":"🌧","text":"rainy weather"}
]
```

The ID identifies the entity returned to the caller. Multiple descriptions
may share an ID, but each description is indexed independently. Write text
that accurately expresses what that entity represents or does. Do not
repeat the same sentence with mechanical prefixes such as `I need to`,
`how to`, and `tool to`: such variations add records without much
new retrieval evidence.

Prefer a combination of distinctive terms and meaningful phrases.
Single-word labels are useful for exact matching, but they frequently lack
context. For example, `cold` alone can refer to temperature or illness.
A phrase such as `feeling freezing cold` expresses a narrower sense.
Keep genuine ambiguity where the entity itself has several meanings;
do not invent descriptions simply to make tests pass. A larger corpus may
improve coverage when it adds genuine associations or useful descriptions.
Measure that gain against ranking quality, index size, build cost, and
query latency rather than minimizing record count by default.

When using the current `bge-small-en-v1.5` embedding model, English
descriptions are generally the more dependable starting point for
semantic retrieval. Other languages can be used, but their quality
must be measured. IDs can be arbitrary UTF-8 strings.

## Design the dictionary for the corpus

A dictionary is an optional JSON array of source/target literal strings:

```json
[
  {"source":"tengo frío","target":"feeling cold"},
  {"source":"me estoy congelando","target":"freezing cold"},
  {"source":"llueve","target":"raining"},
  {"source":"lluvia","target":"rain"},
  {"source":"no llueve","target":"not raining"}
]
```

These are illustrative transformations, not a universal translation
dictionary. Choose target text to agree with the intended concepts and
language of the actual corpus; change or omit entries that have no
useful counterpart in the target package.

Sources and targets may contain one or more words, symbols, or arbitrary
UTF-8 sequences. Prefer an expression that preserves the intended
meaning over word-by-word translations. For example, the negation in
`no llueve` must not be silently reduced to `rain`: this would reverse
what the user said. Neither a dictionary nor the embedding model guarantees
logical understanding of negation, so test it explicitly.

Do not assume an infinitive covers every conjugation. If users write
`busco`, `buscás`, or `buscando`, include relevant forms where the
literal dictionary needs them. Support informal, technical, and regional
forms when those are plausible user inputs. Choose only variants useful
to the domain rather than every possible word form.

An input expression can have several targets; the first is the primary
substitution, and further targets are query alternatives. Keep alternatives
meaningful and limited, since they can increase search-time inference.
Dictionary size is not a quality metric. A broader dictionary may recover
phrases and vocabulary that a specialized one misses, while a smaller
dictionary may reduce competing meanings and search cost. Compare both
using representative queries and relevant IDs at the intended result limit.
Keep the coverage that improves retrieval and remove mappings shown to
be misleading or unnecessary.

Dictionary transformations are literal substitutions, not grammatical
parsing, regular expressions, or logical inference. A transformed phrase
must be tested as a real search query.

## Build corpus and dictionary together

If both are being created from scratch, design them together:

1. Enumerate the entities or documents and preserve their authoritative IDs.
2. Describe each entity's meaning, action, objects, and distinguishing
    context, without attributing capabilities it does not have.
3. Add short canonical labels where exact phrase retrieval is useful.
4. List representative user requests in the supported languages and
    registers, including natural paraphrases and multiword expressions.
5. Create dictionary transformations that connect those requests to
    phrases and concepts actually represented by the corpus.
6. Remove duplicates, misleading senses, invented synonyms, and formulaic
    description variants.
7. Build the map and measure retrieval against independent queries.
8. Revise either input file on the basis of observed failures, then rebuild.

If the corpus already exists, preserve its IDs and descriptions by default.
Generate the dictionary against it, changing corpus records only when an
ambiguity, omission, or misleading description has been established.
Do not replace diverse source descriptions with synthetic template variants
during translation.

## Evaluate what the index returns

Build and search a package using the same files and query language the
application will use:

```sh
embdex build index.bin -i corpus.json -d dictionary.json --progress
embdex search index.bin "tengo mucho frío" --limit 10
```

Create a separate set of queries with expected relevant IDs. Test literal
matches, conjugations, regional variants, multiword phrases, negation,
additional conversational context, ambiguous expressions, and queries that
have no correct match in the package.

Inspect recall at the intended Top-K, the diversity of returned IDs, their
relative positions, query latency, and the effect of increasing the result
limit. Compare broader and narrower corpora and dictionaries with the same
query set to identify whether more coverage outweighs additional noise
and cost. A score is ranking evidence, not a correctness probability. When a
query has no applicable result, embdex may still return nearest candidates;
the caller must handle that case.

For example, an emoji corpus with many independent `cold` associations may
return faces associated with illness or sweat before the freezing face.
A missing translation for `llueve` and the negated phrase `no llueve`
can further obscure a longer query. This is a corpus-and-dictionary
preparation issue to investigate, not a reason to hardcode emoji behavior
into the search engine.

A failed test can indicate a missing dictionary form, a poor corpus
description, ambiguous data, or a retrieval/ranking problem. Determine
which before adding more entries. Keep a small set of regression queries
from each domain and compare index versions using the same query set.

## Using an AI to prepare the package

An AI can draft the corpus, dictionary, and evaluation queries from a
catalog of real entities. Give it the actual entity list, source
descriptions, domain, intended input languages, locale, and known user
expressions. It must not invent entity IDs or behaviors.

A reusable instruction is:

> Produce embdex data for the supplied entities. Return a corpus JSON
> array of {id,text} associations and an optional dictionary JSON array
> of {source,target} literal UTF-8 substitutions. Preserve all supplied
> IDs. Describe real distinct meanings and user intents, not repeated
> template sentences. Use the language best suited to the configured
> embedding model for the indexed text. Derive dictionary entries from
> likely user phrasing in the specified languages and locale, including
> useful conjugations, expressions, and multiword terms; preserve
> negation and meaningful distinctions. Prefer a compact, specialized
> dictionary to broad speculative translations. Do not create false
> associations. Supply a separate test set of natural queries and
> expected relevant IDs. State which entries are uncertain, and do not
> claim retrieval quality was validated without running the index.

The output schema for the two build inputs must remain exactly the
documented `id`/`text` and `source`/`target` arrays; any tests,
confidence notes, or generation metadata belong in separate files.

A generated package is a draft until tested. Where generation is
iterative, provide the AI with failed queries, expected relevant IDs,
returned candidates, and rankings. Ask it to identify whether the
failure belongs to the corpus, dictionary, or engine before proposing
a data change.

## Next step

Use the [CLI](cli.md) to compile and query the prepared package.
