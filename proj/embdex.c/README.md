# embdex.c - Text association retrieval

`embdex` compiles ID/text associations into portable binary maps and retrieves
relevant candidates from natural-language queries. Maps contain their own
indexes and optional word dictionaries, so applications can open and search
them locally without the original input files, a database, or a server.

Search checks literal terms and phrases first, then uses spelling correction
and semantic embeddings when needed. Each association remains independently
searchable; repeated IDs are allowed. The caller decides how to use the
ranked results.

## Quick start

Build a map from a tab-separated ID/text association, then search it:

```sh
printf 'find\tfind files by name\n' | embdex build commands.bin
embdex search commands.bin "find files by name"
```

Search prints the association ID, score, and original text as tab-separated
fields. The map can be reused for later queries.

For larger collections, provide a TSV file and optional dictionaries:

```sh
embdex build commands.bin -i corpus.tsv -d dictionary.tsv --progress
embdex search commands.bin "search for a file" --limit 10
```

A dictionary contains source-word/replacement-word pairs and is compiled
into the map. `--progress` shows build stages and embedding completion on
standard error; without it, building is silent.

## Documentation

- [CLI](doc/cli.md) - corpus and dictionary input, commands, and progress.
- [C API](doc/api.md) - build, search, callbacks, and ownership.
- [Retrieval](doc/advanced/retrieval.md) - candidate discovery and ranking.
- [Binary index](doc/advanced/index-format.md) - persistence and memory layout.

## Build

From the project directory, point `KCDIST_DIR` to a precompiled kclib
distribution containing matching artifacts for libemb, libhnsw, libbm25,
libmmap, libngram, and libleven:

```sh
export KCDIST_DIR="$HOME/kclib/dist"
make
make test
```

The directory shown is an example; use the location of your distribution.
`make` builds for the native platform. `make all` builds the configured
cross-platform targets when the corresponding toolchains and precompiled
dependencies are available. The project uses the supplied kclib artifacts
without rebuilding them.

## Notice

No guarantees are provided regarding stability or future support. You may
test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
