/**
 * libembdex.c - Semantic Index Implementation
 * Summary: Composes embeddings, HNSW, BM25, and mapped persistence.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libembdex.h"
#include "libemb.h"
#include "libbm25.h"
#include "libhnsw.h"
#include "libmmap.h"
#include "libngram.h"
#include "libleven.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_EMBDEX_BUILD_VERSION
#define KC_EMBDEX_BUILD_VERSION 0
#endif

#define KC_EMBDEX_HEADER_SIZE 72U
#define KC_EMBDEX_FORMAT_VERSION 1U
#define KC_EMBDEX_MODEL_SIZE 32U
#define KC_EMBDEX_EXACT_LIMIT 256U
#define KC_EMBDEX_QUERY_TOKENS 64U
#define KC_EMBDEX_FUZZY_CHOICES 8U

static const unsigned char kc_embdex_magic[8] = {
    'K', 'C', 'E', 'M', 'B', 'D', 'X', '\n'
};
static const char kc_embdex_model[KC_EMBDEX_MODEL_SIZE] =
    "bge-small-en-v1.5";

typedef struct {
    const char *id;
    const char *content;
    const unsigned char *encoded;
    float *owned_vector;
    int owned_strings;
} kc_embdex_record_t;

struct kc_embdex {
    kc_embdex_record_t *records;
    size_t count;
    size_t dimension;
    kc_hnsw_t *graph;
    kc_bm25_t *lexical;
    kc_mmap_t *mapping;
    size_t lexical_offset;
    size_t lexical_size;
};

/**
 * Check whether a bounded slice fits in an encoded buffer.
 * @param size Total buffer size.
 * @param offset Offset to inspect.
 * @param length Required number of bytes.
 * @return Nonzero when the slice is valid.
 */
static int kc_embdex_fits(size_t size, size_t offset, size_t length)
{
    return offset <= size && length <= size - offset;
}

/**
 * Read one little-endian unsigned 32-bit value.
 * @param data Pointer to at least four encoded bytes.
 * @return Decoded value.
 */
static uint32_t kc_embdex_read_u32(const unsigned char *data)
{
    return (uint32_t)data[0] |
        ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) |
        ((uint32_t)data[3] << 24);
}

/**
 * Read one little-endian unsigned 64-bit value.
 * @param data Pointer to at least eight encoded bytes.
 * @return Decoded value.
 */
static uint64_t kc_embdex_read_u64(const unsigned char *data)
{
    uint64_t value = 0;

    for (unsigned int i = 0; i < 8; i++) {
        value |= (uint64_t)data[i] << (i * 8);
    }
    return value;
}

/**
 * Write one unsigned 32-bit value in little-endian order.
 * @param data Destination for four bytes.
 * @param value Value to encode.
 * @return void
 */
static void kc_embdex_write_u32(unsigned char *data, uint32_t value)
{
    for (unsigned int i = 0; i < 4; i++) {
        data[i] = (unsigned char)(value >> (i * 8));
    }
}

/**
 * Write one unsigned 64-bit value in little-endian order.
 * @param data Destination for eight bytes.
 * @param value Value to encode.
 * @return void
 */
static void kc_embdex_write_u64(unsigned char *data, uint64_t value)
{
    for (unsigned int i = 0; i < 8; i++) {
        data[i] = (unsigned char)(value >> (i * 8));
    }
}

/**
 * Decode a little-endian IEEE 754 single-precision float.
 * @param data Pointer to four encoded bytes.
 * @return Decoded floating-point value.
 */
static float kc_embdex_read_float(const unsigned char *data)
{
    uint32_t bits = kc_embdex_read_u32(data);
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

/**
 * Encode one IEEE 754 single-precision float.
 * @param data Destination for four bytes.
 * @param value Floating-point value.
 * @return void
 */
static void kc_embdex_write_float(unsigned char *data, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    kc_embdex_write_u32(data, bits);
}

/**
 * Copy a NUL-terminated string into index-owned storage.
 * @param input Input string.
 * @return Allocated copy, or NULL on allocation failure.
 */
static char *kc_embdex_copy_string(const char *input)
{
    size_t length;
    char *copy;

    if (input == NULL) {
        return NULL;
    }
    length = strlen(input);
    if (length == SIZE_MAX) {
        return NULL;
    }
    copy = (char *)malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, input, length + 1);
    }
    return copy;
}

/**
 * Allocate an index and its document-reference array.
 * @param count Number of document references to allocate.
 * @return New index or NULL if allocation fails.
 */
static kc_embdex_t *kc_embdex_allocate(size_t count)
{
    kc_embdex_t *index = (kc_embdex_t *)calloc(1, sizeof(*index));

    if (index == NULL) {
        return NULL;
    }
    if (count > SIZE_MAX / sizeof(*index->records)) {
        free(index);
        return NULL;
    }
    if (count > 0) {
        index->records = (kc_embdex_record_t *)calloc(
            count, sizeof(*index->records)
        );
        if (index->records == NULL) {
            free(index);
            return NULL;
        }
    }
    index->count = count;
    return index;
}

/**
 * Add vectors to a new HNSW graph using unique positional identifiers.
 * @param index Index with validated record references and dimensions.
 * @return KC_EMBDEX_OK or a negative status code.
 */
static int kc_embdex_prepare_graph(kc_embdex_t *index)
{
    kc_hnsw_options_t options = {0};
    float *decoded = NULL;
    int status = KC_EMBDEX_EINDEX;

    options.dimension = index->dimension;
    if (kc_hnsw_open(&index->graph, &options) != KC_HNSW_OK) {
        return KC_EMBDEX_EINDEX;
    }
    if (index->mapping != NULL && index->count > 0) {
        if (index->dimension > SIZE_MAX / sizeof(float)) {
            return KC_EMBDEX_EFORMAT;
        }
        decoded = (float *)malloc(index->dimension * sizeof(float));
        if (decoded == NULL) {
            return KC_EMBDEX_ENOMEM;
        }
    }

    for (size_t i = 0; i < index->count; i++) {
        char key[32];
        const float *vector = index->records[i].owned_vector;

        if (snprintf(key, sizeof(key), "%zu", i) < 0) {
            goto finished;
        }
        if (index->mapping != NULL) {
            for (size_t j = 0; j < index->dimension; j++) {
                decoded[j] = kc_embdex_read_float(
                    index->records[i].encoded + j * sizeof(float)
                );
                if (!isfinite(decoded[j])) {
                    status = KC_EMBDEX_EFORMAT;
                    goto finished;
                }
            }
            vector = decoded;
        }
        if (kc_hnsw_add(index->graph, key, vector) != KC_HNSW_OK) {
            goto finished;
        }
    }
    if (kc_hnsw_build(index->graph) == KC_HNSW_OK) {
        status = KC_EMBDEX_OK;
    }

finished:
    free(decoded);
    return status;
}

