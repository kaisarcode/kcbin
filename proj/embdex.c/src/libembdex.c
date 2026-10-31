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

#define KC_EMBDEX_HEADER_SIZE 68U
#define KC_EMBDEX_MODEL_SIZE 32U

static const unsigned char kc_embdex_magic[8] = {
    'K', 'C', 'S', 'I', 'D', 'X', '\r', '\n'
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
    int status = kc_bm25_open(&index->lexical, NULL);

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
    status = kc_embdex_prepare_graph(index);
    if (status != KC_EMBDEX_OK) {
        goto failure;
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
        memcmp(data + 20, kc_embdex_model, KC_EMBDEX_MODEL_SIZE) != 0)
        return KC_EMBDEX_EFORMAT;
    {
        uint64_t lexical_offset = kc_embdex_read_u64(data + 52);
        uint64_t lexical_size = kc_embdex_read_u64(data + 60);

        if (lexical_offset > size || lexical_size == 0 ||
            lexical_size > size - lexical_offset ||
            lexical_offset + lexical_size != size)
            return KC_EMBDEX_EFORMAT;
        index->lexical_offset = (size_t)lexical_offset;
        index->lexical_size = (size_t)lexical_size;
    }
    index->dimension = kc_embdex_read_u32(data + 8);
    count = kc_embdex_read_u64(data + 12);
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
        status = kc_embdex_prepare_graph(index);
    }
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
    kc_embdex_write_u32(data + 8, (uint32_t)index->dimension);
    kc_embdex_write_u64(data + 12, (uint64_t)index->count);
    memcpy(data + 20, kc_embdex_model, KC_EMBDEX_MODEL_SIZE);
    kc_embdex_write_u64(data + 52, (uint64_t)record_size);
    kc_embdex_write_u64(data + 60, (uint64_t)lexical_size);

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
    size_t ordinal;
    double score;
} kc_embdex_rank_t;

/**
 * Compare candidates by descending score and insertion order.
 * @param left First candidate.
 * @param right Second candidate.
 * @return Sort order.
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
 * Accumulate an independent rank contribution by document ordinal.
 * @param ranks Preallocated candidate array.
 * @param count Current candidate count.
 * @param capacity Maximum candidate count.
 * @param ordinal Unique association ordinal.
 * @param score Additional rank evidence.
 * @return KC_EMBDEX_OK or an error status.
 */
static int kc_embdex_rank_add(kc_embdex_rank_t *ranks,
    size_t *count, size_t capacity, size_t ordinal, double score)
{
    for (size_t i = 0; i < *count; i++) {
        if (ranks[i].ordinal == ordinal) {
            ranks[i].score += score;
            return KC_EMBDEX_OK;
        }
    }
    if (*count == capacity) return KC_EMBDEX_EINDEX;
    ranks[*count].ordinal = ordinal;
    ranks[*count].score = score;
    (*count)++;
    return KC_EMBDEX_OK;
}

/**
 * Retrieve candidates independently with BM25 and HNSW and rank by ordinal.
 * This baseline fusion will be superseded by the planned evidence evaluator
 * after relevance and false-positive measurements.
 * @param index Opened index.
 * @param query UTF-8 query.
 * @param options Optional result limit.
 * @param callback Receives borrowed results and may stop delivery.
 * @param userdata Passed unchanged to callback.
 * @return KC_EMBDEX_OK or an error status.
 */
int kc_embdex_search(const kc_embdex_t *index, const char *query,
    const kc_embdex_search_options_t *options,
    kc_embdex_result_fn callback, void *userdata)
{
    const size_t base_window = 100;
    const double minimum_similarity = 0.35;
    kc_bm25_result_t *lexical = NULL;
    kc_hnsw_result_t *semantic = NULL;
    kc_embdex_rank_t *ranks = NULL;
    kc_bm25_search_options_t lexical_options = {0};
    float *embedding = NULL;
    size_t limit = 10, window, capacity, dimension = 0, rank_count = 0;
    size_t lexical_count = 0, semantic_count = 0, result_count;
    int status = KC_EMBDEX_OK;

    if (index == NULL || query == NULL || callback == NULL ||
        index->lexical == NULL || index->graph == NULL)
        return KC_EMBDEX_EINVAL;
    if (options != NULL && options->limit != NULL)
        limit = *options->limit;
    if (limit > INT_MAX) return KC_EMBDEX_EINVAL;
    if (limit == 0 || index->count == 0 || query[0] == '\0')
        return KC_EMBDEX_OK;

    window = limit > base_window ? limit : base_window;
    if (window > index->count) window = index->count;
    if (window > SIZE_MAX / 2 ||
        window * 2 > SIZE_MAX / sizeof(*ranks))
        return KC_EMBDEX_ENOMEM;
    capacity = window * 2;
    ranks = (kc_embdex_rank_t *)calloc(capacity, sizeof(*ranks));
    if (ranks == NULL) return KC_EMBDEX_ENOMEM;

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
        status = kc_embdex_rank_add(ranks, &rank_count, capacity,
            lexical[i].ordinal, 1.0 / (61.0 + (double)i));
        if (status != KC_EMBDEX_OK) goto finished;
    }

    if (kc_emb_embed(query, &embedding, &dimension) != KC_EMB_OK ||
        dimension != index->dimension)
    {
        status = KC_EMBDEX_EEMBED;
        goto finished;
    }
    if (kc_hnsw_search(index->graph, embedding, window,
        minimum_similarity, &semantic, &semantic_count) != KC_HNSW_OK)
    {
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
        status = kc_embdex_rank_add(ranks, &rank_count, capacity,
            (size_t)ordinal, 1.0 / (61.0 + (double)i));
        if (status != KC_EMBDEX_OK) goto finished;
    }

    qsort(ranks, rank_count, sizeof(*ranks), kc_embdex_rank_compare);
    result_count = rank_count < limit ? rank_count : limit;
    for (size_t i = 0; i < result_count; i++) {
        size_t ordinal = ranks[i].ordinal;
        kc_embdex_result_t result = {
            index->records[ordinal].id,
            index->records[ordinal].content,
            ranks[i].score
        };

        if (callback(&result, userdata) != 0) break;
    }

finished:
    kc_bm25_free(lexical);
    kc_hnsw_free(semantic);
    kc_emb_free(embedding);
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
