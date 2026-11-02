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

An AI can assist with drafting descriptions, term substitutions, and test
queries, but the input catalog remains authoritative. The goal is to help
embdex retrieve relevant candidates, not to teach the model to return a
single final answer. No generated entry should invent a capability, ID,
document, or factual claim.

### What to provide

Give the model enough context to describe real content and anticipate real
queries:

- **Entities:** authoritative IDs and actual descriptions, documents, or
    excerpts. Preserve IDs exactly, including repeated IDs where an entity
    has several useful associations.
- **Purpose:** what each ID represents and what the consuming application
    will do with the retrieved candidates.
- **Query examples:** realistic ways users ask for that content, ideally
    collected independently from usage rather than generated from the
    descriptions themselves.
- **Language and locale:** input languages, regional vocabulary, register,
    abbreviations, technical terms, and common alternative spellings.
- **Model and format:** the embedding model used by the index and the exact
    corpus and dictionary JSON schemas.
- **Constraints:** terms that must remain distinct, sensitive negation,
    forbidden or unsupported interpretations, and whether the original
    corpus may be edited.
- **Retrieval target:** intended result limit, representative query length,
    resource constraints, and existing retrieval failures if available.

Provide a representative sample first when the catalog is large. Ask for a
small batch that can be reviewed and tested before processing the entire
collection. The model needs the real source content; a list of IDs alone
is insufficient to write trustworthy descriptions.

### What to generate

Request three separate deliverables:

1. `corpus.json`: a JSON array of `{"id":"...","text":"..."}`
    associations. Every description must be grounded in its source item;
    multiple distinct descriptions may share one ID.
2. `dictionary.json`: a JSON array of
    `{"source":"...","target":"..."}` literal UTF-8 substitutions.
    These map likely input expressions to terms represented by the
    corpus. They are not stemming or grammar rules.
3. `queries.json`: a separate evaluation set of natural queries with
    expected relevant IDs. It is not passed to the embdex builder.

Keep generation notes, doubtful mappings, provenance, and evaluation
metadata out of the two build files. Review apparent synonyms carefully:
a related word is not always interchangeable, and a translation can change
meaning, polarity, or scope. Avoid mechanically multiplying descriptions
with generic templates.

### Example prompt

Copy this prompt and replace the bracketed inputs with source material
from the application. The output files should be returned separately as
valid JSON without prose inside the JSON.

```text
You are preparing retrieval data for embdex, a candidate narrower.
It returns ranked associations, not definitive answers.

Application purpose:
[What the consuming application does with candidate IDs]

Authoritative source catalog:
[ID and actual text, metadata, or document excerpts for each item]

Typical user queries:
[Real or representative queries, including difficult queries]

Input languages and locale:
[Languages, region, register, relevant abbreviations and vocabulary]

Embedding model:
[bge-small-en-v1.5 or the model actually in use]

Retrieval constraints:
[Expected Top-K, important distinctions, resource budget,
existing corpus or dictionary that must be preserved]

Prepare three separate JSON outputs:

corpus.json:
- Use exactly an array of {"id":"...","text":"..."} objects.
- Preserve every authoritative ID. Never invent items or capabilities.
- Describe each item's genuine meaning, actions, objects, and context.
- Provide additional associations only for distinct useful descriptions.
- Prefer the language appropriate to the embedding model where useful.
- Do not generate formulaic paraphrases merely to increase corpus size.

dictionary.json:
- Use exactly an array of {"source":"...","target":"..."} objects.
- Map likely input terms and expressions to meaningful corpus language.
- Include useful regional forms, variants, and complete phrases.
- Preserve negation, technical distinctions, and multiword meaning.
- Do not invent stemming rules, regexes, or additional schema fields.
- Avoid ambiguous and misleading substitutions.
- Do not assume that a larger dictionary is always better.

queries.json:
- Produce a separate array of objects with "query" and "relevant_ids".
- Use real or independently written natural queries where provided.
- Include positive, ambiguous, misspelled, and out-of-scope examples.
- Use only authoritative IDs; use [] when no result should be relevant.
- Distinguish generated test suggestions from observed user queries.

Return valid UTF-8 JSON for each file separately. Report uncertain
associations outside the build JSON. Do not assert that retrieval quality
is validated: that requires building and searching the resulting index.
```

A generated test set is useful for finding obvious errors, but it may
share the same assumptions as the generated corpus. Keep a separate
holdout set of real queries and manually checked relevant IDs where
possible; otherwise report the limits of the evaluation.

### Iterate using retrieval results

Build the index and test the generated data before requesting revisions:

```sh
embdex build index.bin -i corpus.json -d dictionary.json --progress
embdex search index.bin "representative query" --limit 10
```

Give the model the failed query, the actual ranked results and scores,
the relevant IDs, and the source items they represent. Ask it to diagnose
whether the issue is missing content, poor descriptions, a misleading
substitution, ambiguity, or ranking behavior. Request only the changes
supported by that evidence.

Compare a baseline with revised corpus and dictionary sizes using the
same independent queries. Measure relevant IDs in Top-K, result
diversity, latency, and index size. Both additional coverage and reduced
noise can improve retrieval, depending on the case. Keep only
changes that provide a useful measured tradeoff.

## Next step

Use the [CLI](cli.md) to compile and query the prepared package.
