/**
 * libsemidx.c - Semantic Index Implementation
 * Summary: Composes embeddings, HNSW and mapped binary persistence.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libsemidx.h"
#include "libemb.h"
#include "libhnsw.h"
#include "libmmap.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_SEMIDX_BUILD_VERSION
#define KC_SEMIDX_BUILD_VERSION 0
#endif

#define KC_SEMIDX_FORMAT_VERSION 1U
#define KC_SEMIDX_HEADER_SIZE 56U
#define KC_SEMIDX_MODEL_SIZE 32U

static const unsigned char kc_semidx_magic[8] = {
    'K', 'C', 'S', 'I', 'D', 'X', '0', '1'
};
static const char kc_semidx_model[KC_SEMIDX_MODEL_SIZE] =
    "bge-small-en-v1.5";

typedef struct {
    const char *id;
    const char *content;
    const unsigned char *encoded;
    float *owned_vector;
    int owned_strings;
} kc_semidx_record_t;

struct kc_semidx {
    kc_semidx_record_t *records;
    size_t count;
    size_t dimension;
    kc_hnsw_t *graph;
    kc_mmap_t *mapping;
};

/**
 * Check whether a bounded slice fits in an encoded buffer.
 * @param size Total buffer size.
 * @param offset Offset to inspect.
 * @param length Required number of bytes.
 * @return Nonzero when the slice is valid.
 */
static int kc_semidx_fits(size_t size, size_t offset, size_t length)
{
    return offset <= size && length <= size - offset;
}

/**
 * Read one little-endian unsigned 32-bit value.
 * @param data Pointer to at least four encoded bytes.
 * @return Decoded value.
 */
