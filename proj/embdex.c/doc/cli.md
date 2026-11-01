# CLI

`embdex` compiles text associations into binary maps and searches them with
natural-language queries. Building may take time because each association
receives a stored embedding. Search reuses that work.

## Build a map

The input is a JSON array of ID/text associations:

```json
[
  {"id": "find", "text": "find files by name"},
  {"id": "grep", "text": "search for text\ninside files"}
]
```

Save it as `corpus.json` and build the map:

```sh
embdex build commands.bin -i corpus.json
```

Standard input works as well:

```sh
printf '[{"id":"find","text":"find files by name"}]\n' | embdex build commands.bin
```

IDs may repeat, and every association remains independent. Both fields must
be nonempty JSON strings. Text can contain tabs, quotes, and newlines escaped
according to JSON syntax. An empty array is permitted.

## Compile dictionaries

An optional dictionary is a JSON array of source/target string pairs:

```json
[
  {"source": "buscar", "target": "find"},
  {"source": "archivos", "target": "files"},
  {"source": "fichero", "target": "file"}
]
```

Save this as `dictionary.json`, then build:

```sh
embdex build commands.bin -i corpus.json -d dictionary.json
embdex search commands.bin "buscar archivos"
```

`-d` and `--dictionary` are repeatable. Dictionaries are merged in the
order supplied. Repeated source words may have different replacements;
the first is the primary substitution for indexed text, while the others
remain query alternatives. Exact duplicate pairs are discarded.

Substitutions operate on words, not full-sentence translations. Text
without a matching dictionary entry is preserved. The compiled map
contains the merged dictionary and does not need the input files during
search. Dictionaries are optional.

## Show build progress

`-p` and `--progress` show preparation, embedding progress, saving, and
completion:

```sh
embdex build commands.bin -i corpus.json -d dictionary.json --progress
```

Embedding progress reports completed associations and updates the same
terminal line at 1% intervals. Other build phases have no percentage.
Progress goes to standard error; without the flag, a successful build
is silent. Search does not accept this option.

## Search a map

```sh
embdex search commands.bin "find files by name"
embdex search commands.bin "search inside files" --limit 5
```

Results are a JSON array in descending score order:

```json
[
  {"id": "find", "score": 0.85, "text": "find files by name"}
]
```

Scores rank candidates; they are not probabilities or final classifications.
Queries outside the corpus may still return candidates. The default limit is
10. `-l` / `--limit` accepts a nonnegative integer; zero returns `[]`.

`-h` / `--help` prints usage, and `-v` / `--version` prints the build
version. Invalid arguments or failed operations return a nonzero exit
status and write diagnostics to standard error.

## Next step

Use the [C API](api.md) to build and query maps from an application.
