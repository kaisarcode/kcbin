# Binary index

A embdex index is one portable binary value containing the source
associations, stored embedding vectors, and the BM25 inverted index.
It uses one file and one libmmap instance.

## Format

The file is a little-endian binary value with a 68-byte header.
The header contains the magic signature, vector
dimension, document count, model identity, and the offset and length
of the BM25 section.

Each document record stores its ID and content lengths, NUL-terminated
ID and content, followed by its vector as IEEE 754 single-precision
values. Public IDs may repeat; the source ordinal is the document
identity used internally across both ranking engines.

The trailing BM25 section uses the portable memory representation
exported by libbm25. It contains its analyzer configuration, document
statistics, vocabulary, and postings. embdex validates the section
bounds and format before accepting a saved index. The data stored in
both sections belongs to the same ordered corpus.

## Memory and persistence

embdex uses libmmap to open the complete file as one value. The source
associations and embeddings are accessed from the mapped data.
libbm25 imports the embedded BM25 section directly from the mapped
bytes without re-tokenizing the document corpus; its current import API
allocates the internal dictionary, postings, and document storage.
libhnsw reconstructs its graph from the saved embedding vectors,
without running the embedding model again.

Saving writes the combined value through libmmap. It replaces the
existing destination and is not an atomic transaction; readers must
not concurrently access the file being replaced. For safe publication,
write a separate destination and switch consumers after completion.

The index does not contain executable binaries or runtime libraries.
Applications need platform-matched kclib artifacts when building.

## Next step

Return to the [embdex README](../README.md).
