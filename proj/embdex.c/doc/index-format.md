# Binary index

An embdex index is a portable binary containing the original ID/text
associations, precomputed vectors, and lexical retrieval structures. An index
built with a dictionary also contains that dictionary and an independent
BM25 index over its word-for-word normalized associations.

Build inputs are not required at search time. Different domain, language
or regional maps can be compiled to independent binary files and opened
without referring to the original dataset or dictionary.

## Format

The file uses little-endian integer fields. A dictionary-free index
retains the version 1 format with a 72-byte header. A dictionary-built
index uses version 2 and a 104-byte header:

| Offset | Size | Value |
| :--- | ---: | :--- |
| 0 | 8 | `KCEMBDX\\n` magic bytes |
| 8 | 4 | Format version (1 or 2) |
| 12 | 4 | Vector dimension |
| 16 | 8 | Number of associations |
| 24 | 32 | Embedding identity (including dictionary fingerprint for version 2) |
| 56 | 8 | Original BM25 section offset |
| 64 | 8 | Original BM25 section length |
| 72 | 8 | Normalized BM25 section offset (version 2) |
| 80 | 8 | Normalized BM25 section length (version 2) |
| 88 | 8 | Dictionary section offset (version 2) |
| 96 | 8 | Dictionary section length (version 2) |

Version 2 contains contiguous sections in order: associations and vectors,
original BM25, normalized BM25, and dictionary. The dictionary starts
with its 32-bit entry count and uses 32-bit byte lengths followed by
NUL-terminated normalized-source/English-replacement word pairs.
Repeated source words retain their ordered translation alternatives.

Each association stores its ID and content lengths as 32-bit values,
NUL-terminated ID and content strings, and a vector of single-precision
IEEE 754 floats. IDs may repeat; their zero-based insertion ordinal remains
the internal identity for every retrieval mechanism.

Each BM25 section contains analyzer configuration, normalized terms,
document statistics, and postings. The source and translated BM25 indexes
share association ordinals but remain independent. They use accent folding.
The dictionary embedded in version 2 is loaded into a compact lookup table
when the map is opened; no source TSV file is needed.

Loaded indexes validate section boundaries, association counts, vector
dimensions and dictionary structure. Version 1 remains readable for
dictionary-free maps. Older version 1 indexes built with the experimental
external dictionary still require their original external configuration.

The previous index format is not read by this version. Rebuild existing
indexes from their source associations after updating the library.

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
