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
    char *source;
    char *replacement;
    size_t insertion;
} kc_embdex_word_t;

typedef struct {
    size_t count;
    size_t ambiguous;
    int exhaustive;
} kc_embdex_variant_plan_t;

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
    kc_embdex_word_t *wordmap;
    size_t word_count;
    char model_tag[KC_EMBDEX_MODEL_SIZE];
};

static void kc_embdex_wordmap_trace(const char *original,
    const char *mapped, size_t variant);

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
 * Identify the optional experimental word map in the existing model tag.
 * The bytewise FNV checksum deliberately rejects reuse after map changes.
 * @param tag Receives the complete 32-byte index model identity.
 * @return KC_EMBDEX_OK or a file error.
 */
static int kc_embdex_model_identity(char tag[KC_EMBDEX_MODEL_SIZE])
{
    const char *path = getenv("KC_EMBDEX_WORDMAP");
    FILE *file;
    uint32_t digest = UINT32_C(2166136261);
    int ch;

    memset(tag, 0, KC_EMBDEX_MODEL_SIZE);
    memcpy(tag, kc_embdex_model, sizeof(kc_embdex_model));
    if (path == NULL || path[0] == '\0') return KC_EMBDEX_OK;
    file = fopen(path, "rb");
    if (file == NULL) return KC_EMBDEX_EIO;
    while ((ch = fgetc(file)) != EOF) {
        digest ^= (unsigned char)ch;
        digest *= UINT32_C(16777619);
    }
    {
        int failed = ferror(file);

        if (fclose(file) != 0 || failed) return KC_EMBDEX_EIO;
    }
    memset(tag, 0, KC_EMBDEX_MODEL_SIZE);
    {
        int written = snprintf(tag, KC_EMBDEX_MODEL_SIZE,
            "%s+%08" PRIx32, kc_embdex_model, digest);

        if (written < 0 || (size_t)written >= KC_EMBDEX_MODEL_SIZE)
            return KC_EMBDEX_EFORMAT;
    }
    return KC_EMBDEX_OK;
}

typedef struct {
    char *value;
    size_t count;
    int failed;
} kc_embdex_word_capture_t;

/**
 * Capture exactly one term through the BM25 Unicode normalizer.
 * @param token Borrowed normalized token.
 * @param userdata Word accumulator.
 * @return Zero unless a duplicate token or allocation error occurs.
 */
static int kc_embdex_capture_word(const char *token, void *userdata)
{
    kc_embdex_word_capture_t *capture =
        (kc_embdex_word_capture_t *)userdata;

    capture->count++;
    if (capture->count != 1) return 1;
    capture->value = kc_embdex_copy_string(token);
    if (capture->value == NULL) {
        capture->failed = 1;
        return 1;
    }
    return 0;
}

/**
 * Normalize exactly one word using the existing BM25 analyzer.
 * @param index Index with a built or imported lexical analyzer.
 * @param bytes Source substring.
 * @param size Source length.
 * @param result Receives an owned normalized word.
 * @return KC_EMBDEX_OK, EFORMAT, ELEXICAL or ENOMEM.
 */
static int kc_embdex_word_normalize(const kc_embdex_t *index,
    const char *bytes, size_t size, char **result)
{
    kc_embdex_word_capture_t capture = {0};
    char *copy;
    int status;

    *result = NULL;
    if (size == SIZE_MAX) return KC_EMBDEX_ENOMEM;
    copy = (char *)malloc(size + 1);
    if (copy == NULL) return KC_EMBDEX_ENOMEM;
    memcpy(copy, bytes, size);
    copy[size] = '\0';
    status = kc_bm25_tokens(index->lexical, copy,
        kc_embdex_capture_word, &capture);
    free(copy);
    if (capture.failed) {
        free(capture.value);
        return KC_EMBDEX_ENOMEM;
    }
    if (status != KC_BM25_OK || capture.count != 1) {
        free(capture.value);
        return status == KC_BM25_EUTF8 ?
            KC_EMBDEX_ELEXICAL : KC_EMBDEX_EFORMAT;
    }
    *result = capture.value;
    return KC_EMBDEX_OK;
}

