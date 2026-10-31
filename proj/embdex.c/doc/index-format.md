# Binary index

An embdex index is a portable binary containing the original ID/text
associations, precomputed vectors, and lexical retrieval structures. An index
built with a dictionary also contains that dictionary and an independent
BM25 index over its word-for-word normalized associations.

Build inputs are not required at search time. Different domain, language
or regional maps can be compiled to independent binary files and opened
without referring to the original dataset or dictionary.

## Format

The file uses little-endian integer fields and one V1 format
with a 104-byte header, whether or not dictionaries are present:

| Offset | Size | Value |
| :--- | ---: | :--- |
| 0 | 8 | `KCEMBDX\\n` magic bytes |
| 8 | 4 | Format version (1) |
| 12 | 4 | Vector dimension |
| 16 | 8 | Number of associations |
| 24 | 32 | Model identity and dictionary fingerprint |
| 56 | 8 | Original BM25 section offset |
| 64 | 8 | Original BM25 section length |
| 72 | 8 | Normalized BM25 section offset |
| 80 | 8 | Normalized BM25 section length (zero without a dictionary) |
| 88 | 8 | Dictionary section offset |
| 96 | 8 | Dictionary section length |

The file contains contiguous sections in order: associations and vectors,
original BM25, optional normalized BM25, and dictionary. The dictionary starts
with a 32-bit entry count (zero when absent) and uses 32-bit byte lengths
followed by NUL-terminated source/replacement word pairs. Repeated source
words retain the ordered alternatives supplied by all input dictionaries,
with exact duplicate pairs discarded. Neither source nor replacement
is labeled with a language.

Each association stores its ID and content lengths as 32-bit values,
NUL-terminated ID and content strings, and a vector of single-precision
IEEE 754 floats. IDs may repeat; their zero-based insertion ordinal remains
the internal identity for every retrieval mechanism.

Each BM25 section contains analyzer configuration, normalized terms,
document statistics, and postings. The source and translated BM25 indexes
share association ordinals but remain independent. They use accent folding.
The embedded dictionary is loaded into a compact lookup table when
the map is opened; none of the original TSV files are needed.

Loaded indexes validate section boundaries, association counts, vector
dimensions, dictionary structure, and the embedded dictionary fingerprint.
The fingerprint detects inconsistent dictionary metadata; it is not a
cryptographic signature.

Only V1 is supported during development. Rebuild existing maps from
source associations and dictionaries whenever the format changes.

## Memory and persistence

embdex uses libmmap to access one mapped file. It imports the precompiled
BM25 sections without re-tokenizing the dataset. Original association
strings and vectors borrow mapped bytes; vocabulary, postings and embedded
dictionary lookup structures are restored for querying.

For indexes with up to 256 associations, embdex computes vector similarity
directly and avoids a resident HNSW graph. Larger indexes construct HNSW
once when opened; the stored vectors do not require rerunning embeddings.

Phrase matches use BM25's Unicode normalization over relevant source
associations at query time. The two BM25 sections in a dictionary-built
map are intentionally separate because they index different texts; neither
requires a second Unicode analyzer.
Temporary query evidence is released before search returns.

Saving replaces the destination file and is not an atomic transaction.
Write to a separate file and publish the completed index separately if
concurrent readers must keep their current mapping.

## Next step

Return to the [embdex README](../README.md).