/**
 * Construct the independent lexical index over the same ordered associations.
 * @param index Index with owned or mapped document strings.
 * @return KC_EMBDEX_OK or an appropriate negative status.
 */
static int kc_embdex_prepare_lexical(kc_embdex_t *index)
{
    int fold_accents = 1;
    kc_bm25_options_t options = { .fold_accents = &fold_accents };
    int status = kc_bm25_open(&index->lexical, &options);

    if (status != KC_BM25_OK) {
        return status == KC_BM25_ENOMEM ? KC_EMBDEX_ENOMEM : KC_EMBDEX_ELEXICAL;
    }
    for (size_t i = 0; i < index->count; i++) {
        char ordinal[32];

        if (snprintf(ordinal, sizeof(ordinal), "%zu", i) < 0)
            return KC_EMBDEX_ELEXICAL;
        status = kc_bm25_add(index->lexical,
            ordinal, index->records[i].content);
        if (status != KC_BM25_OK) {
            return status == KC_BM25_ENOMEM ?
                KC_EMBDEX_ENOMEM : KC_EMBDEX_ELEXICAL;
        }
    }
    status = kc_bm25_build(index->lexical);
    return status == KC_BM25_OK ? KC_EMBDEX_OK :
        status == KC_BM25_ENOMEM ? KC_EMBDEX_ENOMEM : KC_EMBDEX_ELEXICAL;
}

/**
 * Build an index of independent text associations.
 * @param out Receives the built index.
 * @param documents Input corpus.
 * @param count Number of entries in the corpus.
 * @return KC_EMBDEX_OK or a negative status code.
 */
static int kc_embdex_create(
    kc_embdex_t **out,
    const kc_embdex_document_t *documents,
    size_t count
)
{
    kc_embdex_t *index;
    int status = KC_EMBDEX_EEMBED;

    if (out == NULL) {
        return KC_EMBDEX_EINVAL;
    }
    *out = NULL;
    if (count > 0 && documents == NULL) {
        return KC_EMBDEX_EINVAL;
    }
    for (size_t i = 0; i < count; i++) {
        if (documents[i].id == NULL || documents[i].content == NULL) {
            return KC_EMBDEX_EINVAL;
        }
    }

    index = kc_embdex_allocate(count);
    if (index == NULL) {
        return KC_EMBDEX_ENOMEM;
    }
    index->dimension = kc_emb_dimension();
    if (index->dimension == 0 ||
        index->dimension > SIZE_MAX / sizeof(float)) {
        goto failure;
    }

    for (size_t i = 0; i < count; i++) {
        size_t length = 0;

        index->records[i].id = kc_embdex_copy_string(documents[i].id);
        index->records[i].content = kc_embdex_copy_string(
            documents[i].content
        );
        index->records[i].owned_strings = 1;
        if (index->records[i].id == NULL ||
            index->records[i].content == NULL) {
            status = KC_EMBDEX_ENOMEM;
            goto failure;
        }
        if (kc_emb_embed(
            documents[i].content,
            &index->records[i].owned_vector,
            &length
        ) != KC_EMB_OK || length != index->dimension) {
            goto failure;
        }
    }
    status = kc_embdex_prepare_lexical(index);
    if (status != KC_EMBDEX_OK) {
        goto failure;
    }
    *out = index;
    return KC_EMBDEX_OK;

failure:
    kc_embdex_close(index);
    return status;
}

/**
 * Load and validate the binary records from an opened mapping.
 * @param index Destination index with a live file mapping.
 * @return KC_EMBDEX_OK or a negative status code.
 */
static int kc_embdex_load_records(kc_embdex_t *index)
{
    const void *bytes;
    size_t size;
    size_t offset;
    const unsigned char *data;
    uint64_t count;

    if (kc_mmap_get(index->mapping, &bytes, &size) != KC_MMAP_OK ||
        size < KC_EMBDEX_HEADER_SIZE) {
        return KC_EMBDEX_EFORMAT;
    }
    data = (const unsigned char *)bytes;
    if (memcmp(data, kc_embdex_magic, 8) != 0 ||
        kc_embdex_read_u32(data + 8) != KC_EMBDEX_FORMAT_VERSION ||
        memcmp(data + 24, kc_embdex_model, KC_EMBDEX_MODEL_SIZE) != 0)
        return KC_EMBDEX_EFORMAT;
    {
        uint64_t lexical_offset = kc_embdex_read_u64(data + 56);
        uint64_t lexical_size = kc_embdex_read_u64(data + 64);

        if (lexical_offset > size || lexical_size == 0 ||
            lexical_size > size - lexical_offset ||
            lexical_offset + lexical_size != size)
            return KC_EMBDEX_EFORMAT;
        index->lexical_offset = (size_t)lexical_offset;
        index->lexical_size = (size_t)lexical_size;
    }
    index->dimension = kc_embdex_read_u32(data + 12);
    count = kc_embdex_read_u64(data + 16);
    if (index->dimension == 0 || index->dimension != kc_emb_dimension() ||
        index->dimension > SIZE_MAX / sizeof(float) ||
        count > SIZE_MAX / sizeof(kc_embdex_record_t)) {
        return KC_EMBDEX_EFORMAT;
    }
    index->count = (size_t)count;
    if (index->count > 0) {
        index->records = (kc_embdex_record_t *)calloc(
            index->count, sizeof(*index->records)
        );
        if (index->records == NULL) {
            return KC_EMBDEX_ENOMEM;
        }
    }
    offset = KC_EMBDEX_HEADER_SIZE;
    for (size_t i = 0; i < index->count; i++) {
        size_t id_length;
        size_t content_length;

        if (!kc_embdex_fits(size, offset, 8)) {
            return KC_EMBDEX_EFORMAT;
        }
        id_length = kc_embdex_read_u32(data + offset);
        content_length = kc_embdex_read_u32(data + offset + 4);
        offset += 8;
        if (!kc_embdex_fits(size, offset, id_length + 1) ||
            data[offset + id_length] != 0 ||
            memchr(data + offset, 0, id_length) != NULL) {
            return KC_EMBDEX_EFORMAT;
        }
        index->records[i].id = (const char *)(data + offset);
        offset += id_length + 1;
        if (!kc_embdex_fits(size, offset, content_length + 1) ||
            data[offset + content_length] != 0 ||
            memchr(data + offset, 0, content_length) != NULL) {
            return KC_EMBDEX_EFORMAT;
        }
        index->records[i].content = (const char *)(data + offset);
        offset += content_length + 1;
        if (!kc_embdex_fits(size, offset, index->dimension * sizeof(float))) {
            return KC_EMBDEX_EFORMAT;
        }
        index->records[i].encoded = data + offset;
        for (size_t j = 0; j < index->dimension; j++) {
            if (!isfinite(kc_embdex_read_float(
                index->records[i].encoded + j * sizeof(float))))
                return KC_EMBDEX_EFORMAT;
        }
        offset += index->dimension * sizeof(float);
    }
    return offset == index->lexical_offset ?
        KC_EMBDEX_OK : KC_EMBDEX_EFORMAT;
}