static uint32_t kc_semidx_read_u32(const unsigned char *data)
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
static uint64_t kc_semidx_read_u64(const unsigned char *data)
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
static void kc_semidx_write_u32(unsigned char *data, uint32_t value)
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
static void kc_semidx_write_u64(unsigned char *data, uint64_t value)
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
static float kc_semidx_read_float(const unsigned char *data)
{
    uint32_t bits = kc_semidx_read_u32(data);
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
static void kc_semidx_write_float(unsigned char *data, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    kc_semidx_write_u32(data, bits);
}

/**
 * Copy a NUL-terminated string into index-owned storage.
 * @param input Input string.
 * @return Allocated copy, or NULL on allocation failure.
 */
static char *kc_semidx_copy_string(const char *input)
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
static kc_semidx_t *kc_semidx_allocate(size_t count)
{
    kc_semidx_t *index = (kc_semidx_t *)calloc(1, sizeof(*index));

    if (index == NULL) {
        return NULL;
    }
    if (count > SIZE_MAX / sizeof(*index->records)) {
        free(index);
        return NULL;
    }
    if (count > 0) {
        index->records = (kc_semidx_record_t *)calloc(
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
 * @return KC_SEMIDX_OK or a negative status code.
 */
static int kc_semidx_prepare_graph(kc_semidx_t *index)
{
    kc_hnsw_options_t options = {0};
    float *decoded = NULL;
    int status = KC_SEMIDX_EINDEX;

    options.dimension = index->dimension;
    if (kc_hnsw_open(&index->graph, &options) != KC_HNSW_OK) {
        return KC_SEMIDX_EINDEX;
    }
    if (index->mapping != NULL && index->count > 0) {
        if (index->dimension > SIZE_MAX / sizeof(float)) {
            return KC_SEMIDX_EFORMAT;
        }
        decoded = (float *)malloc(index->dimension * sizeof(float));
        if (decoded == NULL) {
            return KC_SEMIDX_ENOMEM;
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
                decoded[j] = kc_semidx_read_float(
                    index->records[i].encoded + j * sizeof(float)
                );
                if (!isfinite(decoded[j])) {
                    status = KC_SEMIDX_EFORMAT;
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
        status = KC_SEMIDX_OK;
    }

finished:
    free(decoded);
    return status;
}

/**
 * Build an index of independent text associations.
 * @param out Receives the built index.
 * @param documents Input corpus.
 * @param count Number of entries in the corpus.
 * @return KC_SEMIDX_OK or a negative status code.
 */
int kc_semidx_build(
    kc_semidx_t **out,
    const kc_semidx_document_t *documents,
    size_t count
)
{
    kc_semidx_t *index;
    int status = KC_SEMIDX_EEMBED;

    if (out == NULL) {
        return KC_SEMIDX_EINVAL;
    }
    *out = NULL;
    if (count > 0 && documents == NULL) {
        return KC_SEMIDX_EINVAL;
    }
    for (size_t i = 0; i < count; i++) {
        if (documents[i].id == NULL || documents[i].content == NULL) {
            return KC_SEMIDX_EINVAL;
        }
    }

    index = kc_semidx_allocate(count);
    if (index == NULL) {
        return KC_SEMIDX_ENOMEM;
    }
    index->dimension = kc_emb_dimension();
    if (index->dimension == 0 ||
        index->dimension > SIZE_MAX / sizeof(float)) {
        goto failure;
    }

    for (size_t i = 0; i < count; i++) {
        size_t length = 0;

        index->records[i].id = kc_semidx_copy_string(documents[i].id);
        index->records[i].content = kc_semidx_copy_string(
            documents[i].content
        );
        index->records[i].owned_strings = 1;
        if (index->records[i].id == NULL ||
            index->records[i].content == NULL) {
            status = KC_SEMIDX_ENOMEM;
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
    status = kc_semidx_prepare_graph(index);
    if (status != KC_SEMIDX_OK) {
        goto failure;
    }
    *out = index;
    return KC_SEMIDX_OK;

failure:
    kc_semidx_close(index);
    return status;
}

/**
 * Load and validate the binary records from an opened mapping.
 * @param index Destination index with a live file mapping.
 * @return KC_SEMIDX_OK or a negative status code.
 */
static int kc_semidx_load_records(kc_semidx_t *index)
{
    const void *bytes;
    size_t size;
    size_t offset;
    const unsigned char *data;
    uint64_t count;

    if (kc_mmap_get(index->mapping, &bytes, &size) != KC_MMAP_OK ||
        size < KC_SEMIDX_HEADER_SIZE) {
        return KC_SEMIDX_EFORMAT;
    }
    data = (const unsigned char *)bytes;
    if (memcmp(data, kc_semidx_magic, 8) != 0 ||
        kc_semidx_read_u32(data + 8) != KC_SEMIDX_FORMAT_VERSION ||
        memcmp(data + 24, kc_semidx_model, KC_SEMIDX_MODEL_SIZE) != 0) {
        return KC_SEMIDX_EFORMAT;
    }
    index->dimension = kc_semidx_read_u32(data + 12);
    count = kc_semidx_read_u64(data + 16);
    if (index->dimension == 0 || index->dimension != kc_emb_dimension() ||
        index->dimension > SIZE_MAX / sizeof(float) ||
        count > SIZE_MAX / sizeof(kc_semidx_record_t)) {
        return KC_SEMIDX_EFORMAT;
    }
    index->count = (size_t)count;
    if (index->count > 0) {
        index->records = (kc_semidx_record_t *)calloc(
            index->count, sizeof(*index->records)
        );
        if (index->records == NULL) {
            return KC_SEMIDX_ENOMEM;
        }
    }
    offset = KC_SEMIDX_HEADER_SIZE;
    for (size_t i = 0; i < index->count; i++) {
        size_t id_length;
        size_t content_length;

        if (!kc_semidx_fits(size, offset, 8)) {
            return KC_SEMIDX_EFORMAT;
        }
        id_length = kc_semidx_read_u32(data + offset);
        content_length = kc_semidx_read_u32(data + offset + 4);
        offset += 8;
        if (!kc_semidx_fits(size, offset, id_length + 1) ||
            data[offset + id_length] != 0 ||
            memchr(data + offset, 0, id_length) != NULL) {
            return KC_SEMIDX_EFORMAT;
        }
        index->records[i].id = (const char *)(data + offset);
        offset += id_length + 1;
        if (!kc_semidx_fits(size, offset, content_length + 1) ||
            data[offset + content_length] != 0 ||
            memchr(data + offset, 0, content_length) != NULL) {
            return KC_SEMIDX_EFORMAT;
        }
        index->records[i].content = (const char *)(data + offset);
        offset += content_length + 1;
        if (!kc_semidx_fits(size, offset, index->dimension * sizeof(float))) {
            return KC_SEMIDX_EFORMAT;
        }
        index->records[i].encoded = data + offset;
        offset += index->dimension * sizeof(float);
    }
    return offset == size ? KC_SEMIDX_OK : KC_SEMIDX_EFORMAT;
}

/**
 * Open a binary semantic index and reconstruct its in-memory graph.
 * @param out Receives the opened index.
 * @param path Index file path.
 * @return KC_SEMIDX_OK or a negative status code.
 */
int kc_semidx_open(kc_semidx_t **out, const char *path)
{
    kc_semidx_t *index;
    int status;

    if (out == NULL) {
        return KC_SEMIDX_EINVAL;
    }
    *out = NULL;
    if (path == NULL || path[0] == '\0') {
        return KC_SEMIDX_EINVAL;
    }
    index = kc_semidx_allocate(0);
    if (index == NULL) {
        return KC_SEMIDX_ENOMEM;
    }
    if (kc_mmap_open(&index->mapping, path) != KC_MMAP_OK) {
        kc_semidx_close(index);
        return KC_SEMIDX_EIO;
    }
    status = kc_semidx_load_records(index);
    if (status == KC_SEMIDX_OK) {
        status = kc_semidx_prepare_graph(index);
    }
    if (status != KC_SEMIDX_OK) {
        kc_semidx_close(index);
        return status;
    }
    *out = index;
    return KC_SEMIDX_OK;
}

/**
 * Compute a checked serialized size for a built index.
 * @param index Index to measure.
 * @param out_size Receives the serialized byte size.
 * @return KC_SEMIDX_OK or a negative status code.
 */
static int kc_semidx_serialized_size(
    const kc_semidx_t *index,
    size_t *out_size
)
{
    size_t total = KC_SEMIDX_HEADER_SIZE;

    if (index->dimension > SIZE_MAX / sizeof(float)) {
        return KC_SEMIDX_EFORMAT;
    }
    for (size_t i = 0; i < index->count; i++) {
        size_t id_length = strlen(index->records[i].id);
        size_t content_length = strlen(index->records[i].content);
        size_t vector_size = index->dimension * sizeof(float);

        if (id_length > UINT32_MAX || content_length > UINT32_MAX ||
            id_length > SIZE_MAX - 1 ||
            content_length > SIZE_MAX - 1) {
            return KC_SEMIDX_EFORMAT;
        }
        if (id_length > SIZE_MAX - 9 ||
            !kc_semidx_fits(SIZE_MAX, total, id_length + 9)) {
            return KC_SEMIDX_EFORMAT;
        }
        total += id_length + 9;
        if (!kc_semidx_fits(SIZE_MAX, total, content_length + 1)) {
            return KC_SEMIDX_EFORMAT;
        }
        total += content_length + 1;
        if (!kc_semidx_fits(SIZE_MAX, total, vector_size)) {
            return KC_SEMIDX_EFORMAT;
        }
        total += vector_size;
    }
    *out_size = total;
    return KC_SEMIDX_OK;
}

/**
 * Save a built index with little-endian lengths and vectors.
 * @param index Index created by kc_semidx_build().
 * @param path Binary destination file.
 * @return KC_SEMIDX_OK or a negative status code.
 */
int kc_semidx_save(const kc_semidx_t *index, const char *path)
{
    kc_mmap_t *mapping = NULL;
    unsigned char *data;
    size_t size;
    size_t offset = KC_SEMIDX_HEADER_SIZE;
    int status = KC_SEMIDX_EIO;

    if (index == NULL || path == NULL || path[0] == '\0') {
        return KC_SEMIDX_EINVAL;
    }
    if (index->mapping != NULL) {
        return KC_SEMIDX_ESTATE;
    }
    status = kc_semidx_serialized_size(index, &size);
    if (status != KC_SEMIDX_OK) {
        return status;
    }
    data = (unsigned char *)malloc(size);
    if (data == NULL) {
        return KC_SEMIDX_ENOMEM;
    }
    memcpy(data, kc_semidx_magic, 8);
    kc_semidx_write_u32(data + 8, KC_SEMIDX_FORMAT_VERSION);
    kc_semidx_write_u32(data + 12, (uint32_t)index->dimension);
    kc_semidx_write_u64(data + 16, (uint64_t)index->count);
    memcpy(data + 24, kc_semidx_model, KC_SEMIDX_MODEL_SIZE);

    for (size_t i = 0; i < index->count; i++) {
        size_t id_length = strlen(index->records[i].id);
        size_t content_length = strlen(index->records[i].content);

        kc_semidx_write_u32(data + offset, (uint32_t)id_length);
        kc_semidx_write_u32(data + offset + 4, (uint32_t)content_length);
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
            kc_semidx_write_float(
                data + offset + j * sizeof(float),
                index->records[i].owned_vector[j]
            );
        }
        offset += index->dimension * sizeof(float);
    }

    status = KC_SEMIDX_EIO;
    if (kc_mmap_open(&mapping, path) == KC_MMAP_OK) {
        if (kc_mmap_set(mapping, data, size) == KC_MMAP_OK &&
            kc_mmap_save(mapping) == KC_MMAP_OK) {
            status = KC_SEMIDX_OK;
        }
    }
    kc_mmap_close(mapping);
    free(data);
    return status;
}

/**
 * Search a semantic index and restore its user-supplied associations.
 * @param index Index to query.
 * @param query Search text.
 * @param options Optional search settings.
 * @param out_results Receives the allocated result array.
 * @param out_count Receives the number of results.
 * @return KC_SEMIDX_OK or a negative status code.
 */
int kc_semidx_search(
    const kc_semidx_t *index,
    const char *query,
    const kc_semidx_search_options_t *options,
    kc_semidx_result_t **out_results,
    size_t *out_count
)
{
    float *embedding = NULL;
    size_t dimension = 0;
    size_t limit = 10;
    double threshold = -1.0;
    kc_hnsw_result_t *matches = NULL;
    size_t count = 0;
    kc_semidx_result_t *results;
    int status = KC_SEMIDX_EINDEX;

    if (out_results == NULL || out_count == NULL) {
        return KC_SEMIDX_EINVAL;
    }
    *out_results = NULL;
    *out_count = 0;
    if (index == NULL || query == NULL || index->graph == NULL) {
        return KC_SEMIDX_EINVAL;
    }
    if (options != NULL) {
        if (options->limit != NULL) {
            limit = *options->limit;
        }
        if (options->threshold != NULL) {
            threshold = *options->threshold;
        }
    }
    if (!isfinite(threshold) || threshold < -1.0 || threshold > 1.0) {
        return KC_SEMIDX_EINVAL;
    }
    if (limit > INT_MAX) {
        return KC_SEMIDX_EINVAL;
    }
    if (limit == 0 || index->count == 0) {
        return KC_SEMIDX_OK;
    }
    if (kc_emb_embed(query, &embedding, &dimension) != KC_EMB_OK ||
        dimension != index->dimension) {
        kc_emb_free(embedding);
        return KC_SEMIDX_EEMBED;
    }
    if (kc_hnsw_search(
        index->graph, embedding, limit, threshold, &matches, &count
    ) != KC_HNSW_OK) {
        kc_emb_free(embedding);
        return KC_SEMIDX_EINDEX;
    }
    kc_emb_free(embedding);
    if (count == 0) {
        return KC_SEMIDX_OK;
    }
    if (count > SIZE_MAX / sizeof(*results)) {
        kc_hnsw_free(matches);
        return KC_SEMIDX_ENOMEM;
    }
    results = (kc_semidx_result_t *)calloc(count, sizeof(*results));
    if (results == NULL) {
        kc_hnsw_free(matches);
        return KC_SEMIDX_ENOMEM;
    }
    for (size_t i = 0; i < count; i++) {
        char *end;
        uintmax_t ordinal = strtoumax(matches[i].id, &end, 10);

        if (matches[i].id[0] == '\0' || *end != '\0' ||
            ordinal >= index->count) {
            goto failure;
        }
        results[i].id = index->records[(size_t)ordinal].id;
        results[i].content = index->records[(size_t)ordinal].content;
        results[i].score = matches[i].score;
    }
    *out_results = results;
    *out_count = count;
    kc_hnsw_free(matches);
    return KC_SEMIDX_OK;

failure:
    free(results);
    kc_hnsw_free(matches);
    return status;
}

/**
 * Return the number of records in a semantic index.
 * @param index Index or NULL.
 * @return Number of records, or zero for NULL.
 */
size_t kc_semidx_count(const kc_semidx_t *index)
{
    return index == NULL ? 0 : index->count;
}

/**
 * Release search result storage without touching borrowed record strings.
 * @param ptr Result allocation or NULL.
 * @return void
 */
void kc_semidx_free(void *ptr)
{
    free(ptr);
}

/**
 * Release all resources held by a semantic index.
 * @param index Index handle or NULL.
 * @return void
 */
void kc_semidx_close(kc_semidx_t *index)
{
    if (index == NULL) {
        return;
    }
    kc_hnsw_close(index->graph);
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
const char *kc_semidx_strerror(int status)
{
    switch (status) {
        case KC_SEMIDX_OK:
            return "success";
        case KC_SEMIDX_EINVAL:
            return "invalid argument";
        case KC_SEMIDX_ENOMEM:
            return "out of memory";
        case KC_SEMIDX_EIO:
            return "file I/O error";
        case KC_SEMIDX_EFORMAT:
            return "incompatible or invalid index";
        case KC_SEMIDX_EEMBED:
            return "embedding failed";
        case KC_SEMIDX_EINDEX:
            return "vector index failed";
        case KC_SEMIDX_ESTATE:
            return "index cannot be saved in its current state";
        default:
            return "unknown error";
    }
}

/**
 * Return the build timestamp for this library.
 * @return Build timestamp.
 */
uint64_t kc_semidx_version(void)
{
    return (uint64_t)KC_SEMIDX_BUILD_VERSION;
}
