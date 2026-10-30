# Binary index

The saved index contains the source associations and their embeddings, allowing
a different process to open and query it without access to the original corpus.
The application can copy the file to another machine with compatible binaries.

## Format

The version-1 file is one little-endian binary value. A fixed-size header
contains its magic signature, format version, vector dimension, association
count, and model identity. Each record then stores its ID length, content
length, NUL-terminated ID, NUL-terminated content, and a sequence of 32-bit
IEEE 754 embedding values.

IDs are not unique and no application-defined ordering is imposed on search
results. The file stores every association independently.

Opening validates the format version, model identity, vector dimension,
record bounds, string terminators and floating-point values. An incompatible
or damaged index is rejected.

## Memory and persistence

semidx uses `libmmap` to open the binary file as mapped read-only storage.
The source strings are read directly from the mapping. `libhnsw` currently
requires its own vector and graph storage in RAM, so opening the file rebuilds
the graph from the persisted vectors without generating embeddings again.

The graph is not currently serialized. Index opening therefore has a build
cost, and searches operate on the HNSW graph in memory.

Saving uses the existing `libmmap` replacement operation. It overwrites
the destination and is not an atomic transaction. Do not overwrite an index
while another process is reading it. For a new version, write a separate file
and switch consumers only after it is complete.

The index file does not package the executable or its shared dependencies.
All linked libraries must be available for the target platform at runtime.

## Next step

Return to the [semidx README](../README.md).