/**
 * Open a binary semantic index and reconstruct its in-memory graph.
 * @param out Receives the opened index.
 * @param path Index file path.
 * @return KC_EMBDEX_OK or a negative status code.
 */
int kc_embdex_open(kc_embdex_t **out, const char *path)
{
    kc_embdex_t *index;
    int status;

    if (out == NULL) {
        return KC_EMBDEX_EINVAL;
    }
    *out = NULL;
    if (path == NULL || path[0] == '\0') {
        return KC_EMBDEX_EINVAL;
    }
    index = kc_embdex_allocate(0);
    if (index == NULL) {
        return KC_EMBDEX_ENOMEM;
    }
    if (kc_mmap_open(&index->mapping, path) != KC_MMAP_OK) {
        kc_embdex_close(index);
        return KC_EMBDEX_EIO;
    }
    status = kc_embdex_load_records(index);
    if (status == KC_EMBDEX_OK) {
        const void *mapped = NULL;
        size_t mapped_size = 0;
        int lexical_status = KC_BM25_EFORMAT;

        if (kc_mmap_get(index->mapping, &mapped, &mapped_size) == KC_MMAP_OK &&
            kc_embdex_fits(mapped_size, index->lexical_offset,
                index->lexical_size))
        {
            lexical_status = kc_bm25_import(&index->lexical,
                (const unsigned char *)mapped + index->lexical_offset,
                index->lexical_size);
        }
        if (lexical_status != KC_BM25_OK)
            status = lexical_status == KC_BM25_ENOMEM ?
                KC_EMBDEX_ENOMEM : KC_EMBDEX_EFORMAT;
        else if (kc_bm25_count(index->lexical) != index->count)
            status = KC_EMBDEX_EFORMAT;
    }
    if (status == KC_EMBDEX_OK && index->count > KC_EMBDEX_EXACT_LIMIT)
        status = kc_embdex_prepare_graph(index);
    if (status != KC_EMBDEX_OK) {
        kc_embdex_close(index);
        return status;
    }
    *out = index;
    return KC_EMBDEX_OK;
}

/**
 * Compute a checked serialized size for a built index.
 * @param index Index to measure.
 * @param out_size Receives the serialized byte size.
 * @return KC_EMBDEX_OK or a negative status code.
 */
static int kc_embdex_serialized_size(
    const kc_embdex_t *index,
    size_t *out_size
)
{
    size_t total = KC_EMBDEX_HEADER_SIZE;

    if (index->dimension > SIZE_MAX / sizeof(float)) {
        return KC_EMBDEX_EFORMAT;
    }
    for (size_t i = 0; i < index->count; i++) {
        size_t id_length = strlen(index->records[i].id);
        size_t content_length = strlen(index->records[i].content);
        size_t vector_size = index->dimension * sizeof(float);

        if (id_length > UINT32_MAX || content_length > UINT32_MAX ||
            id_length > SIZE_MAX - 1 ||
            content_length > SIZE_MAX - 1) {
            return KC_EMBDEX_EFORMAT;
        }
        if (id_length > SIZE_MAX - 9 ||
            !kc_embdex_fits(SIZE_MAX, total, id_length + 9)) {
            return KC_EMBDEX_EFORMAT;
        }
        total += id_length + 9;
        if (!kc_embdex_fits(SIZE_MAX, total, content_length + 1)) {
            return KC_EMBDEX_EFORMAT;
        }
        total += content_length + 1;
        if (!kc_embdex_fits(SIZE_MAX, total, vector_size)) {
            return KC_EMBDEX_EFORMAT;
        }
        total += vector_size;
    }
    *out_size = total;
    return KC_EMBDEX_OK;
}

/**
 * Save a built index with little-endian lengths and vectors.
 * @param index Index created by kc_embdex_create().
 * @param path Binary destination file.
 * @return KC_EMBDEX_OK or a negative status code.
 */
static int kc_embdex_save(const kc_embdex_t *index, const char *path)
{
    kc_mmap_t *mapping = NULL;
    unsigned char *data;
    void *lexical_bytes = NULL;
    size_t size, lexical_size = 0, record_size;
    size_t offset = KC_EMBDEX_HEADER_SIZE;
    int status = KC_EMBDEX_EIO;

    if (index == NULL || path == NULL || path[0] == '\0') {
        return KC_EMBDEX_EINVAL;
    }
    if (index->mapping != NULL) {
        return KC_EMBDEX_ESTATE;
    }
    if (index->lexical == NULL) return KC_EMBDEX_ESTATE;
    status = kc_embdex_serialized_size(index, &record_size);
    if (status != KC_EMBDEX_OK) return status;
    {
        int lexical_status = kc_bm25_export(index->lexical,
            &lexical_bytes, &lexical_size);

        if (lexical_status != KC_BM25_OK)
            return lexical_status == KC_BM25_ENOMEM ?
                KC_EMBDEX_ENOMEM : KC_EMBDEX_ELEXICAL;
    }
    if (record_size > SIZE_MAX - lexical_size) {
        kc_bm25_free(lexical_bytes);
        return KC_EMBDEX_ENOMEM;
    }
    size = record_size + lexical_size;
    data = (unsigned char *)malloc(size);
    if (data == NULL) {
        kc_bm25_free(lexical_bytes);
        return KC_EMBDEX_ENOMEM;
    }
    memcpy(data, kc_embdex_magic, 8);
    kc_embdex_write_u32(data + 8, KC_EMBDEX_FORMAT_VERSION);
    kc_embdex_write_u32(data + 12, (uint32_t)index->dimension);
    kc_embdex_write_u64(data + 16, (uint64_t)index->count);
    memcpy(data + 24, kc_embdex_model, KC_EMBDEX_MODEL_SIZE);
    kc_embdex_write_u64(data + 56, (uint64_t)record_size);
    kc_embdex_write_u64(data + 64, (uint64_t)lexical_size);

    for (size_t i = 0; i < index->count; i++) {
        size_t id_length = strlen(index->records[i].id);
        size_t content_length = strlen(index->records[i].content);

        kc_embdex_write_u32(data + offset, (uint32_t)id_length);
        kc_embdex_write_u32(data + offset + 4, (uint32_t)content_length);
        offset += 8;
        memcpy(data + offset, index->records[i].id, id_length + 1);
        offset += id_length + 1;
        memcpy(
            data + offset,
            index->records[i].content,
            content_length + 1
        );
        offset += content_length + 1;
        for (size_t j = 0; j < index->dimension; j++) {
            kc_embdex_write_float(
                data + offset + j * sizeof(float),
                index->records[i].owned_vector[j]
            );
        }
        offset += index->dimension * sizeof(float);
    }
    if (offset != record_size) {
        kc_bm25_free(lexical_bytes);
        free(data);
        return KC_EMBDEX_EFORMAT;
    }
    memcpy(data + offset, lexical_bytes, lexical_size);
    kc_bm25_free(lexical_bytes);

    status = KC_EMBDEX_EIO;
    if (kc_mmap_open(&mapping, path) == KC_MMAP_OK) {
        if (kc_mmap_set(mapping, data, size) == KC_MMAP_OK &&
            kc_mmap_save(mapping) == KC_MMAP_OK) {
            status = KC_EMBDEX_OK;
        }
    }
    kc_mmap_close(mapping);
    free(data);
    return status;
}

