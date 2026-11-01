# CLI

`embdex` compiles text associations into binary maps and searches them with
natural-language queries. Building may take time because each association
receives a stored embedding. Search reuses that work.

## Build a map

Each nonempty TSV line contains an association ID, a tab, and its text:

```text
find	find files by name
grep	search for text inside files
```

Save the lines as `corpus.tsv` and build the map:

```sh
embdex build commands.bin -i corpus.tsv
```

Use standard input instead of a file when convenient:

```sh
printf 'find\tfind files by name\n' | embdex build commands.bin
```

IDs may repeat. The first tab separates the ID from the text; neither
may be empty. Additional tabs are preserved as part of the text. Embedded
line breaks and NUL bytes are unsupported in CLI input. Empty lines are
ignored.

## Compile dictionaries

An optional dictionary contains UTF-8 source-word and replacement-word
pairs, separated by a tab:

```text
buscar	find
archivos	files
fichero	file
```

Save this as `dictionary.tsv`, then build:

```sh
embdex build commands.bin -i corpus.tsv -d dictionary.tsv
embdex search commands.bin "buscar archivos"
```

`-d` and `--dictionary` are repeatable. Dictionaries are merged in the
order supplied. Repeated source words may have different replacements;
the first is the primary substitution for indexed text, while the others
remain query alternatives. Exact duplicate pairs are discarded.

Substitutions operate on words, not full-sentence translations. Text
without a matching dictionary entry is preserved. The compiled map
contains the merged dictionary and does not need the TSV files at search
time. Dictionaries are optional.

## Show build progress

`-p` and `--progress` show preparation, embedding progress, saving, and
completion:

```sh
embdex build commands.bin -i corpus.tsv -d dictionary.tsv --progress
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

Results are printed in descending score order, one per line:

```text
ID	SCORE	CONTENT
```

These are three tab-separated fields, not a header emitted by the CLI.
The score ranks associations; it is not a probability or a final intent
classification. The default limit is 10. `-l` / `--limit` accepts a
nonnegative integer, and zero suppresses result delivery.

`-h` / `--help` prints usage, and `-v` / `--version` prints the build
version. Invalid arguments or failed operations return a nonzero exit
status and write diagnostics to standard error.

## Next step

Use the [C API](api.md) to build and query maps from an application.
