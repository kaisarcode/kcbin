# Binary index

An embdex index is one portable binary value containing source associations,
stored vectors, and the BM25 inverted index. The BM25 section also contains
the normalized term vocabulary and document postings used for exact and
spelling-based retrieval. There is no separate dictionary or corpus service.

## Format

The file is a little-endian binary value with a 72-byte header:

| Offset | Size | Value |
| :--- | ---: | :--- |
| 0 | 8 | `KCEMBDX\\n` magic bytes |
| 8 | 4 | Format version (currently 1) |
| 12 | 4 | Vector dimension |
| 16 | 8 | Number of associations |
| 24 | 32 | Fixed embedding model identity |
| 56 | 8 | Offset of embedded BM25 section |
| 64 | 8 | Size of embedded BM25 section |

Each association stores its ID and content lengths as 32-bit values,
NUL-terminated ID and content strings, and a vector of single-precision
IEEE 754 floats. IDs may repeat; their zero-based insertion ordinal remains
the internal identity for every retrieval mechanism.

The BM25 section includes the corpus analyzer configuration, term dictionary,
document statistics, and inverted postings. New indexes use accent folding
for lexical normalization. Loaded indexes must have a recognized format and
embedding identity; corrupt vectors, incomplete records and incompatible
sections are rejected.

The previous index format is not read by this version. Rebuild existing
indexes from their source associations after updating the library.

## Memory and persistence

embdex uses libmmap to access one mapped file. It imports the BM25 section
without tokenizing the source corpus again. The original strings and vectors
are borrowed from the mapped bytes.

For indexes with up to 256 associations, embdex computes vector similarity
directly and avoids a resident HNSW graph. Larger indexes construct HNSW
once when opened; the stored vectors do not require rerunning embeddings.

Phrase matches use BM25's Unicode normalization over relevant associations
at query time. The term vocabulary and postings are already stored inside
the BM25 section, so they are not duplicated in a second persistent index.
Temporary query evidence is released before search returns.

Saving replaces the destination file and is not an atomic transaction.
Write to a separate file and publish the completed index separately if
concurrent readers must keep their current mapping.

## Next step

Return to the [embdex README](../README.md).