/**
 * Create a persistent index, closing the temporary build resources.
 * @param path File destination.
 * @param documents Borrowed associations.
 * @param count Number of associations.
 * @return KC_EMBDEX_OK or a negative error code.
 */
int kc_embdex_build(const char *path,
    const kc_embdex_document_t *documents, size_t count)
{
    kc_embdex_t *index = NULL;
    int status;

    if (path == NULL || path[0] == '\0') return KC_EMBDEX_EINVAL;
    status = kc_embdex_create(&index, documents, count);
    if (status != KC_EMBDEX_OK) return status;
    status = kc_embdex_save(index, path);
    kc_embdex_close(index);
    return status;
}

typedef struct {
    char *value[KC_EMBDEX_QUERY_TOKENS];
    size_t count;
    int failed;
    int truncated;
} kc_embdex_tokens_t;

typedef struct {
    uint64_t exact;
    uint64_t fuzzy;
    uint64_t phrase;
    size_t longest;
    size_t lexical_rank;
    double semantic;
    int semantic_found;
    int semantic_measured;
    size_t semantic_rank;
    int full_phrase;
    int accepted;
    size_t final_rank;
    double score;
    double coverage;
    double precision;
    const char *reason;
} kc_embdex_evidence_t;

typedef struct {
    size_t ordinal;
    double score;
} kc_embdex_rank_t;

typedef struct {
    kc_embdex_evidence_t *evidence;
    size_t count;
    size_t position;
    int fuzzy;
    int failed;
    size_t found;
} kc_embdex_post_context_t;

typedef struct {
    const char *word;
    size_t length;
    size_t maximum;
    size_t best;
    const char *choices[KC_EMBDEX_FUZZY_CHOICES];
    size_t count;
    int ambiguous;
    int failed;
} kc_embdex_fuzzy_context_t;

typedef struct {
    const char *document;
    kc_embdex_evidence_t *evidence;
} kc_embdex_phrase_context_t;

/**
 * Keep the first bounded set of normalized query words in owned storage.
 * @param word Borrowed normalized word.
 * @param userdata Query token accumulator.
 * @return Zero to continue, nonzero when the bound or memory is exhausted.
 */
static int kc_embdex_token_add(const char *word, void *userdata)
{
    kc_embdex_tokens_t *tokens = (kc_embdex_tokens_t *)userdata;
    size_t size;
    char *copy;

    if (tokens->count >= KC_EMBDEX_QUERY_TOKENS) {
        tokens->truncated = 1;
        return 0;
    }
    size = strlen(word);
    if (size == SIZE_MAX) {
        tokens->failed = 1;
        return 1;
    }
    copy = (char *)malloc(size + 1);
    if (copy == NULL) {
        tokens->failed = 1;
        return 1;
    }
    memcpy(copy, word, size + 1);
    tokens->value[tokens->count++] = copy;
    return 0;
}

/**
 * Release normalized query word storage.
 * @param tokens Owned query token collection.
 * @return None.
 */
static void kc_embdex_tokens_close(kc_embdex_tokens_t *tokens)
{
    for (size_t i = 0; i < tokens->count; i++)
        free(tokens->value[i]);
    tokens->count = 0;
}

/**
 * Join normalized query words using unambiguous ASCII word separators.
 * @param tokens Normalized query tokens.
 * @return Owned joined string, or NULL when allocation fails.
 */
static char *kc_embdex_join(const kc_embdex_tokens_t *tokens)
{
    size_t size = 1, offset = 0;
    char *joined;

    for (size_t i = 0; i < tokens->count; i++) {
        size_t length = strlen(tokens->value[i]);

        if (length > SIZE_MAX - size - 1) return NULL;
        size += length + 1;
    }
    joined = (char *)malloc(size);
    if (joined == NULL) return NULL;
    for (size_t i = 0; i < tokens->count; i++) {
        size_t length = strlen(tokens->value[i]);

        if (i > 0) joined[offset++] = ' ';
        memcpy(joined + offset, tokens->value[i], length);
        offset += length;
    }
    joined[offset] = '\0';
    return joined;
}

typedef struct {
    char *text;
    size_t used;
    size_t capacity;
    int failed;
} kc_embdex_text_t;

/**
 * Append one normalized term to a bounded dynamically allocated string.
 * @param word Borrowed normalized word.
 * @param userdata String accumulator.
 * @return Zero on success and nonzero on allocation failure.
 */
static int kc_embdex_text_append(const char *word, void *userdata)
{
    kc_embdex_text_t *text = (kc_embdex_text_t *)userdata;
    size_t length = strlen(word), required;
    char *next;

    if (length > SIZE_MAX - text->used - 2) {
        text->failed = 1;
        return 1;
    }
    required = text->used + length + 2;
    if (required > text->capacity) {
        size_t capacity = text->capacity == 0 ? 64 : text->capacity;

        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                text->failed = 1;
                return 1;
            }
            capacity *= 2;
        }
        next = (char *)realloc(text->text, capacity);
        if (next == NULL) {
            text->failed = 1;
            return 1;
        }
        text->text = next;
        text->capacity = capacity;
    }
    if (text->used > 0) text->text[text->used++] = ' ';
    memcpy(text->text + text->used, word, length);
    text->used += length;
    text->text[text->used] = '\0';
    return 0;
}

/**
 * Count Unicode scalar values in an already validated normalized token.
 * @param word BM25-normalized UTF-8 word.
 * @return Number of Unicode scalar values.
 */
static size_t kc_embdex_length(const char *word)
{
    size_t length = 0;

    for (const unsigned char *p = (const unsigned char *)word; *p; p++)
        if ((*p & 0xc0U) != 0x80U) length++;
    return length;
}

/**
 * Record document matches for one normalized exact or corrected term.
 * @param ordinal Matching document ordinal.
 * @param frequency Term frequency, unused for coverage.
 * @param userdata Current query-word evidence context.
 * @return Zero on success, nonzero when ordinal is invalid.
 */