/**
 * Sort normalized dictionary keys for binary lookups.
 * @param a First word replacement.
 * @param b Second word replacement.
 * @return Standard comparison result.
 */
static int kc_embdex_word_compare(const void *a, const void *b)
{
    const kc_embdex_word_t *left = (const kc_embdex_word_t *)a;
    const kc_embdex_word_t *right = (const kc_embdex_word_t *)b;

    {
        int order = strcmp(left->source, right->source);

        if (order != 0) return order;
        return left->insertion < right->insertion ? -1 :
            left->insertion > right->insertion ? 1 : 0;
    }
}

/**
 * Find the first candidate translation by source word.
 * Multiple dictionary lines may share a source term, in file order.
 * @param index Loaded experimental word map.
 * @param word BM25-normalized source term.
 * @param count Receives number of translations available.
 * @return Start position, or zero with count zero when absent.
 */
static size_t kc_embdex_word_find(const kc_embdex_t *index,
    const char *word, size_t *count)
{
    size_t low = 0, high = index->word_count, first;

    *count = 0;
    while (low < high) {
        size_t mid = low + (high - low) / 2;

        if (strcmp(index->wordmap[mid].source, word) < 0)
            low = mid + 1;
        else high = mid;
    }
    first = low;
    while (low < index->word_count &&
        strcmp(index->wordmap[low].source, word) == 0)
    {
        low++;
    }
    *count = low - first;
    return first;
}

/**
 * Select a translation for one word in a bounded query variant.
 * Enumerate all combinations when possible; otherwise vary words across
 * the entire query with staggered masks. The first variant is the baseline.
 * @param plan Per-query budget and selection strategy.
 * @param variant Candidate combination ordinal.
 * @param occurrence Zero-based ambiguous word occurrence.
 * @param stride Mixed-radix weight for exhaustive enumeration.
 * @param choices Number of translations for the current word.
 * @return Translation ordinal.
 */
static size_t kc_embdex_variant_choice(const kc_embdex_variant_plan_t *plan,
    size_t variant, size_t occurrence, size_t stride, size_t choices)
{
    size_t bits = 0, mask, secondary;

    if (variant == 0 || choices <= 1) return 0;
    if (plan == NULL || plan->exhaustive)
        return (variant / stride) % choices;
    if (plan->ambiguous == 1) return variant % choices;
    /* Four staggered bits cover the full sentence within 16 variants. */
    while (((size_t)1 << bits) < plan->count) bits++;
    mask = (size_t)1 << (occurrence % bits);
    if (occurrence >= bits && bits > 1) {
        secondary = (occurrence + occurrence / bits) % bits;
        mask |= (size_t)1 << secondary;
    }
    if ((variant & mask) == 0) return 0;
    return 1 + ((variant + occurrence) % (choices - 1));
}

/**
 * Load an experimental UTF-8 TSV map of normalized words to English words.
 * Mapping is deliberately optional and leaves lexical evidence unchanged.
 * @param index Index with an initialized BM25 analyzer.
 * @return KC_EMBDEX_OK or an error on invalid mappings and I/O.
 */