static int kc_embdex_posting_add(size_t ordinal, size_t frequency,
    void *userdata)
{
    kc_embdex_post_context_t *context =
        (kc_embdex_post_context_t *)userdata;
    kc_embdex_evidence_t *evidence;

    (void)frequency;
    if (ordinal >= context->count) {
        context->failed = 1;
        return 1;
    }
    evidence = &context->evidence[ordinal];
    if (context->fuzzy)
        evidence->fuzzy |= UINT64_C(1) << context->position;
    else
        evidence->exact |= UINT64_C(1) << context->position;
    context->found++;
    return 0;
}

/**
 * Retain nearest spelling alternatives in the actual indexed vocabulary.
 * @param term Normalized vocabulary term.
 * @param document_count Number of matching associations, unused.
 * @param userdata Fuzzy candidate collector.
 * @return Zero to keep scanning, nonzero on an internal error.
 */
static int kc_embdex_fuzzy_visit(const char *term, size_t document_count,
    void *userdata)
{
    kc_embdex_fuzzy_context_t *context =
        (kc_embdex_fuzzy_context_t *)userdata;
    size_t length = kc_embdex_length(term), distance = 0;
    int status;

    (void)document_count;
    if (length > context->length + context->maximum ||
        context->length > length + context->maximum)
        return 0;
    status = kc_leven_distance(context->word, term, &context->maximum,
        &distance);
    if (status == KC_LEVEN_ELIMIT) return 0;
    if (status != KC_LEVEN_OK) {
        context->failed = status;
        return 1;
    }
    if (distance == 0 || distance > context->best) return 0;
    if (distance < context->best) {
        context->best = distance;
        context->count = 0;
        context->ambiguous = 0;
    }
    if (context->count < KC_EMBDEX_FUZZY_CHOICES)
        context->choices[context->count++] = term;
    else context->ambiguous = 1;
    return 0;
}

/**
 * Determine whether a normalized document contains a complete word span.
 * @param document Space-separated normalized document.
 * @param phrase Text bytes for a normalized word sequence.
 * @param length Exact byte length of the phrase.
 * @return One on a whole-token phrase match, otherwise zero.
 */
static int kc_embdex_contains(const char *document,
    const char *phrase, size_t length)
{
    for (const char *p = document; *p; p++) {
        size_t remaining;

        if (p != document && p[-1] != ' ') continue;
        remaining = strlen(p);
        if (remaining >= length && strncmp(p, phrase, length) == 0 &&
            (remaining == length || p[length] == ' '))
            return 1;
    }
    return 0;
}

/**
 * Measure matching query windows without counting overlapping tokens twice.
 * @param chunk Current n-gram window in the normalized query.
 * @param userdata Candidate document and its evidence.
 * @return Zero to continue traversing.
 */
static int kc_embdex_phrase_visit(const kc_ngram_chunk_t *chunk,
    void *userdata)
{
    kc_embdex_phrase_context_t *context =
        (kc_embdex_phrase_context_t *)userdata;

    if (kc_embdex_contains(context->document,
        chunk->data, chunk->data_size))
    {
        for (size_t j = 0; j < chunk->token_count; j++)
            context->evidence->phrase |=
                UINT64_C(1) << (chunk->token_start + j);
        if (chunk->token_count > context->evidence->longest)
            context->evidence->longest = chunk->token_count;
    }
    return 0;
}

/**
 * Count recognized query positions without double-counting window overlaps.
 * @param bits Query-position coverage bitmap.
 * @return Number of distinct covered positions.
 */
static size_t kc_embdex_popcount(uint64_t bits)
{
    size_t count = 0;

    while (bits != 0) {
        bits &= bits - 1;
        count++;
    }
    return count;
}

/**
 * Compute normalized cosine similarity against one persisted document vector.
 * @param index Opened index.
 * @param ordinal Document ordinal.
 * @param query Query embedding.
 * @return Similarity in [-1, 1], or zero for empty vectors.
 */
static double kc_embdex_cosine(const kc_embdex_t *index,
    size_t ordinal, const float *query)
{
    const kc_embdex_record_t *record = &index->records[ordinal];
    double dot = 0, left = 0, right = 0;

    for (size_t j = 0; j < index->dimension; j++) {
        double a = (double)query[j];
        double b = record->owned_vector != NULL ?
            (double)record->owned_vector[j] :
            (double)kc_embdex_read_float(record->encoded +
                j * sizeof(float));

        dot += a * b;
        left += a * a;
        right += b * b;
    }
    if (left == 0 || right == 0) return 0;
    {
        double score = dot / sqrt(left * right);

        return score > 1 ? 1 : score < -1 ? -1 : score;
    }
}

/**
 * Compare scalar similarity values for a small sorted reference sample.
 * @param left First double.
 * @param right Second double.
 * @return Comparison order.
 */
static int kc_embdex_double_compare(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;

    return a < b ? -1 : a > b ? 1 : 0;
}

/**
 * Require an uncorroborated vector neighbor to be unusual relative to the
 * query's similarity to representative indexed associations. A bounded
 * stratified sample avoids exhaustive work on large HNSW indexes.
 * @param index Opened index.
 * @param query Calculated query embedding.
 * @return Minimum independent semantic acceptance similarity.
 */
static double kc_embdex_semantic_threshold(const kc_embdex_t *index,
    const float *query)
{
    double similarities[64];
    double threshold = 0.58;
    size_t sample_count = index->count < 64 ? index->count : 64;

    if (sample_count < 5) return threshold;
    for (size_t i = 0; i < sample_count; i++) {
        size_t ordinal = (index->count / sample_count) * i +
            ((index->count % sample_count) * i) / sample_count;

        similarities[i] = kc_embdex_cosine(index, ordinal, query);
    }
    qsort(similarities, sample_count, sizeof(*similarities),
        kc_embdex_double_compare);
    {
        double reference = similarities[(sample_count * 3) / 4] + 0.07;

        if (reference > threshold) threshold = reference;
    }
    return threshold;
}

/**
 * Sort accepted associations by descending evidence score and stable ordinal.
 * @param left First candidate.
 * @param right Second candidate.
 * @return Comparison order.
 */
static int kc_embdex_rank_compare(const void *left, const void *right)
{
    const kc_embdex_rank_t *a = (const kc_embdex_rank_t *)left;
    const kc_embdex_rank_t *b = (const kc_embdex_rank_t *)right;

    if (a->score > b->score) return -1;
    if (a->score < b->score) return 1;
    if (a->ordinal < b->ordinal) return -1;
    if (a->ordinal > b->ordinal) return 1;
    return 0;
}

/**
 * Write one UTF-8 string escaped as JSON, including surrounding quotes.
 * @param file Open trace output file.
 * @param value NUL-terminated borrowed text.
 * @return None.
 */