static int kc_embdex_wordmap_load(kc_embdex_t *index)
{
    const char *path = getenv("KC_EMBDEX_WORDMAP");
    kc_embdex_word_t *next;
    char line[2048];
    size_t capacity = 0;
    FILE *file;
    int status = KC_EMBDEX_OK;

    if (path == NULL || path[0] == '\0') return KC_EMBDEX_OK;
    file = fopen(path, "rb");
    if (file == NULL) return KC_EMBDEX_EIO;

    while (fgets(line, sizeof(line), file) != NULL) {
        size_t length = strlen(line);
        char *tab;
        char *normalized = NULL;
        char *replacement;

        if (length == sizeof(line) - 1 && line[length - 1] != '\n') {
            status = KC_EMBDEX_EFORMAT;
            break;
        }
        while (length != 0 &&
            (line[length - 1] == '\r' || line[length - 1] == '\n'))
            line[--length] = '\0';
        if (length == 0 || line[0] == '#') continue;
        tab = strchr(line, '\t');
        if (tab == NULL || tab == line || strchr(tab + 1, '\t') != NULL ||
            tab[1] == '\0')
        {
            status = KC_EMBDEX_EFORMAT;
            break;
        }
        *tab++ = '\0';
        for (const unsigned char *p = (const unsigned char *)tab;
            *p != '\0'; p++)
        {
            if (!((*p >= 'A' && *p <= 'Z') ||
                (*p >= 'a' && *p <= 'z')))
            {
                status = KC_EMBDEX_EFORMAT;
                break;
            }
        }
        if (status != KC_EMBDEX_OK) break;
        status = kc_embdex_word_normalize(index,
            line, strlen(line), &normalized);
        if (status != KC_EMBDEX_OK) break;
        if (index->word_count >= 4096) {
            free(normalized);
            status = KC_EMBDEX_EFORMAT;
            break;
        }
        if (index->word_count == capacity) {
            size_t updated = capacity == 0 ? 64 : capacity * 2;

            next = (kc_embdex_word_t *)realloc(index->wordmap,
                updated * sizeof(*next));
            if (next == NULL) {
                free(normalized);
                status = KC_EMBDEX_ENOMEM;
                break;
            }
            index->wordmap = next;
            capacity = updated;
        }
        replacement = kc_embdex_copy_string(tab);
        if (replacement == NULL) {
            free(normalized);
            status = KC_EMBDEX_ENOMEM;
            break;
        }
        index->wordmap[index->word_count].source = normalized;
        index->wordmap[index->word_count].replacement = replacement;
        index->wordmap[index->word_count].insertion = index->word_count;
        index->word_count++;
    }
    if (ferror(file)) status = KC_EMBDEX_EIO;
    if (fclose(file) != 0) status = KC_EMBDEX_EIO;
    if (status != KC_EMBDEX_OK) return status;

    qsort(index->wordmap, index->word_count, sizeof(*index->wordmap),
        kc_embdex_word_compare);
    return KC_EMBDEX_OK;
}

/**
 * Identify word bytes without treating punctuation as part of a word.
 * Unicode bytes remain grouped and are normalized by BM25 for lookup.
 * @param value One byte in UTF-8 text.
 * @return Nonzero if this byte belongs to a word.
 */