static void kc_embdex_json_string(FILE *file, const char *value)
{
    const unsigned char *p = (const unsigned char *)value;

    fputc('"', file);
    for (; *p != 0; p++) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', file);
            fputc(*p, file);
        } else if (*p < 32U) {
            fprintf(file, "\\u%04x", (unsigned int)*p);
        } else {
            fputc(*p, file);
        }
    }
    fputc('"', file);
}

/**
 * Append opt-in diagnostic evidence without changing the public search API.
 * The KC_EMBDEX_TRACE environment variable supplies the destination JSONL
 * path. It is intentionally inactive unless an application explicitly opts in.
 * @param index Opened index with candidate ordinals.
 * @param query Original query text.
 * @param evidence Evidence records for every indexed association.
 * @param count Total record count.
 * @return None; diagnostic I/O never changes the search outcome.
 */
static void kc_embdex_trace(const kc_embdex_t *index, const char *query,
    const kc_embdex_evidence_t *evidence, size_t count,
    double semantic_threshold)
{
    const char *path = getenv("KC_EMBDEX_TRACE");
    FILE *file;

    if (path == NULL || path[0] == '\0') return;
    file = fopen(path, "a");
    if (file == NULL) return;

    for (size_t i = 0; i < count; i++) {
        const kc_embdex_evidence_t *item = &evidence[i];

        fputs("{\"query\":", file);
        kc_embdex_json_string(file, query);
        fprintf(file, ",\"ordinal\":%zu,\"semantic_threshold\":%.9g,\"id\":",
            i, semantic_threshold);
        kc_embdex_json_string(file, index->records[i].id);
        fprintf(file, ",\"content\":");
        kc_embdex_json_string(file, index->records[i].content);
        fprintf(file, ",\"exact\":\"%016" PRIx64
            "\",\"fuzzy\":\"%016" PRIx64
            "\",\"phrase\":\"%016" PRIx64
            "\",\"longest\":%zu,\"bm25_rank\":%zu",
            item->exact, item->fuzzy, item->phrase,
            item->longest, item->lexical_rank);
        fputs(",\"cosine\":", file);
        if (item->semantic_measured)
            fprintf(file, "%.9g", item->semantic);
        else fputs("null", file);
        fprintf(file, ",\"coverage\":%.9g,\"precision\":%.9g,"
            "\"full_phrase\":%s,\"accepted\":%s,\"score\":",
            item->coverage, item->precision,
            item->full_phrase ? "true" : "false",
            item->accepted ? "true" : "false");
        if (item->accepted) fprintf(file, "%.9g", item->score);
        else fputs("null", file);
        fprintf(file, ",\"semantic_rank\":%zu,\"final_rank\":%zu,\"reason\":",
            item->semantic_rank, item->final_rank);
        kc_embdex_json_string(file,
            item->reason == NULL ? "not_discovered" : item->reason);
        fputs("}\n", file);
    }
    fclose(file);
}

/**
 * Determine whether independent retrieval evidence justifies a result.
 * The BM25 rank is a discovery signal, not an acceptance shortcut.
 * @param item Combined evidence for one association.
 * @param total_query_tokens Number of normalized query words.
 * @param exact_count Positions matching the exact vocabulary.
 * @param corrected_count Positions matching corrected vocabulary words.
 * @param coverage Corpus-rarity-weighted fraction of justified words.
 * @param semantic_floor Query-relative acceptance threshold.
 * @param truncated Whether the query exceeded the lexical token bound.
 * @return Stable explanation of acceptance, or NULL when rejected.
 */
static const char *kc_embdex_accept(const kc_embdex_evidence_t *item,
    size_t total_query_tokens, size_t exact_count,
    size_t corrected_count, double coverage,
    double semantic_floor, int truncated)
{
    double similarity = item->semantic_found ? item->semantic : 0;

    if (!truncated && item->full_phrase)
        return "complete_phrase";
    if (!truncated && coverage >= 0.80 &&
        (exact_count + corrected_count >= 2 ||
            (total_query_tokens == 1 &&
                exact_count + corrected_count == 1)))
        return "strong_lexical_coverage";
    if (!truncated && exact_count > 0 && corrected_count > 0 &&
        corrected_count <= exact_count + 1 && coverage >= 0.18)
        return "corroborated_spelling";
    if (!truncated && exact_count >= 2 && coverage >= 0.30 &&
        item->semantic_found && similarity + 0.03 >= semantic_floor)
        return "corroborated_lexical_evidence";
    if (item->semantic_found && similarity >= semantic_floor + 0.04)
        return "strong_semantic_evidence";
    if (!truncated && item->semantic_found &&
        similarity >= 0.62 && similarity + 0.02 >= semantic_floor &&
        coverage >= 0.10 && (exact_count > 0 || corrected_count > 0))
        return "corroborated_semantic_evidence";
    if (!truncated && item->semantic_found &&
        item->semantic_rank > 0 && item->semantic_rank <= 3 &&
        similarity + 0.04 >= semantic_floor &&
        coverage >= 0.05)
        return "ranked_semantic_evidence";
    return NULL;
}

/**
 * Rank accepted associations after the acceptance decision is complete.
 * BM25 contributes only in proportion to supported lexical coverage.
 * @param item Evidence for an already accepted association.
 * @param coverage Rarity-weighted lexical coverage.
 * @param precision Rarity-weighted exact coverage.
 * @param corrected_weight Rarity-weighted corrected coverage.
 * @param phrase_ratio Longest matching phrase relative to query length.
 * @return Deterministic score used only for sorting accepted matches.
 */
static double kc_embdex_score(const kc_embdex_evidence_t *item,
    double coverage, double precision, double corrected_weight,
    double phrase_ratio)
{
    double bm25 = item->lexical_rank != 0 ?
        10.0 / (10.0 + (double)item->lexical_rank) : 0.0;
    double cosine = item->semantic_found ? item->semantic : 0;
    double score = 0.45 * coverage + 0.25 * precision +
        0.15 * phrase_ratio + 0.10 * bm25 * coverage +
        0.50 * cosine - 0.08 * corrected_weight;

    if (item->full_phrase) score += 0.50;
    return score;
}

/**
 * Find relevant associations from independent lexical and semantic evidence.
 * Query coverage, phrase matching and edit-distance uncertainty influence
 * selection; lexical and semantic candidates are admitted through separate
 * confidence checks before ranking.
 * @param index Opened persistent index.
 * @param query Natural-language text.
 * @param options Optional maximum number of results.
 * @param callback Receiver for borrowed results.
 * @param userdata Receiver context.
 * @return KC_EMBDEX_OK or a negative status.
 */
int kc_embdex_search(const kc_embdex_t *index, const char *query,
    const kc_embdex_search_options_t *options,
    kc_embdex_result_fn callback, void *userdata)
{
    const double semantic_floor = 0.50;
    double semantic_only_floor = 0.58;
    kc_embdex_tokens_t tokens = {0};
    kc_embdex_evidence_t *evidence = NULL;
    kc_embdex_rank_t *ranks = NULL;
    kc_bm25_result_t *lexical = NULL;
    kc_hnsw_result_t *semantic = NULL;
    kc_bm25_search_options_t lexical_options = {0};
    char *joined = NULL;
    float *embedding = NULL;
    size_t dimension = 0, limit = 10, window, lexical_count = 0;
    size_t semantic_count = 0, rank_count = 0;
    double weights[KC_EMBDEX_QUERY_TOKENS] = {0};
    double total_weight = 0;
    int status = KC_EMBDEX_OK;

    if (index == NULL || query == NULL || callback == NULL ||
        index->lexical == NULL) return KC_EMBDEX_EINVAL;
    if (options != NULL && options->limit != NULL)
        limit = *options->limit;
    if (limit == 0 || index->count == 0 || query[0] == '\0')
        return KC_EMBDEX_OK;
    if (index->count > SIZE_MAX / sizeof(*evidence) ||
        index->count > SIZE_MAX / sizeof(*ranks))
        return KC_EMBDEX_ENOMEM;

    evidence = (kc_embdex_evidence_t *)calloc(index->count,
        sizeof(*evidence));
    ranks = (kc_embdex_rank_t *)calloc(index->count, sizeof(*ranks));
    if (evidence == NULL || ranks == NULL) {
        status = KC_EMBDEX_ENOMEM;
        goto finished;
    }
    {
        int result = kc_bm25_tokens(index->lexical, query,
            kc_embdex_token_add, &tokens);

        if (tokens.failed) {
            status = KC_EMBDEX_ENOMEM;
            goto finished;
        }
        if (result != KC_BM25_OK)
        {
            status = KC_EMBDEX_ELEXICAL;
            goto finished;
        }
    }
    if (tokens.count == 0) goto finished;
    joined = kc_embdex_join(&tokens);
    if (joined == NULL) {
        status = KC_EMBDEX_ENOMEM;
        goto finished;
    }

    for (size_t i = 0; i < tokens.count; i++) {
        kc_embdex_post_context_t post = {
            evidence, index->count, i, 0, 0, 0
        };
        size_t exact_frequency;
        int result = kc_bm25_postings(index->lexical,
            tokens.value[i], kc_embdex_posting_add, &post);

        if (result != KC_BM25_OK || post.failed) {
            status = KC_EMBDEX_ELEXICAL;
            goto finished;
        }
        exact_frequency = post.found;
        if (exact_frequency == 0) {
            size_t length = kc_embdex_length(tokens.value[i]);
            size_t maximum = length >= 8 ? 2 : length >= 5 ? 1 : 0;

            if (maximum > 0) {
                kc_embdex_fuzzy_context_t fuzzy = {0};

                fuzzy.word = tokens.value[i];
                fuzzy.length = length;
                fuzzy.maximum = maximum;
                fuzzy.best = maximum;
                result = kc_bm25_vocabulary(index->lexical,
                    kc_embdex_fuzzy_visit, &fuzzy);
                if (result != KC_BM25_OK || fuzzy.failed) {
                    status = fuzzy.failed == KC_LEVEN_ENOMEM ?
                        KC_EMBDEX_ENOMEM : KC_EMBDEX_ELEXICAL;
                    goto finished;
                }
                if (!fuzzy.ambiguous && (tokens.count > 1 ||
                    (fuzzy.best == 1 && fuzzy.count == 1))) {
                    post.fuzzy = 1;
                    for (size_t k = 0; k < fuzzy.count; k++) {
                        result = kc_bm25_postings(index->lexical,
                            fuzzy.choices[k], kc_embdex_posting_add,
                            &post);
                        if (result != KC_BM25_OK || post.failed) {
                            status = KC_EMBDEX_ELEXICAL;
                            goto finished;
                        }
                    }
                }
            }
        }
        {
            size_t frequency = exact_frequency;
            double rarity;

            /* Unknown words have the rarity of a singleton, not infinity.
             * Fuzzy postings never change the document frequency. */
            if (frequency == 0) frequency = 1;
            if (frequency > index->count) frequency = index->count;
            rarity = log(1.0 + ((double)(index->count - frequency) + 0.5) /
                ((double)frequency + 0.5));
            weights[i] = rarity;
            total_weight += rarity;
        }
    }

    window = limit > 100 ? limit : 100;
    if (window > index->count) window = index->count;
    lexical_options.limit = &window;
    {
        int result = kc_bm25_search(index->lexical, query,
            &lexical_options, &lexical, &lexical_count);

        if (result != KC_BM25_OK) {
            status = result == KC_BM25_ENOMEM ?
                KC_EMBDEX_ENOMEM : KC_EMBDEX_ELEXICAL;
            goto finished;
        }
    }
    for (size_t i = 0; i < lexical_count; i++) {
        if (lexical[i].ordinal >= index->count) {
            status = KC_EMBDEX_EINDEX;
            goto finished;
        }
        evidence[lexical[i].ordinal].lexical_rank = i + 1;
    }

    for (size_t i = 0; i < index->count; i++) {
        kc_embdex_text_t document = {0};
        kc_embdex_phrase_context_t phrase;
        kc_ngram_options_t ngram_options = {0};
        size_t minimum = 2, maximum = 4;
        int result;

        if (evidence[i].exact == 0 && evidence[i].fuzzy == 0 &&
            evidence[i].lexical_rank == 0) continue;
        result = kc_bm25_tokens(index->lexical,
            index->records[i].content, kc_embdex_text_append, &document);
        if (result != KC_BM25_OK || document.failed) {
            free(document.text);
            status = document.failed ? KC_EMBDEX_ENOMEM :
                KC_EMBDEX_ELEXICAL;
            goto finished;
        }
        if (document.text == NULL) {
            document.text = kc_embdex_copy_string("");
            if (document.text == NULL) {
                status = KC_EMBDEX_ENOMEM;
                goto finished;
            }
        }
        evidence[i].full_phrase = !tokens.truncated &&
            kc_embdex_contains(document.text, joined, strlen(joined));
        if (evidence[i].full_phrase) {
            evidence[i].exact = tokens.count == 64 ?
                UINT64_MAX : (UINT64_C(1) << tokens.count) - 1;
            evidence[i].longest = tokens.count;
        } else if (tokens.count >= 2) {
            if (tokens.count < maximum) maximum = tokens.count;
            ngram_options.min_tokens = &minimum;
            ngram_options.max_tokens = &maximum;
            phrase.document = document.text;
            phrase.evidence = &evidence[i];
            if (kc_ngram_traverse(joined, &ngram_options,
                kc_embdex_phrase_visit, &phrase) != KC_NGRAM_OK)
            {
                free(document.text);
                status = KC_EMBDEX_EINDEX;
                goto finished;
            }
        }
        free(document.text);
    }

    {
        int exact_full = 0;

        for (size_t i = 0; i < index->count; i++)
            exact_full |= evidence[i].full_phrase;
        if (tokens.truncated || !exact_full || limit != 1) {
            if (kc_emb_embed(query, &embedding, &dimension) != KC_EMB_OK ||
                dimension != index->dimension)
            {
                status = KC_EMBDEX_EEMBED;
                goto finished;
            }
            semantic_only_floor = kc_embdex_semantic_threshold(index,
                embedding);
            if (index->graph == NULL) {
                for (size_t i = 0; i < index->count; i++) {
                    double cosine = kc_embdex_cosine(index, i, embedding);

                    evidence[i].semantic = cosine;
                    evidence[i].semantic_measured = 1;
                    if (cosine >= semantic_floor)
                        evidence[i].semantic_found = 1;
                }
                for (size_t i = 0; i < index->count; i++) {
                    ranks[i].ordinal = i;
                    ranks[i].score = evidence[i].semantic;
                }
                qsort(ranks, index->count, sizeof(*ranks),
                    kc_embdex_rank_compare);
                for (size_t i = 0; i < index->count; i++)
                    evidence[ranks[i].ordinal].semantic_rank = i + 1;
            } else {
                int result = kc_hnsw_search(index->graph,
                    embedding, window, semantic_floor,
                    &semantic, &semantic_count);

                if (result != KC_HNSW_OK) {
                    status = KC_EMBDEX_EINDEX;
                    goto finished;
                }
                for (size_t i = 0; i < semantic_count; i++) {
                    char *end;
                    uintmax_t ordinal = strtoumax(semantic[i].id, &end, 10);

                    if (semantic[i].id[0] == '\0' || *end != '\0' ||
                        ordinal >= index->count)
                    {
                        status = KC_EMBDEX_EINDEX;
                        goto finished;
                    }
                    evidence[(size_t)ordinal].semantic = semantic[i].score;
                    evidence[(size_t)ordinal].semantic_measured = 1;
                    evidence[(size_t)ordinal].semantic_found = 1;
                    evidence[(size_t)ordinal].semantic_rank = i + 1;
                }
            }
        }
    }

    for (size_t i = 0; i < index->count; i++) {
        kc_embdex_evidence_t *item = &evidence[i];
        uint64_t mask = item->exact | item->fuzzy | item->phrase;
        size_t exact = kc_embdex_popcount(item->exact);
        size_t corrected = kc_embdex_popcount(item->fuzzy & ~item->exact);
        double weight_covered = 0, weight_exact = 0, weight_corrected = 0;
        double coverage, precise;
        double phrase = (double)item->longest / (double)tokens.count;

        for (size_t j = 0; j < tokens.count; j++) {
            uint64_t bit = UINT64_C(1) << j;

            if ((mask & bit) != 0) weight_covered += weights[j];
            if ((item->exact & bit) != 0) weight_exact += weights[j];
            if ((item->fuzzy & ~item->exact & bit) != 0)
                weight_corrected += weights[j];
        }
        coverage = total_weight > 0 ? weight_covered / total_weight : 0;
        precise = total_weight > 0 ? weight_exact / total_weight : 0;
        item->coverage = coverage;
        item->precision = precise;
        double score;
        const char *reason = kc_embdex_accept(item, tokens.count,
            exact, corrected, coverage, semantic_only_floor,
            tokens.truncated);

        if (reason == NULL) {
            item->reason = mask != 0 || item->lexical_rank != 0 ||
                item->semantic_found ? "insufficient_evidence" :
                "not_discovered";
            continue;
        }
        item->accepted = 1;
        item->reason = reason;
        score = kc_embdex_score(item, coverage, precise,
            total_weight > 0 ? weight_corrected / total_weight : 0,
            phrase);
        item->score = score;
        ranks[rank_count].ordinal = i;
        ranks[rank_count++].score = score;
    }

    qsort(ranks, rank_count, sizeof(*ranks), kc_embdex_rank_compare);
    for (size_t i = 0; i < rank_count; i++)
        evidence[ranks[i].ordinal].final_rank = i + 1;
    if (rank_count > limit) rank_count = limit;
    for (size_t i = 0; i < rank_count; i++) {
        size_t ordinal = ranks[i].ordinal;
        kc_embdex_result_t result = {
            index->records[ordinal].id,
            index->records[ordinal].content,
            ranks[i].score
        };

        if (callback(&result, userdata) != 0) break;
    }

finished:
    if (status == KC_EMBDEX_OK && evidence != NULL && tokens.count > 0)
        kc_embdex_trace(index, query, evidence, index->count,
            semantic_only_floor);
    kc_bm25_free(lexical);
    kc_hnsw_free(semantic);
    kc_emb_free(embedding);
    kc_embdex_tokens_close(&tokens);
    free(joined);
    free(evidence);
    free(ranks);
    return status;
}