static int kc_embdex_is_word_byte(unsigned char value)
{
    return value >= 128 ||
        (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9');
}

/**
 * Recognize common UTF-8 punctuation which must not be absorbed into a
 * translated word, including inverted Spanish question/exclamation marks.
 * @param text Remaining UTF-8 bytes.
 * @param length Remaining byte count.
 * @return Punctuation sequence width, otherwise zero.
 */
static size_t kc_embdex_punctuation_width(const char *text, size_t length)
{
    const unsigned char *p = (const unsigned char *)text;

    if (length >= 2 && p[0] == 0xc2 &&
        (p[1] == 0xa1 || p[1] == 0xbf))
        return 2;
    if (length >= 3 && p[0] == 0xe2 && p[1] == 0x80 &&
        p[2] >= 0x90 && p[2] <= 0xa6)
        return 3;
    return 0;
}

/**
 * Append a byte sequence into an expanding, NUL-terminated text buffer.
 * @param buffer Destination allocation.
 * @param used Current content length.
 * @param capacity Current allocated size.
 * @param data Source bytes.
 * @param length Number of bytes to copy.
 * @return KC_EMBDEX_OK or ENOMEM.
 */
static int kc_embdex_word_append(char **buffer, size_t *used,
    size_t *capacity, const char *data, size_t length)
{
    size_t required;
    char *next;

    if (length > SIZE_MAX - *used - 1) return KC_EMBDEX_ENOMEM;
    required = *used + length + 1;
    if (required > *capacity) {
        size_t grown = *capacity == 0 ? 64 : *capacity;

        while (grown < required) {
            if (grown > SIZE_MAX / 2) return KC_EMBDEX_ENOMEM;
            grown *= 2;
        }
        next = (char *)realloc(*buffer, grown);
        if (next == NULL) return KC_EMBDEX_ENOMEM;
        *buffer = next;
        *capacity = grown;
    }
    memcpy(*buffer + *used, data, length);
    *used += length;
    (*buffer)[*used] = '\0';
    return KC_EMBDEX_OK;
}

/**
 * Embed with word-for-word substitution when an experimental map is enabled.
 * Unmatched words and all punctuation are preserved byte-for-byte.
 * @param index Index with a loaded wordmap and lexical normalizer.
 * @param text Borrowed original input.
 * @param vector Receives the embedding allocated by libemb.
 * @param dimension Receives the vector length.
 * @return KC_EMBDEX_OK or an embedding, normalization or memory error.
 */
static int kc_embdex_transform_text(const kc_embdex_t *index,
    const char *text, size_t variant,
    const kc_embdex_variant_plan_t *plan, char **output)
{
    size_t used = 0, capacity = 0, size = strlen(text), offset = 0;
    size_t stride = 1, ambiguous = 0;
    char *mapped = NULL;
    int status = KC_EMBDEX_OK;

    *output = NULL;
    if (index->word_count == 0) {
        *output = kc_embdex_copy_string(text);
        return *output == NULL ? KC_EMBDEX_ENOMEM : KC_EMBDEX_OK;
    }
    while (offset < size) {
        size_t end = offset;
        const char *replacement = text + offset;
        size_t length;
        char *normalized = NULL;
        size_t punctuation = kc_embdex_punctuation_width(
            text + offset, size - offset);

        if (punctuation != 0 ||
            !kc_embdex_is_word_byte((unsigned char)text[offset])) {
            end += punctuation == 0 ? 1 : punctuation;
        } else {
            size_t first, alternatives;

            while (end < size &&
                kc_embdex_punctuation_width(text + end, size - end) == 0 &&
                kc_embdex_is_word_byte((unsigned char)text[end]))
                end++;
            status = kc_embdex_word_normalize(index,
                text + offset, end - offset, &normalized);
            if (status == KC_EMBDEX_EFORMAT) status = KC_EMBDEX_OK;
            else if (status != KC_EMBDEX_OK) break;
            if (normalized != NULL) {
                first = kc_embdex_word_find(index, normalized,
                    &alternatives);
                if (alternatives != 0) {
                    size_t choice = kc_embdex_variant_choice(plan,
                        variant, ambiguous, stride, alternatives);

                    replacement = index->wordmap[first + choice].replacement;
                    if (alternatives > 1) {
                        ambiguous++;
                        stride = stride > 16 / alternatives ?
                            16 : stride * alternatives;
                    }
                }
            }
        }
        length = normalized == NULL || replacement == text + offset ?
            end - offset : strlen(replacement);
        status = kc_embdex_word_append(&mapped, &used, &capacity,
            replacement, length);
        free(normalized);
        if (status != KC_EMBDEX_OK) break;
        offset = end;
    }
    if (status == KC_EMBDEX_OK && mapped == NULL) {
        mapped = kc_embdex_copy_string(text);
        if (mapped == NULL) status = KC_EMBDEX_ENOMEM;
    }
    if (status == KC_EMBDEX_OK) {
        *output = mapped;
        return KC_EMBDEX_OK;
    }
    free(mapped);
    return status;
}

/**
 * Embed original or literally substituted words using the selected variant.
 * The mapped string is temporary and never alters stored document content.
 * @param index Index with an optional experimental dictionary.
 * @param text Original input text.
 * @param variant Bounded word-combination ordinal.
 * @param vector Receives the owned model embedding.
 * @param dimension Receives its dimension.
 * @return KC_EMBDEX_OK or a negative error status.
 */
static int kc_embdex_embed_text(const kc_embdex_t *index,
    const char *text, size_t variant, float **vector, size_t *dimension)
{
    char *mapped = NULL;
    int status;

    if (index->word_count == 0)
        return kc_emb_embed(text, vector, dimension) == KC_EMB_OK ?
            KC_EMBDEX_OK : KC_EMBDEX_EEMBED;
    status = kc_embdex_transform_text(index, text, variant,
        NULL, &mapped);
    if (status != KC_EMBDEX_OK) return status;
    kc_embdex_wordmap_trace(text, mapped, variant);
    status = kc_emb_embed(mapped, vector, dimension) == KC_EMB_OK ?
        KC_EMBDEX_OK : KC_EMBDEX_EEMBED;
    free(mapped);
    return status;
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
    status = kc_embdex_model_identity(index->model_tag);
    if (status != KC_EMBDEX_OK) goto failure;
    status = KC_EMBDEX_EEMBED;
    index->dimension = kc_emb_dimension();
    if (index->dimension == 0 ||
        index->dimension > SIZE_MAX / sizeof(float)) {
        goto failure;
    }

    for (size_t i = 0; i < count; i++) {
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
    }
    status = kc_embdex_prepare_lexical(index);
    if (status != KC_EMBDEX_OK) goto failure;
    status = kc_embdex_wordmap_load(index);
    if (status != KC_EMBDEX_OK) goto failure;
    for (size_t i = 0; i < count; i++) {
        size_t length = 0;

        status = kc_embdex_embed_text(index, documents[i].content,
            0, &index->records[i].owned_vector, &length);
        if (status != KC_EMBDEX_OK) goto failure;
        if (length != index->dimension) {
            status = KC_EMBDEX_EEMBED;
            goto failure;
        }
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
        memcmp(data + 24, index->model_tag, KC_EMBDEX_MODEL_SIZE) != 0)
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
    status = kc_embdex_model_identity(index->model_tag);
    if (status != KC_EMBDEX_OK) {
        kc_embdex_close(index);
        return status;
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
    if (status == KC_EMBDEX_OK)
        status = kc_embdex_wordmap_load(index);
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
    memcpy(data + 24, index->model_tag, KC_EMBDEX_MODEL_SIZE);
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
    size_t semantic_variant;
    size_t variants_evaluated;
    double semantic_threshold;
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
 * Log the actual pseudo-English text supplied to libemb for each variant.
 * Enabled only when KC_EMBDEX_WORDMAP_TRACE points to a writable JSONL file.
 * This trace includes original content and must not be used for production.
 * @param original Source text.
 * @param mapped Literal word substitutions, preserving original word order.
 * @param variant Index of the chosen word combination.
 * @return None; trace I/O never affects embedding success.
 */
static void kc_embdex_wordmap_trace(const char *original,
    const char *mapped, size_t variant)
{
    const char *path = getenv("KC_EMBDEX_WORDMAP_TRACE");
    FILE *file;

    if (path == NULL || path[0] == '\0') return;
    file = fopen(path, "a");
    if (file == NULL) return;
    fputs("{\"original\":", file);
    kc_embdex_json_string(file, original);
    fputs(",\"mapped\":", file);
    kc_embdex_json_string(file, mapped);
    fprintf(file, ",\"variant\":%zu}\n", variant);
    fclose(file);
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
    double semantic_threshold, const double *variant_scores,
    size_t variant_count, size_t variant_stride, size_t requested_variants)
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
        fprintf(file, ",\"semantic_rank\":%zu,\"variant\":%zu,"
            "\"variants_evaluated\":%zu,\"variants_requested\":%zu,"
            "\"candidate_threshold\":%.9g,\"final_rank\":%zu,"
            "\"variant_scores\":[",
            item->semantic_rank, item->semantic_variant,
            item->variants_evaluated, requested_variants,
            item->semantic_threshold, item->final_rank);
        for (size_t k = 0; k < variant_count; k++) {
            double cosine = variant_scores == NULL ? NAN :
                variant_scores[i * variant_stride + k];

            if (k != 0) fputc(',', file);
            if (isfinite(cosine)) fprintf(file, "%.9g", cosine);
            else fputs("null", file);
        }
        fputs("],\"reason\":", file);
        kc_embdex_json_string(file,
            item->reason == NULL ? "not_discovered" : item->reason);
        fputs("}\n", file);
    }
    fclose(file);
}

/**
 * Bound the number of heuristic word-substitution variants per query.
 * The baseline experiment compares one translation; ambiguous queries
 * can be tested with up to sixteen deterministic word combinations.
 * @param index Index with a previously loaded word map.
 * @param tokens Already-normalized query words.
 * @param count Receives the number of model calls.
 * @return KC_EMBDEX_OK or EINVAL for an invalid budget.
 */
static int kc_embdex_variant_count(const kc_embdex_t *index,
    const kc_embdex_tokens_t *tokens, kc_embdex_variant_plan_t *plan)
{
    const char *configured = getenv("KC_EMBDEX_WORDMAP_VARIANTS");
    size_t combinations = 1;
    size_t budget = 8;

    plan->count = 1;
    plan->ambiguous = 0;
    plan->exhaustive = 1;
    if (index->word_count == 0) return KC_EMBDEX_OK;
    if (configured != NULL && configured[0] != '\0') {
        char *end = NULL;
        unsigned long value = strtoul(configured, &end, 10);

        if (end == configured || *end != '\0' || value < 1 ||
            value > 16) return KC_EMBDEX_EINVAL;
        budget = (size_t)value;
    }
    for (size_t i = 0; i < tokens->count; i++) {
        size_t alternatives = 0;

        kc_embdex_word_find(index, tokens->value[i], &alternatives);
        if (alternatives > 1) {
            plan->ambiguous++;
            combinations = combinations > budget / alternatives ?
                budget + 1 : combinations * alternatives;
        }
    }
    plan->count = combinations > budget ? budget : combinations;
    plan->exhaustive = combinations <= budget;
    return KC_EMBDEX_OK;
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

    if (item->semantic_measured && item->semantic_threshold > 0)
        semantic_floor = item->semantic_threshold;
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
    size_t semantic_count = 0, rank_count = 0, variants = 1;
    kc_embdex_variant_plan_t variant_plan = {0};
    size_t unique_variants = 0;
    char *variant_text[16] = {0};
    double *variant_scores = NULL;
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

    status = kc_embdex_variant_count(index, &tokens, &variant_plan);
    if (status != KC_EMBDEX_OK) goto finished;
    variants = variant_plan.count;
    if (getenv("KC_EMBDEX_TRACE") != NULL &&
        getenv("KC_EMBDEX_TRACE")[0] != '\0')
    {
        if (index->count > SIZE_MAX / variants ||
            index->count * variants > SIZE_MAX / sizeof(double)) {
            status = KC_EMBDEX_ENOMEM;
            goto finished;
        }
        variant_scores = (double *)malloc(index->count *
            variants * sizeof(*variant_scores));
        if (variant_scores == NULL) {
            status = KC_EMBDEX_ENOMEM;
            goto finished;
        }
        for (size_t i = 0; i < index->count * variants; i++)
            variant_scores[i] = NAN;
    }
    {
        int exact_full = 0;

        for (size_t i = 0; i < index->count; i++)
            exact_full |= evidence[i].full_phrase;
        if (tokens.truncated || !exact_full || limit != 1) {
            for (size_t variant = 0; variant < variants; variant++) {
                double candidate_floor;
                char *mapped = NULL;
                int repeated = 0;

                if (index->word_count != 0) {
                    status = kc_embdex_transform_text(index, query,
                        variant, &variant_plan, &mapped);
                    if (status != KC_EMBDEX_OK) goto finished;
                    for (size_t k = 0; k < unique_variants; k++) {
                        if (strcmp(mapped, variant_text[k]) == 0) {
                            repeated = 1;
                            break;
                        }
                    }
                    if (repeated) {
                        free(mapped);
                        continue;
                    }
                    variant_text[unique_variants] = mapped;
                    kc_embdex_wordmap_trace(query, mapped, variant);
                } else {
                    mapped = (char *)query;
                }
                status = kc_emb_embed(mapped, &embedding, &dimension) ==
                    KC_EMB_OK ? KC_EMBDEX_OK : KC_EMBDEX_EEMBED;
                if (status != KC_EMBDEX_OK) goto finished;
                if (dimension != index->dimension) {
                    status = KC_EMBDEX_EEMBED;
                    goto finished;
                }
                candidate_floor = kc_embdex_semantic_threshold(index,
                    embedding);
                if (index->graph == NULL) {
                    for (size_t i = 0; i < index->count; i++) {
                        double cosine = kc_embdex_cosine(index, i, embedding);
                        kc_embdex_evidence_t *item = &evidence[i];

                        if (variant_scores != NULL)
                            variant_scores[i * variants +
                                unique_variants] = cosine;
                        if (!item->semantic_measured ||
                            cosine > item->semantic)
                        {
                            item->semantic = cosine;
                            item->semantic_measured = 1;
                            item->semantic_found = cosine >= semantic_floor;
                            item->semantic_threshold = candidate_floor;
                            item->semantic_variant = variant;
                        }
                    }
                } else {
                    int result = kc_hnsw_search(index->graph,
                        embedding, window, semantic_floor,
                        &semantic, &semantic_count);

                    if (result != KC_HNSW_OK) {
                        status = KC_EMBDEX_EINDEX;
                        goto finished;
                    }
                    if (semantic_count > index->count) {
                        status = KC_EMBDEX_EINDEX;
                        goto finished;
                    }
                    for (size_t i = 0; i < semantic_count; i++) {
                        char *end = NULL;
                        uintmax_t ordinal = strtoumax(semantic[i].id,
                            &end, 10);
                        kc_embdex_evidence_t *item;

                        if (semantic[i].id[0] == '\0' || *end != '\0' ||
                            ordinal >= index->count)
                        {
                            status = KC_EMBDEX_EINDEX;
                            goto finished;
                        }
                        item = &evidence[(size_t)ordinal];
                        if (variant_scores != NULL)
                            variant_scores[(size_t)ordinal * variants +
                                unique_variants] = semantic[i].score;
                        if (!item->semantic_measured ||
                            semantic[i].score > item->semantic)
                        {
                            item->semantic = semantic[i].score;
                            item->semantic_measured = 1;
                            item->semantic_found = 1;
                            item->semantic_threshold = candidate_floor;
                            item->semantic_variant = variant;
                        }
                    }
                    kc_hnsw_free(semantic);
                    semantic = NULL;
                }
                unique_variants++;
                kc_emb_free(embedding);
                embedding = NULL;
                dimension = 0;
            }
            {
                size_t candidates = 0;

                for (size_t i = 0; i < index->count; i++) {
                    evidence[i].variants_evaluated = unique_variants;
                    if (!evidence[i].semantic_measured) continue;
                    ranks[candidates].ordinal = i;
                    ranks[candidates++].score = evidence[i].semantic;
                }
                qsort(ranks, candidates, sizeof(*ranks),
                    kc_embdex_rank_compare);
                for (size_t i = 0; i < candidates; i++)
                    evidence[ranks[i].ordinal].semantic_rank = i + 1;
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
            semantic_only_floor, variant_scores, unique_variants, variants,
            variants);
    kc_bm25_free(lexical);
    kc_hnsw_free(semantic);
    kc_emb_free(embedding);
    kc_embdex_tokens_close(&tokens);
    free(joined);
    for (size_t i = 0; i < 16; i++)
        free(variant_text[i]);
    free(variant_scores);
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
    for (size_t i = 0; i < index->word_count; i++) {
        free(index->wordmap[i].source);
        free(index->wordmap[i].replacement);
    }
    free(index->wordmap);
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