/**
 * Release all resources held by a semantic index.
 * @param index Index handle or NULL.
 * @return void
 */
void kc_embdex_close(kc_embdex_t *index)
{
    if (index == NULL) {
        return;
    }
    kc_hnsw_close(index->graph);
    kc_bm25_close(index->lexical);
    for (size_t i = 0; i < index->count; i++) {
        if (index->records != NULL) {
            if (index->records[i].owned_strings) {
                free((void *)index->records[i].id);
                free((void *)index->records[i].content);
            }
            kc_emb_free(index->records[i].owned_vector);
        }
    }
    free(index->records);
    kc_mmap_close(index->mapping);
    free(index);
}

/**
 * Describe a public semantic index status code.
 * @param status Status to describe.
 * @return Static text.
 */
const char *kc_embdex_strerror(int status)
{
    switch (status) {
        case KC_EMBDEX_OK:
            return "success";
        case KC_EMBDEX_EINVAL:
            return "invalid argument";
        case KC_EMBDEX_ENOMEM:
            return "out of memory";
        case KC_EMBDEX_EIO:
            return "file I/O error";
        case KC_EMBDEX_EFORMAT:
            return "incompatible or invalid index";
        case KC_EMBDEX_EEMBED:
            return "embedding failed";
        case KC_EMBDEX_EINDEX:
            return "vector index failed";
        case KC_EMBDEX_ESTATE:
            return "invalid index state";
        case KC_EMBDEX_ELEXICAL:
            return "lexical index failed";
        default:
            return "unknown error";
    }
}

/**
 * Return the build timestamp for this library.
 * @return Build timestamp.
 */
uint64_t kc_embdex_version(void)
{
    return (uint64_t)KC_EMBDEX_BUILD_VERSION;
}
