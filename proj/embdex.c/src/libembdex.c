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
#include "parson.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_EMBDEX_BUILD_VERSION
#define KC_EMBDEX_BUILD_VERSION 0
#endif

#define KC_EMBDEX_HEADER_SIZE 104U
#define KC_EMBDEX_FORMAT_VERSION 1U
#define KC_EMBDEX_MODEL_SIZE 32U
#define KC_EMBDEX_QUERY_TOKENS 64U
#define KC_EMBDEX_FUZZY_CHOICES 8U
#define KC_EMBDEX_COVER_PATHS 2U
#define KC_EMBDEX_VARIANTS 8U

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
    kc_bm25_t *mapped_lexical;
    kc_mmap_t *mapping;
    size_t lexical_offset;
    size_t lexical_size;
    size_t mapped_lexical_offset;
    size_t mapped_lexical_size;
    size_t wordmap_offset;
    size_t wordmap_size;
    kc_embdex_word_t *wordmap;
    size_t word_count;
    size_t max_term_length;
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
 * Validate UTF-8 independently from the BM25 lexical analyzer.
 * Reject overlong encodings, surrogates, invalid continuations and scalars
 * above U+10FFFF; otherwise preserve every code point without tokenizing.
 * @return Nonzero when the bytes are valid UTF-8.
 */
static int kc_embdex_utf8_valid(const char *bytes, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        const unsigned char first = (unsigned char)bytes[offset];
        uint32_t codepoint;
        uint32_t minimum;
        size_t tail;

        if (first <= 0x7fU) {
            offset++;
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            codepoint = (uint32_t)(first & 0x1fU);
            minimum = 0x80U;
            tail = 1;
        } else if (first >= 0xe0U && first <= 0xefU) {
            codepoint = (uint32_t)(first & 0x0fU);
            minimum = 0x800U;
            tail = 2;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            codepoint = (uint32_t)(first & 0x07U);
            minimum = 0x10000U;
            tail = 3;
        } else return 0;
        if (tail >= length - offset) return 0;
        for (size_t i = 1; i <= tail; i++) {
            const unsigned char next = (unsigned char)bytes[offset + i];

            if ((next & 0xc0U) != 0x80U) return 0;
            codepoint = (codepoint << 6) | (uint32_t)(next & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU))
            return 0;
        offset += tail + 1;
    }
    return 1;
}

/**
 * Validate and normalize a dictionary source term, independently of BM25.
 * ASCII case folding retains byte offsets; all other UTF-8 is kept literally.
 * @return KC_EMBDEX_OK or an allocation or lexical error.
 */
static int kc_embdex_term_normalize(const char *bytes, size_t size,
    char **result)
{
    char *copy;

    *result = NULL;
    if (size == SIZE_MAX) return KC_EMBDEX_ENOMEM;
    if (!kc_embdex_utf8_valid(bytes, size)) return KC_EMBDEX_ELEXICAL;
    copy = (char *)malloc(size + 1);
    if (copy == NULL) return KC_EMBDEX_ENOMEM;
    memcpy(copy, bytes, size);
    copy[size] = '\0';
    for (size_t i = 0; i < size; i++)
        if (copy[i] >= 'A' && copy[i] <= 'Z')
            copy[i] = (char)(copy[i] - 'A' + 'a');
    *result = copy;
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
 * Compare a dictionary term with a prefix of the current input.
 * Returns zero when the key contains at least the requested prefix.
 * @return Negative, zero, or positive according to the prefix ordering.
 */
static int kc_embdex_term_compare_prefix(const char *term,
    const char *text, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        unsigned char left = (unsigned char)term[i];
        unsigned char right = (unsigned char)text[i];

        if (left != right) return left < right ? -1 : 1;
        if (left == 0) return -1;
    }
    return 0;
}

/**
 * Narrow a sorted dictionary range to keys sharing an exact prefix.
 * Prefixes that are absent stop the matcher immediately.
 * @return void
 */
static void kc_embdex_term_prefix_range(const kc_embdex_t *index,
    const char *text, size_t length, size_t *start, size_t *end)
{
    size_t low = *start, high = *end, midpoint;

    while (low < high) {
        midpoint = low + (high - low) / 2;
        if (kc_embdex_term_compare_prefix(
            index->wordmap[midpoint].source, text, length) < 0)
            low = midpoint + 1;
        else high = midpoint;
    }
    *start = low;
    high = *end;
    while (low < high) {
        midpoint = low + (high - low) / 2;
        if (kc_embdex_term_compare_prefix(
            index->wordmap[midpoint].source, text, length) <= 0)
            low = midpoint + 1;
        else high = midpoint;
    }
    *end = low;
}

/**
 * Select a translation for one term in a bounded query variant.
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
 * Append normalized substitution pairs from one JSON dictionary array.
 * File order determines the first translation for repeated source terms.
 * @param index Writable dictionary destination.
 * @param path Input JSON file.
 * @param capacity Current allocated mapping capacity.
 * @return KC_EMBDEX_OK or an input/memory error.
 */
static int kc_embdex_wordmap_read_file(kc_embdex_t *index,
    const char *path, size_t *capacity)
{
    JSON_Value *root = json_parse_file(path);
    JSON_Array *array;
    int status = KC_EMBDEX_OK;

    if (root == NULL) {
        FILE *file = fopen(path, "rb");

        if (file == NULL) return KC_EMBDEX_EIO;
        fclose(file);
        return KC_EMBDEX_EFORMAT;
    }
    array = json_value_get_array(root);
    if (array == NULL) {
        json_value_free(root);
        return KC_EMBDEX_EFORMAT;
    }
    for (size_t i = 0; i < json_array_get_count(array); i++) {
        JSON_Object *entry = json_array_get_object(array, i);
        const char *source;
        const char *target;
        char *normalized = NULL;
        char *replacement_text;
        kc_embdex_word_t *next;

        if (entry == NULL ||
            json_object_get_value(entry, "source") == NULL ||
            json_value_get_type(json_object_get_value(entry, "source")) != JSONString ||
            json_object_get_value(entry, "target") == NULL ||
            json_value_get_type(json_object_get_value(entry, "target")) != JSONString) {
            status = KC_EMBDEX_EFORMAT;
            break;
        }
        source = json_object_get_string(entry, "source");
        target = json_object_get_string(entry, "target");
        if (source[0] == '\0' || target[0] == '\0' ||
            index->word_count >= UINT32_MAX) {
            status = KC_EMBDEX_EFORMAT;
            break;
        }
        {
            char *validated = NULL;

            status = kc_embdex_term_normalize(target,
                strlen(target), &validated);
            free(validated);
            if (status != KC_EMBDEX_OK) break;
        }
        status = kc_embdex_term_normalize(source,
            strlen(source), &normalized);
        if (status != KC_EMBDEX_OK) break;
        if (index->word_count == *capacity) {
            size_t updated = *capacity == 0 ? 64 : *capacity * 2;

            if (updated < *capacity ||
                updated > SIZE_MAX / sizeof(*next)) {
                free(normalized);
                status = KC_EMBDEX_ENOMEM;
                break;
            }
            next = (kc_embdex_word_t *)realloc(index->wordmap,
                updated * sizeof(*next));
            if (next == NULL) {
                free(normalized);
                status = KC_EMBDEX_ENOMEM;
                break;
            }
            index->wordmap = next;
            *capacity = updated;
        }
        replacement_text = kc_embdex_copy_string(target);
        if (replacement_text == NULL) {
            free(normalized);
            status = KC_EMBDEX_ENOMEM;
            break;
        }
        index->wordmap[index->word_count].source = normalized;
        index->wordmap[index->word_count].replacement = replacement_text;
        index->wordmap[index->word_count].insertion = index->word_count;
        index->word_count++;
    }
    json_value_free(root);
    return status;
}

/**
 * Merge independent word-replacement tables without assigning them languages
 * or any semantic metadata. Only exact duplicate pairs are discarded.
 * @param index Index with an initialized BM25 analyzer.
 * @param paths Explicit dictionary paths, in preference order.
 * @param count Number of explicit dictionaries.
 * @return KC_EMBDEX_OK or a format, memory or file error.
 */
static int kc_embdex_wordmap_load(kc_embdex_t *index,
    const char *const *paths, size_t count)
{
    size_t capacity = 0;
    int status;

    if (count == 0) return KC_EMBDEX_OK;
    if (paths == NULL) return KC_EMBDEX_EINVAL;
    for (size_t i = 0; i < count; i++) {
        if (paths[i] == NULL || paths[i][0] == '\0')
            return KC_EMBDEX_EINVAL;
        status = kc_embdex_wordmap_read_file(index,
            paths[i], &capacity);
        if (status != KC_EMBDEX_OK) return status;
    }
    qsort(index->wordmap, index->word_count, sizeof(*index->wordmap),
        kc_embdex_word_compare);
    {
        size_t retained = 0;

        for (size_t i = 0; i < index->word_count; i++) {
            int duplicate = 0;

            for (size_t j = retained; j > 0; j--) {
                if (strcmp(index->wordmap[j - 1].source,
                    index->wordmap[i].source) != 0)
                    break;
                if (strcmp(index->wordmap[j - 1].replacement,
                    index->wordmap[i].replacement) == 0) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate) {
                free(index->wordmap[i].source);
                free(index->wordmap[i].replacement);
            } else {
                if (retained != i)
                    index->wordmap[retained] = index->wordmap[i];
                retained++;
            }
        }
        index->word_count = retained;
    }
    index->max_term_length = 0;
    for (size_t i = 0; i < index->word_count; i++) {
        size_t length = strlen(index->wordmap[i].source);

        if (length > index->max_term_length)
            index->max_term_length = length;
    }
    return KC_EMBDEX_OK;
}

/**
 * Identify explicit dictionary content without depending on filenames.
 * All source/target pairs contribute in their deterministic merged order.
 * @param index Index whose wordmap has been loaded.
 * @return KC_EMBDEX_OK or a formatting error.
 */
static int kc_embdex_wordmap_identity(kc_embdex_t *index)
{
    uint32_t hash = UINT32_C(2166136261);
    int written;

    if (index->word_count != 0) {
        const unsigned char *tag =
            (const unsigned char *)"embdex-utf8-terms-v2";

        for (; *tag != 0; tag++) {
            hash ^= *tag;
            hash *= UINT32_C(16777619);
        }
    }
    for (size_t i = 0; i < index->word_count; i++) {
        const unsigned char *parts[2] = {
            (const unsigned char *)index->wordmap[i].source,
            (const unsigned char *)index->wordmap[i].replacement
        };

        for (size_t j = 0; j < 2; j++) {
            for (const unsigned char *p = parts[j]; *p != 0; p++) {
                hash ^= *p;
                hash *= UINT32_C(16777619);
            }
            hash ^= 0;
            hash *= UINT32_C(16777619);
        }
    }
    memset(index->model_tag, 0, sizeof(index->model_tag));
    written = snprintf(index->model_tag, sizeof(index->model_tag),
        "%s+%08" PRIx32, kc_embdex_model, hash);
    return written < 0 || (size_t)written >= sizeof(index->model_tag) ?
        KC_EMBDEX_EFORMAT : KC_EMBDEX_OK;
}

/**
 * Read the UTF-8 code point at a known character boundary.
 * All callers operate on previously validated UTF-8 input.
 * @return UTF-8 code point beginning at the requested byte position.
 */
static uint32_t kc_embdex_codepoint(const char *text, size_t pos)
{
    const unsigned char *p = (const unsigned char *)text + pos;

    if (p[0] < 0x80U) return p[0];
    if (p[0] < 0xe0U)
        return ((uint32_t)(p[0] & 0x1fU) << 6) | (p[1] & 0x3fU);
    if (p[0] < 0xf0U)
        return ((uint32_t)(p[0] & 0x0fU) << 12) |
            ((uint32_t)(p[1] & 0x3fU) << 6) | (p[2] & 0x3fU);
    return ((uint32_t)(p[0] & 0x07U) << 18) |
        ((uint32_t)(p[1] & 0x3fU) << 12) |
        ((uint32_t)(p[2] & 0x3fU) << 6) | (p[3] & 0x3fU);
}

/**
 * Find the byte offset of the preceding UTF-8 code point.
 * @param text UTF-8 text containing the code point.
 * @param pos Byte offset after the preceding code point.
 * @return Byte offset of the preceding code point.
 */
static size_t kc_embdex_previous_codepoint(const char *text, size_t pos)
{
    size_t previous = pos - 1;

    while (previous != 0 &&
        (((unsigned char)text[previous] & 0xc0U) == 0x80U))
        previous--;
    return previous;
}

/**
 * Prevent ordinary words from matching inside larger words. Non-word UTF-8
 * symbols, operators and emojis may appear adjacent to other characters.
 * @return Nonzero when the code point is part of a word.
 */
static int kc_embdex_term_is_word(uint32_t cp)
{
    if (cp < 128U)
        return (cp >= 'a' && cp <= 'z') ||
            (cp >= 'A' && cp <= 'Z') ||
            (cp >= '0' && cp <= '9') || cp == '_';
    if ((cp >= 0x2000U && cp <= 0x2bffU) ||
        (cp >= 0x1f000U && cp <= 0x1ffffU) ||
        (cp >= 0xfe00U && cp <= 0xfe0fU) ||
        (cp >= 0x00a0U && cp <= 0x00bfU &&
            cp != 0x00aaU && cp != 0x00baU))
        return 0;
    return 1;
}

/**
 * A dictionary source matches as a complete term, not as a substring
 * within a larger alphanumeric token.
 * @return Nonzero when the source has valid term boundaries.
 */
static int kc_embdex_term_boundary(const char *text,
    size_t size, size_t start, size_t length)
{
    size_t end = start + length;
    size_t last = kc_embdex_previous_codepoint(text, end);

    if (start != 0 &&
        kc_embdex_term_is_word(kc_embdex_codepoint(text, start)) &&
        kc_embdex_term_is_word(kc_embdex_codepoint(
            text, kc_embdex_previous_codepoint(text, start))))
        return 0;
    if (end < size &&
        kc_embdex_term_is_word(kc_embdex_codepoint(text, last)) &&
        kc_embdex_term_is_word(kc_embdex_codepoint(text, end)))
        return 0;
    return 1;
}

/**
 * Case fold ASCII while keeping all UTF-8 symbols and original byte positions.
 * @return Allocated folded text, or NULL on allocation failure.
 */
static char *kc_embdex_term_fold(const char *text)
{
    char *copy = kc_embdex_copy_string(text);

    if (copy == NULL) return NULL;
    for (size_t i = 0; copy[i] != '\0'; i++)
        if (copy[i] >= 'A' && copy[i] <= 'Z')
            copy[i] = (char)(copy[i] - 'A' + 'a');
    return copy;
}

/**
 * Find the longest complete source term beginning at the given byte offset.
 * Source terms remain sorted; exact binary searches avoid scanning the full
 * dictionary for every character of a query.
 * @return Length of the longest matching source term.
 */
static size_t kc_embdex_term_match(const kc_embdex_t *index,
    const char *folded, size_t size, size_t offset,
    size_t *first, size_t *alternatives)
{
    size_t limit = size - offset, best = 0;
    size_t start = 0, end = index->word_count;

    *first = 0;
    *alternatives = 0;
    if (limit > index->max_term_length)
        limit = index->max_term_length;
    for (size_t length = 1; length <= limit; length++) {
        size_t found = 0;

        kc_embdex_term_prefix_range(index,
            folded + offset, length, &start, &end);
        if (start == end) break;
        while (start + found < end &&
            strlen(index->wordmap[start + found].source) == length)
            found++;
        if (found != 0 &&
            kc_embdex_term_boundary(folded, size, offset, length)) {
            best = length;
            *first = start;
            *alternatives = found;
        }
    }
    return best;
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
 * Substitute complete UTF-8 terms, always favoring the longest source match.
 * Scan the original input once: substitutions are never recursively expanded.
 * Unmatched bytes and punctuation are preserved exactly.
 * @return KC_EMBDEX_OK or an allocation or matching error.
 */
static int kc_embdex_transform_text(const kc_embdex_t *index,
    const char *text, size_t variant,
    const kc_embdex_variant_plan_t *plan, char **output)
{
    size_t used = 0, capacity = 0, size = strlen(text), offset = 0;
    size_t stride = 1, ambiguous = 0;
    char *mapped = NULL, *folded = NULL;
    int status = KC_EMBDEX_OK;

    *output = NULL;
    if (index->word_count == 0) {
        *output = kc_embdex_copy_string(text);
        return *output == NULL ? KC_EMBDEX_ENOMEM : KC_EMBDEX_OK;
    }
    folded = kc_embdex_term_fold(text);
    if (folded == NULL) return KC_EMBDEX_ENOMEM;
    while (offset < size) {
        size_t first = 0, alternatives = 0;
        size_t length = kc_embdex_term_match(index,
            folded, size, offset, &first, &alternatives);
        const char *replacement = text + offset;
        size_t replacement_length = 1;

        if (length > 0) {
            size_t choice = kc_embdex_variant_choice(plan,
                variant, ambiguous, stride, alternatives);

            replacement = index->wordmap[first + choice].replacement;
            replacement_length = strlen(replacement);
            if (alternatives > 1) {
                ambiguous++;
                stride = stride > KC_EMBDEX_VARIANTS / alternatives ?
                    KC_EMBDEX_VARIANTS : stride * alternatives;
            }
        } else {
            length = 1;
        }
        status = kc_embdex_word_append(&mapped, &used, &capacity,
            replacement, replacement_length);
        if (status != KC_EMBDEX_OK) break;
        offset += length;
    }
    free(folded);
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
 * Embed original or literally substituted terms using the selected variant.
 * The mapped string is temporary and never alters stored document content.
 * @param index Index with an optional dictionary.
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
 * Construct a separate BM25 section for literal dictionary substitutions.
 * Original associations retain their own independent lexical index.
 * @param index Index with normalized wordmap and original analyzer.
 * @return KC_EMBDEX_OK or a negative status.
 */
static int kc_embdex_prepare_mapped_lexical(kc_embdex_t *index)
{
    int fold_accents = 1;
    kc_bm25_options_t options = { .fold_accents = &fold_accents };
    int status;

    if (index->word_count == 0) return KC_EMBDEX_OK;
    status = kc_bm25_open(&index->mapped_lexical, &options);
    if (status != KC_BM25_OK)
        return status == KC_BM25_ENOMEM ?
            KC_EMBDEX_ENOMEM : KC_EMBDEX_ELEXICAL;
    for (size_t i = 0; i < index->count; i++) {
        char ordinal[32];
        char *mapped = NULL;

        if (snprintf(ordinal, sizeof(ordinal), "%zu", i) < 0)
            return KC_EMBDEX_ELEXICAL;
        status = kc_embdex_transform_text(index,
            index->records[i].content, 0, NULL, &mapped);
        if (status != KC_EMBDEX_OK) return status;
        status = kc_bm25_add(index->mapped_lexical, ordinal, mapped);
        free(mapped);
        if (status != KC_BM25_OK)
            return status == KC_BM25_ENOMEM ?
                KC_EMBDEX_ENOMEM : KC_EMBDEX_ELEXICAL;
    }
    status = kc_bm25_build(index->mapped_lexical);
    return status == KC_BM25_OK ? KC_EMBDEX_OK :
        status == KC_BM25_ENOMEM ? KC_EMBDEX_ENOMEM : KC_EMBDEX_ELEXICAL;
}

/**
 * Build an index of independent text associations.
 * @param out Receives the built index.
 * @param documents Input corpus.
 * @param count Number of entries in the corpus.
 * @param dictionaries Dictionary paths, or NULL when absent.
 * @param dictionary_count Number of dictionary paths.
 * @param options Optional build progress notifications.
 * @return KC_EMBDEX_OK or a negative status code.
 */
static int kc_embdex_create(
    kc_embdex_t **out,
    const kc_embdex_document_t *documents,
    size_t count,
    const char *const *dictionaries,
    size_t dictionary_count,
    const kc_embdex_build_options_t *options
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
    if (options != NULL && options->progress != NULL)
        options->progress(KC_EMBDEX_BUILD_PREPARE, 0, 0, options->userdata);

    index = kc_embdex_allocate(count);
    if (index == NULL) {
        return KC_EMBDEX_ENOMEM;
    }
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
    status = kc_embdex_wordmap_load(index,
        dictionaries, dictionary_count);
    if (status != KC_EMBDEX_OK) goto failure;
    status = kc_embdex_wordmap_identity(index);
    if (status != KC_EMBDEX_OK) goto failure;
    status = kc_embdex_prepare_mapped_lexical(index);
    if (status != KC_EMBDEX_OK) goto failure;
    if (options != NULL && options->progress != NULL)
        options->progress(KC_EMBDEX_BUILD_EMBED, 0, count,
            options->userdata);
    for (size_t i = 0; i < count; i++) {
        size_t length = 0;

        status = kc_embdex_embed_text(index, documents[i].content,
            0, &index->records[i].owned_vector, &length);
        if (status != KC_EMBDEX_OK) goto failure;
        if (length != index->dimension) {
            status = KC_EMBDEX_EEMBED;
            goto failure;
        }
        if (options != NULL && options->progress != NULL)
            options->progress(KC_EMBDEX_BUILD_EMBED, i + 1, count,
                options->userdata);
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
    uint32_t version;

    if (kc_mmap_get(index->mapping, &bytes, &size) != KC_MMAP_OK ||
        size < KC_EMBDEX_HEADER_SIZE) {
        return KC_EMBDEX_EFORMAT;
    }
    data = (const unsigned char *)bytes;
    version = kc_embdex_read_u32(data + 8);
    if (memcmp(data, kc_embdex_magic, 8) != 0 ||
        version != KC_EMBDEX_FORMAT_VERSION ||
        size < KC_EMBDEX_HEADER_SIZE)
        return KC_EMBDEX_EFORMAT;
    {
        size_t model_length = strlen(kc_embdex_model);

        if (memcmp(data + 24, kc_embdex_model, model_length) != 0 ||
            data[24 + model_length] != '+')
            return KC_EMBDEX_EFORMAT;
    }
    {
        uint64_t lexical_offset = kc_embdex_read_u64(data + 56);
        uint64_t lexical_size = kc_embdex_read_u64(data + 64);
        uint64_t mapped_offset = kc_embdex_read_u64(data + 72);
        uint64_t mapped_size = kc_embdex_read_u64(data + 80);
        uint64_t words_offset = kc_embdex_read_u64(data + 88);
        uint64_t words_size = kc_embdex_read_u64(data + 96);

        if (lexical_offset < KC_EMBDEX_HEADER_SIZE ||
            lexical_offset > size || lexical_size == 0 ||
            lexical_size > size - lexical_offset ||
            mapped_offset != lexical_offset + lexical_size ||
            mapped_offset > size || mapped_size > size - mapped_offset ||
            words_offset != mapped_offset + mapped_size ||
            words_offset > size || words_size < 4 ||
            words_size > size - words_offset ||
            words_offset + words_size != size)
            return KC_EMBDEX_EFORMAT;
        index->lexical_offset = (size_t)lexical_offset;
        index->lexical_size = (size_t)lexical_size;
        index->mapped_lexical_offset = (size_t)mapped_offset;
        index->mapped_lexical_size = (size_t)mapped_size;
        index->wordmap_offset = (size_t)words_offset;
        index->wordmap_size = (size_t)words_size;
    }
    index->dimension = kc_embdex_read_u32(data + 12);
    count = kc_embdex_read_u64(data + 16);
    if (index->dimension == 0 ||
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
 * Restore the normalized dictionary from the mapped binary index.
 * The on-disk section is a 32-bit count followed by word lengths and
 * NUL-terminated source/replacement pairs. Copies are owned by the index.
 * @param index Index with a valid mapping and section bounds.
 * @return KC_EMBDEX_OK or EFORMAT / ENOMEM.
 */
static int kc_embdex_load_wordmap(kc_embdex_t *index)
{
    const void *bytes = NULL;
    const unsigned char *data;
    size_t size, offset, end;
    uint32_t count;

    if (kc_mmap_get(index->mapping, &bytes, &size) != KC_MMAP_OK ||
        !kc_embdex_fits(size, index->wordmap_offset,
            index->wordmap_size))
        return KC_EMBDEX_EFORMAT;
    data = (const unsigned char *)bytes;
    offset = index->wordmap_offset;
    end = offset + index->wordmap_size;
    if (!kc_embdex_fits(end, offset, 4))
        return KC_EMBDEX_EFORMAT;
    count = kc_embdex_read_u32(data + offset);
    offset += 4;
    if (count == 0) return offset == end ? KC_EMBDEX_OK :
        KC_EMBDEX_EFORMAT;
    if (count > (end - offset) / 12)
        return KC_EMBDEX_EFORMAT;
    index->wordmap = (kc_embdex_word_t *)calloc(count,
        sizeof(*index->wordmap));
    if (index->wordmap == NULL) return KC_EMBDEX_ENOMEM;
    for (uint32_t i = 0; i < count; i++) {
        size_t source_length, replacement_length;
        char *source, *replacement;

        if (!kc_embdex_fits(end, offset, 8))
            return KC_EMBDEX_EFORMAT;
        source_length = kc_embdex_read_u32(data + offset);
        replacement_length = kc_embdex_read_u32(data + offset + 4);
        offset += 8;
        if (source_length == 0 || replacement_length == 0 ||
            source_length == SIZE_MAX ||
            replacement_length == SIZE_MAX ||
            !kc_embdex_fits(end, offset, source_length + 1) ||
            data[offset + source_length] != '\0' ||
            memchr(data + offset, '\0', source_length) != NULL)
            return KC_EMBDEX_EFORMAT;
        source = kc_embdex_copy_string((const char *)data + offset);
        if (source == NULL) return KC_EMBDEX_ENOMEM;
        offset += source_length + 1;
        if (!kc_embdex_fits(end, offset, replacement_length + 1) ||
            data[offset + replacement_length] != '\0' ||
            memchr(data + offset, '\0', replacement_length) != NULL)
        {
            free(source);
            return KC_EMBDEX_EFORMAT;
        }
        replacement = kc_embdex_copy_string((const char *)data + offset);
        if (replacement == NULL) {
            free(source);
            return KC_EMBDEX_ENOMEM;
        }
        offset += replacement_length + 1;
        index->wordmap[i].source = source;
        index->wordmap[i].replacement = replacement;
        index->wordmap[i].insertion = i;
        index->word_count++;
        if (source_length > index->max_term_length)
            index->max_term_length = source_length;
        if (i != 0 && strcmp(index->wordmap[i-1].source, source) > 0)
            return KC_EMBDEX_EFORMAT;
    }
    return offset == end ? KC_EMBDEX_OK : KC_EMBDEX_EFORMAT;
}

/**
 * Open a binary semantic index without rebuilding the vector graph.
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
    if (status == KC_EMBDEX_OK) {
        const void *bytes = NULL;
        size_t size = 0;

        if (index->mapped_lexical_size > 0) {
            int result = KC_BM25_EFORMAT;

            if (kc_mmap_get(index->mapping, &bytes, &size) == KC_MMAP_OK)
                result = kc_bm25_import(&index->mapped_lexical,
                    (const unsigned char *)bytes +
                    index->mapped_lexical_offset,
                    index->mapped_lexical_size);
            if (result != KC_BM25_OK)
                status = result == KC_BM25_ENOMEM ?
                    KC_EMBDEX_ENOMEM : KC_EMBDEX_EFORMAT;
            else if (kc_bm25_count(index->mapped_lexical) != index->count)
                status = KC_EMBDEX_EFORMAT;
        }
        if (status == KC_EMBDEX_OK)
            status = kc_embdex_load_wordmap(index);
        if (status == KC_EMBDEX_OK &&
            (index->word_count > 0) != (index->mapped_lexical != NULL))
            status = KC_EMBDEX_EFORMAT;
        if (status == KC_EMBDEX_OK)
            status = kc_embdex_wordmap_identity(index);
        if (status == KC_EMBDEX_OK &&
            (kc_mmap_get(index->mapping, &bytes, &size) != KC_MMAP_OK ||
            memcmp((const unsigned char *)bytes + 24, index->model_tag,
                KC_EMBDEX_MODEL_SIZE) != 0))
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
    unsigned char *data = NULL;
    void *lexical_bytes = NULL, *mapped_bytes = NULL;
    size_t size = 0, lexical_size = 0, mapped_size = 0;
    size_t words_size = 0, record_size = 0, offset;
    int status;

    if (index == NULL || path == NULL || path[0] == '\0')
        return KC_EMBDEX_EINVAL;
    if (index->mapping != NULL || index->lexical == NULL ||
        (index->word_count > 0 && index->mapped_lexical == NULL))
        return KC_EMBDEX_ESTATE;
    status = kc_embdex_serialized_size(index, &record_size);
    if (status != KC_EMBDEX_OK) return status;
    {
        int result = kc_bm25_export(index->lexical,
            &lexical_bytes, &lexical_size);

        if (result != KC_BM25_OK) {
            status = result == KC_BM25_ENOMEM ?
                KC_EMBDEX_ENOMEM : KC_EMBDEX_ELEXICAL;
            goto finished;
        }
    }
    words_size = 4;
    if (index->word_count > 0) {
        int result = kc_bm25_export(index->mapped_lexical,
            &mapped_bytes, &mapped_size);

        if (result != KC_BM25_OK) {
            status = result == KC_BM25_ENOMEM ?
                KC_EMBDEX_ENOMEM : KC_EMBDEX_ELEXICAL;
            goto finished;
        }
        for (size_t i = 0; i < index->word_count; i++) {
            size_t source_length = strlen(index->wordmap[i].source);
            size_t replacement_length =
                strlen(index->wordmap[i].replacement);
            size_t combined;

            if (source_length == 0 || replacement_length == 0 ||
                source_length > UINT32_MAX ||
                replacement_length > UINT32_MAX ||
                source_length > SIZE_MAX - replacement_length)
            {
                status = KC_EMBDEX_EFORMAT;
                goto finished;
            }
            combined = source_length + replacement_length;
            if (combined > SIZE_MAX - 10 ||
                words_size > SIZE_MAX - combined - 10) {
                status = KC_EMBDEX_EFORMAT;
                goto finished;
            }
            words_size += combined + 10;
        }
    }
    if (record_size > SIZE_MAX - lexical_size ||
        record_size + lexical_size > SIZE_MAX - mapped_size ||
        record_size + lexical_size + mapped_size > SIZE_MAX - words_size)
    {
        status = KC_EMBDEX_ENOMEM;
        goto finished;
    }
    size = record_size + lexical_size + mapped_size + words_size;
    data = (unsigned char *)malloc(size);
    if (data == NULL) {
        status = KC_EMBDEX_ENOMEM;
        goto finished;
    }
    memset(data, 0, KC_EMBDEX_HEADER_SIZE);
    memcpy(data, kc_embdex_magic, 8);
    kc_embdex_write_u32(data + 8, KC_EMBDEX_FORMAT_VERSION);
    kc_embdex_write_u32(data + 12, (uint32_t)index->dimension);
    kc_embdex_write_u64(data + 16, (uint64_t)index->count);
    memcpy(data + 24, index->model_tag, KC_EMBDEX_MODEL_SIZE);
    kc_embdex_write_u64(data + 56, (uint64_t)record_size);
    kc_embdex_write_u64(data + 64, (uint64_t)lexical_size);
    kc_embdex_write_u64(data + 72,
        (uint64_t)(record_size + lexical_size));
    kc_embdex_write_u64(data + 80, (uint64_t)mapped_size);
    kc_embdex_write_u64(data + 88,
        (uint64_t)(record_size + lexical_size + mapped_size));
    kc_embdex_write_u64(data + 96, (uint64_t)words_size);

    offset = KC_EMBDEX_HEADER_SIZE;
    for (size_t i = 0; i < index->count; i++) {
        size_t id_length = strlen(index->records[i].id);
        size_t content_length = strlen(index->records[i].content);

        kc_embdex_write_u32(data + offset, (uint32_t)id_length);
        kc_embdex_write_u32(data + offset + 4, (uint32_t)content_length);
        offset += 8;
        memcpy(data + offset, index->records[i].id, id_length + 1);
        offset += id_length + 1;
        memcpy(data + offset, index->records[i].content,
            content_length + 1);
        offset += content_length + 1;
        for (size_t j = 0; j < index->dimension; j++) {
            kc_embdex_write_float(
                data + offset + j * sizeof(float),
                index->records[i].owned_vector[j]);
        }
        offset += index->dimension * sizeof(float);
    }
    if (offset != record_size) {
        status = KC_EMBDEX_EFORMAT;
        goto finished;
    }
    memcpy(data + offset, lexical_bytes, lexical_size);
    offset += lexical_size;
    if (mapped_size > 0) {
        memcpy(data + offset, mapped_bytes, mapped_size);
        offset += mapped_size;
    }
    kc_embdex_write_u32(data + offset, (uint32_t)index->word_count);
    offset += 4;
    for (size_t i = 0; i < index->word_count; i++) {
            size_t a = strlen(index->wordmap[i].source);
            size_t b = strlen(index->wordmap[i].replacement);

            kc_embdex_write_u32(data + offset, (uint32_t)a);
            kc_embdex_write_u32(data + offset + 4, (uint32_t)b);
            offset += 8;
            memcpy(data + offset, index->wordmap[i].source, a + 1);
            offset += a + 1;
            memcpy(data + offset, index->wordmap[i].replacement, b + 1);
            offset += b + 1;
    }
    if (offset != size) {
        status = KC_EMBDEX_EFORMAT;
        goto finished;
    }
    status = KC_EMBDEX_EIO;
    if (kc_mmap_open(&mapping, path) == KC_MMAP_OK &&
        kc_mmap_set(mapping, data, size) == KC_MMAP_OK &&
        kc_mmap_save(mapping) == KC_MMAP_OK)
        status = KC_EMBDEX_OK;

finished:
    kc_mmap_close(mapping);
    free(data);
    kc_bm25_free(lexical_bytes);
    kc_bm25_free(mapped_bytes);
    return status;
}

/**
 * Create a persistent index, closing the temporary build resources.
 * @param path File destination.
 * @param documents Borrowed associations.
 * @param count Number of associations.
 * @param dictionaries Dictionary paths, or NULL when absent.
 * @param dictionary_count Number of dictionary paths.
 * @param options Optional progress callback.
 * @return KC_EMBDEX_OK or a negative error code.
 */
int kc_embdex_build_with_dictionaries(const char *path,
    const kc_embdex_document_t *documents, size_t count,
    const char *const *dictionaries, size_t dictionary_count,
    const kc_embdex_build_options_t *options)
{
    kc_embdex_t *index = NULL;
    int status;

    if (path == NULL || path[0] == '\0' ||
        (dictionary_count > 0 && dictionaries == NULL))
        return KC_EMBDEX_EINVAL;
    status = kc_embdex_create(&index, documents, count,
        dictionaries, dictionary_count, options);
    if (status != KC_EMBDEX_OK) return status;
    if (options != NULL && options->progress != NULL)
        options->progress(KC_EMBDEX_BUILD_SAVE, 0, 0, options->userdata);
    status = kc_embdex_save(index, path);
    kc_embdex_close(index);
    if (status == KC_EMBDEX_OK &&
        options != NULL && options->progress != NULL)
        options->progress(KC_EMBDEX_BUILD_DONE, 0, 0, options->userdata);
    return status;
}

/**
 * Create a persistent index without dictionary substitutions.
 * @param path Destination index file, replaced on success.
 * @param documents Borrowed associations, or NULL when count is zero.
 * @param count Number of associations.
 * @return KC_EMBDEX_OK or a negative status code.
 */
int kc_embdex_build(const char *path,
    const kc_embdex_document_t *documents, size_t count)
{
    return kc_embdex_build_with_dictionaries(
        path, documents, count, NULL, 0, NULL);
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
    uint64_t mapped;
    uint64_t mapped_hits;
    uint64_t phrase;
    uint64_t literal_starts;
    uint64_t mapped_starts;
    size_t literal_length;
    size_t mapped_length;
    size_t composition_rank;
    size_t longest;
    size_t lexical_rank;
    size_t mapped_rank;
    double semantic;
    double fusion_sum;
    double fusion_rrf;
    int semantic_found;
    int semantic_measured;
    size_t semantic_rank;
    size_t semantic_variant;
    size_t variants_evaluated;
    double semantic_threshold;
    int full_phrase;
    int mapped_full_phrase;
    int accepted;
    size_t final_rank;
    double score;
    double coverage;
    double mapped_coverage;
    double precision;
    const char *reason;
} kc_embdex_evidence_t;

typedef struct {
    size_t ordinal;
    double score;
} kc_embdex_rank_t;

typedef struct {
    const char *id;
    size_t rank;
} kc_embdex_rank_id_t;

typedef struct {
    kc_embdex_evidence_t *evidence;
    size_t count;
    size_t position;
    int fuzzy;
    int mapped;
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

typedef struct {
    double score;
    size_t ordinal;
    size_t next_position;
    size_t next_slot;
    int valid;
} kc_embdex_cover_t;

/**
 * Match an entire normalized association against each contiguous query span.
 * The positional bitset avoids treating a bag-of-words hit as a phrase.
 * @param tokens Normalized query terms.
 * @param document Space-separated normalized association terms.
 * @param word_count Receives the association length.
 * @return Bitset of matching starting positions.
 */
static uint64_t kc_embdex_literal_starts(const kc_embdex_tokens_t *tokens,
    const char *document, size_t *word_count)
{
    const char *p = document;
    uint64_t starts = 0;
    size_t words = 0;

    while (*p != '\0') {
        while (*p != '\0' && *p != ' ') p++;
        words++;
        if (*p == ' ') p++;
    }
    *word_count = words;
    if (words == 0 || words > tokens->count) return 0;
    for (size_t start = 0; start + words <= tokens->count; start++) {
        const char *cursor = document;
        int equal = 1;

        for (size_t j = 0; j < words; j++) {
            size_t length = strlen(tokens->value[start + j]);
            size_t document_length = strcspn(cursor, " ");

            if (document_length != length ||
                memcmp(cursor, tokens->value[start + j], length) != 0)
            {
                equal = 0;
                break;
            }
            cursor += document_length;
            if (*cursor == ' ') cursor++;
        }
        if (equal) starts |= UINT64_C(1) << start;
    }
    return starts;
}

/**
 * Find the two best complete literal segmentations of a bounded query.
 * Each association is an edge spanning its entire contiguous phrase.
 * The DP favors longer spans and fewer fragments, then lets semantic
 * evidence order associations inside each selected covering.
 * @param evidence Per-association evidence and matching span bitsets.
 * @param count Association count.
 * @param token_count Query length, at most KC_EMBDEX_QUERY_TOKENS.
 * @return One when a complete literal segmentation exists; zero otherwise.
 * Winning association groups receive a rank of one or two.
 */
static int kc_embdex_cover_rank(kc_embdex_evidence_t *evidence,
    size_t count, size_t token_count)
{
    kc_embdex_cover_t best[KC_EMBDEX_QUERY_TOKENS + 1]
        [KC_EMBDEX_COVER_PATHS] = {{{0}}};

    if (token_count < 2 || token_count > KC_EMBDEX_QUERY_TOKENS)
        return 0;
    best[token_count][0].valid = 1;
    for (size_t position = token_count; position-- > 0;) {
        for (size_t i = 0; i < count; i++) {
            const kc_embdex_evidence_t *item = &evidence[i];

            for (size_t form = 0; form < 2; form++) {
                size_t length = form == 0 ?
                    item->literal_length : item->mapped_length;
                uint64_t starts = form == 0 ?
                    item->literal_starts : item->mapped_starts;

                if (length == 0 || position + length > token_count ||
                    (starts & (UINT64_C(1) << position)) == 0)
                    continue;
                if (form == 1 && length == item->literal_length &&
                    (item->literal_starts &
                        (UINT64_C(1) << position)) != 0)
                    continue;
                for (size_t slot = 0; slot < KC_EMBDEX_COVER_PATHS;
                    slot++)
                {
                    kc_embdex_cover_t suffix =
                        best[position + length][slot];
                    kc_embdex_cover_t candidate;

                    if (!suffix.valid) continue;
                    candidate.score = suffix.score + (double)length +
                        0.20 * (double)(length - 1) - 0.25 -
                        (form == 1 ? 0.05 : 0.0);
                    candidate.ordinal = i;
                    candidate.next_position = position + length;
                    candidate.next_slot = slot;
                    candidate.valid = 1;
                    for (size_t k = 0; k < KC_EMBDEX_COVER_PATHS; k++) {
                        if (!best[position][k].valid ||
                            candidate.score > best[position][k].score)
                        {
                            for (size_t shift = KC_EMBDEX_COVER_PATHS - 1;
                                shift > k; shift--)
                                best[position][shift] =
                                    best[position][shift - 1];
                            best[position][k] = candidate;
                            break;
                        }
                    }
                }
            }
        }
    }
    for (size_t route = 0; route < KC_EMBDEX_COVER_PATHS; route++) {
        size_t position = 0, slot = route;

        if (!best[0][route].valid) continue;
        while (position < token_count) {
            kc_embdex_cover_t edge = best[position][slot];
            kc_embdex_evidence_t *item = &evidence[edge.ordinal];

            if (item->composition_rank == 0 ||
                item->composition_rank > route + 1)
                item->composition_rank = route + 1;
            position = edge.next_position;
            slot = edge.next_slot;
        }
    }
    return best[0][0].valid;
}

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

typedef struct {
    const char *expected;
    size_t count;
    int matched;
} kc_embdex_aligned_token_t;

/**
 * Verify that one transformed token still occupies one BM25 position.
 * @return Zero to continue lexical traversal.
 */
static int kc_embdex_aligned_token_visit(const char *term, void *userdata)
{
    kc_embdex_aligned_token_t *state =
        (kc_embdex_aligned_token_t *)userdata;

    state->count++;
    if (state->count == 1 && strcmp(term, state->expected) == 0)
        state->matched = 1;
    return 0;
}

/**
 * Determine whether translated and original query tokens are positionally
 * aligned. Equal token counts alone do not establish correspondence:
 * a multiword source can produce the same number of unrelated target words.
 * @return KC_EMBDEX_OK or an allocation or lexical error.
 */
static int kc_embdex_mapped_alignment(const kc_embdex_t *index,
    const kc_embdex_tokens_t *original,
    const kc_embdex_tokens_t *mapped, int *aligned)
{
    *aligned = 0;
    if (original->truncated || mapped->truncated ||
        original->count != mapped->count) return KC_EMBDEX_OK;

    for (size_t i = 0; i < original->count; i++) {
        kc_embdex_aligned_token_t capture = {
            mapped->value[i], 0, 0
        };
        char *replacement = NULL;
        int status = kc_embdex_transform_text(index, original->value[i],
            0, NULL, &replacement);

        if (status != KC_EMBDEX_OK) return status;
        status = kc_bm25_tokens(index->mapped_lexical, replacement,
            kc_embdex_aligned_token_visit, &capture);
        free(replacement);
        if (status != KC_BM25_OK) return KC_EMBDEX_ELEXICAL;
        if (capture.count != 1 || !capture.matched) return KC_EMBDEX_OK;
    }
    *aligned = 1;
    return KC_EMBDEX_OK;
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
    if (context->mapped) {
        evidence->mapped_hits |= UINT64_C(1) << context->position;
        if (context->mapped == 1)
            evidence->mapped |= UINT64_C(1) << context->position;
    }
    else if (context->fuzzy)
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
static int kc_embdex_rank_id_compare(const void *left, const void *right)
{
    const kc_embdex_rank_id_t *a = (const kc_embdex_rank_id_t *)left;
    const kc_embdex_rank_id_t *b = (const kc_embdex_rank_id_t *)right;
    int order = strcmp(a->id, b->id);

    if (order != 0) return order;
    if (a->rank < b->rank) return -1;
    if (a->rank > b->rank) return 1;
    return 0;
}

/**
 * Sort candidate IDs while preserving the score order within each group.
 * @param left First ID and original score rank.
 * @param right Second ID and original score rank.
 * @return ID order, followed by score rank.
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
            "\",\"mapped\":\"%016" PRIx64
            "\",\"mapped_hits\":\"%016" PRIx64
            "\",\"phrase\":\"%016" PRIx64
            "\",\"longest\":%zu,\"bm25_rank\":%zu,"
            "\"mapped_bm25_rank\":%zu",
            item->exact, item->fuzzy, item->mapped, item->mapped_hits,
            item->phrase,
            item->longest, item->lexical_rank, item->mapped_rank);
        fputs(",\"cosine\":", file);
        if (item->semantic_measured)
            fprintf(file, "%.9g", item->semantic);
        else fputs("null", file);
        fprintf(file, ",\"coverage\":%.9g,\"mapped_coverage\":%.9g,"
            "\"precision\":%.9g,\"full_phrase\":%s,"
            "\"mapped_full_phrase\":%s,\"accepted\":%s,\"score\":",
            item->coverage, item->mapped_coverage, item->precision,
            item->full_phrase ? "true" : "false",
            item->mapped_full_phrase ? "true" : "false",
            item->accepted ? "true" : "false");
        if (item->accepted) fprintf(file, "%.9g", item->score);
        else fputs("null", file);
        fprintf(file, ",\"semantic_rank\":%zu,\"variant\":%zu,"
            "\"variants_evaluated\":%zu,\"variants_requested\":%zu,"
            "\"candidate_threshold\":%.9g,\"fusion_rrf\":%.9g,"
            "\"composition_rank\":%zu,\"literal_starts\":\"%016" PRIx64
            "\",\"mapped_starts\":\"%016" PRIx64
            "\",\"final_rank\":%zu,\"variant_scores\":[",
            item->semantic_rank, item->semantic_variant,
            item->variants_evaluated, requested_variants,
            item->semantic_threshold, item->fusion_rrf,
            item->composition_rank, item->literal_starts,
            item->mapped_starts, item->final_rank);
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
 * Bound substitution combinations over complete matched source terms,
 * not the individual tokens produced by BM25.
 * @return KC_EMBDEX_OK or an allocation error.
 */
static int kc_embdex_variant_count(const kc_embdex_t *index,
    const char *query, kc_embdex_variant_plan_t *plan)
{
    size_t combinations = 1, size = strlen(query), offset = 0;
    const size_t budget = KC_EMBDEX_VARIANTS;
    char *folded;

    plan->count = 1;
    plan->ambiguous = 0;
    plan->exhaustive = 1;
    if (index->word_count == 0) return KC_EMBDEX_OK;
    folded = kc_embdex_term_fold(query);
    if (folded == NULL) return KC_EMBDEX_ENOMEM;
    while (offset < size) {
        size_t first, alternatives;
        size_t length = kc_embdex_term_match(index,
            folded, size, offset, &first, &alternatives);

        (void)first;
        if (alternatives > 1) {
            plan->ambiguous++;
            combinations = combinations > budget / alternatives ?
                budget + 1 : combinations * alternatives;
        }
        offset += length != 0 ? length : 1;
    }
    free(folded);
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
    double semantic_floor, int truncated,
    double mapped_coverage, size_t mapped_count, int mapped_truncated)
{
    double similarity = item->semantic_found ? item->semantic : 0;

    if (item->semantic_measured && item->semantic_threshold > 0)
        semantic_floor = item->semantic_threshold;
    if (!truncated && item->full_phrase)
        return "complete_phrase";
    if (!truncated && !mapped_truncated && item->mapped_full_phrase)
        return "complete_translated_phrase";
    if (!truncated && !mapped_truncated && mapped_count != 0 &&
        mapped_coverage >= 1.0)
        return "strong_translated_coverage";
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
    double cosine = item->semantic_measured ? item->semantic : 0;
    double score = 0.45 * coverage + 0.25 * precision +
        0.15 * phrase_ratio + 0.10 * bm25 * coverage +
        0.50 * cosine - 0.08 * corrected_weight;

    if (item->full_phrase || item->mapped_full_phrase) score += 0.50;
    score += 0.25 * item->mapped_coverage;
    return score;
}

/**
 * Match a complete UTF-8 term within an arbitrary document without relying
 * on BM25 tokens. This handles symbol-only queries and dictionary targets.
 * @return Nonzero when the document contains the complete term.
 */
static int kc_embdex_contains_term(const char *document, const char *term)
{
    size_t size = strlen(document), length = strlen(term);
    const char *p;

    if (length == 0) return 0;
    p = document;
    while ((p = strstr(p, term)) != NULL) {
        size_t offset = (size_t)(p - document);

        if (kc_embdex_term_boundary(document, size, offset, length))
            return 1;
        p++;
    }
    return 0;
}

/**
 * For queries with no BM25 tokens, retain exact literal discovery over
 * source strings and their dictionary-substituted equivalents.
 * @return KC_EMBDEX_OK or a search error.
 */
static int kc_embdex_search_untokenized(const kc_embdex_t *index,
    const char *query, size_t limit,
    kc_embdex_result_fn callback, void *userdata)
{
    kc_embdex_rank_t *ranks;
    char *translated_query = NULL;
    size_t count = 0;
    int status;

    if (index->count > SIZE_MAX / sizeof(*ranks))
        return KC_EMBDEX_ENOMEM;
    ranks = (kc_embdex_rank_t *)malloc(index->count * sizeof(*ranks));
    if (ranks == NULL) return KC_EMBDEX_ENOMEM;
    status = kc_embdex_transform_text(index, query, 0,
        NULL, &translated_query);
    if (status != KC_EMBDEX_OK) {
        free(ranks);
        return status;
    }
    for (size_t i = 0; i < index->count; i++) {
        const char *content = index->records[i].content;
        char *translated_content = NULL;
        double score = 0;

        if (strcmp(content, query) == 0) score = 8.0;
        else if (kc_embdex_contains_term(content, query))
            score = 6.0;
        if (strcmp(translated_query, query) != 0) {
            if (strcmp(content, translated_query) == 0) {
                if (score < 7.0) score = 7.0;
            } else if (kc_embdex_contains_term(content,
                translated_query) && score < 5.0) score = 5.0;
        }
        if (index->word_count != 0) {
            status = kc_embdex_transform_text(index, content,
                0, NULL, &translated_content);
            if (status != KC_EMBDEX_OK) {
                free(translated_query);
                free(ranks);
                return status;
            }
            if (strcmp(translated_content, translated_query) == 0) {
                if (score < 7.0) score = 7.0;
            } else if (kc_embdex_contains_term(translated_content,
                translated_query) && score < 5.0) score = 5.0;
            free(translated_content);
        }
        if (score != 0) {
            ranks[count].ordinal = i;
            ranks[count++].score = score;
        }
    }
    qsort(ranks, count, sizeof(*ranks), kc_embdex_rank_compare);
    if (count > limit) count = limit;
    for (size_t i = 0; i < count; i++) {
        size_t ordinal = ranks[i].ordinal;
        kc_embdex_result_t result = {
            index->records[ordinal].id,
            index->records[ordinal].content,
            ranks[i].score
        };

        if (callback(&result, userdata) != 0) break;
    }
    free(translated_query);
    free(ranks);
    return KC_EMBDEX_OK;
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
    kc_embdex_tokens_t mapped_tokens = {0};
    kc_embdex_evidence_t *evidence = NULL;
    kc_embdex_rank_t *ranks = NULL;
    kc_bm25_result_t *lexical = NULL;
    kc_bm25_result_t *mapped_lexical = NULL;
    kc_hnsw_result_t *semantic = NULL;
    kc_bm25_search_options_t lexical_options = {0};
    char *joined = NULL;
    char *folded_query = NULL;
    char *folded_mapped_query = NULL;
    char *mapped_query = NULL;
    char *symbol_query = NULL;
    const char *search_query = query;
    float *embedding = NULL;
    size_t dimension = 0, limit = 10, window, lexical_count = 0;
    size_t mapped_lexical_count = 0;
    size_t semantic_count = 0, rank_count = 0, variants = 1;
    kc_embdex_variant_plan_t variant_plan = {0};
    size_t unique_variants = 0;
    char *variant_text[KC_EMBDEX_VARIANTS] = {0};
    double *variant_scores = NULL;
    double weights[KC_EMBDEX_QUERY_TOKENS] = {0};
    double total_weight = 0;
    int literal_sufficient = 0;
    int mapped_aligned = 0;
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
    if (tokens.count == 0 && index->word_count != 0) {
        int result;

        status = kc_embdex_transform_text(index, query,
            0, NULL, &symbol_query);
        if (status != KC_EMBDEX_OK) goto finished;
        if (strcmp(symbol_query, query) != 0) {
            result = kc_bm25_tokens(index->lexical, symbol_query,
                kc_embdex_token_add, &tokens);
            if (result != KC_BM25_OK || tokens.failed) {
                status = tokens.failed ? KC_EMBDEX_ENOMEM :
                    KC_EMBDEX_ELEXICAL;
                goto finished;
            }
            search_query = symbol_query;
        }
    }
    if (tokens.count == 0) {
        status = kc_embdex_search_untokenized(index, query, limit,
            callback, userdata);
        goto finished;
    }
    joined = kc_embdex_join(&tokens);
    folded_query = kc_embdex_term_fold(query);
    if (joined == NULL || folded_query == NULL) {
        status = KC_EMBDEX_ENOMEM;
        goto finished;
    }

    if (index->mapped_lexical != NULL) {
        int result = kc_embdex_transform_text(index, query, 0,
            NULL, &mapped_query);

        if (result != KC_EMBDEX_OK) {
            status = result;
            goto finished;
        }
        folded_mapped_query = kc_embdex_term_fold(mapped_query);
        if (folded_mapped_query == NULL) {
            status = KC_EMBDEX_ENOMEM;
            goto finished;
        }
        result = kc_bm25_tokens(index->mapped_lexical, mapped_query,
            kc_embdex_token_add, &mapped_tokens);
        if (mapped_tokens.failed) {
            status = KC_EMBDEX_ENOMEM;
            goto finished;
        }
        if (result != KC_BM25_OK) {
            status = KC_EMBDEX_ELEXICAL;
            goto finished;
        }
    }
    if (index->mapped_lexical != NULL &&
        mapped_tokens.count == 0 && mapped_query != NULL &&
        strcmp(mapped_query, query) != 0)
    {
        status = kc_embdex_search_untokenized(index, query,
            limit, callback, userdata);
        goto finished;
    }
    if (index->mapped_lexical != NULL) {
        status = kc_embdex_mapped_alignment(index, &tokens,
            &mapped_tokens, &mapped_aligned);
        if (status != KC_EMBDEX_OK) goto finished;
        for (size_t i = 0; i < mapped_tokens.count; i++) {
            kc_embdex_post_context_t post = {
                evidence, index->count, i, 0, mapped_aligned ? 1 : 2, 0, 0
            };
            int result = kc_bm25_postings(index->mapped_lexical,
                mapped_tokens.value[i], kc_embdex_posting_add, &post);

            if (result != KC_BM25_OK || post.failed) {
                status = KC_EMBDEX_ELEXICAL;
                goto finished;
            }
        }
    }
    for (size_t i = 0; i < tokens.count; i++) {
        kc_embdex_post_context_t post = {
            evidence, index->count, i, 0, 0, 0, 0
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
        int result = kc_bm25_search(index->lexical, search_query,
            &lexical_options, &lexical, &lexical_count);

        if (result != KC_BM25_OK) {
            status = result == KC_BM25_ENOMEM ?
                KC_EMBDEX_ENOMEM : KC_EMBDEX_ELEXICAL;
            goto finished;
        }
    }
    if (index->mapped_lexical != NULL) {
        int result = kc_bm25_search(index->mapped_lexical,
            mapped_query, &lexical_options,
            &mapped_lexical, &mapped_lexical_count);

        if (result != KC_BM25_OK) {
            status = result == KC_BM25_ENOMEM ?
                KC_EMBDEX_ENOMEM : KC_EMBDEX_ELEXICAL;
            goto finished;
        }
        for (size_t i = 0; i < mapped_lexical_count; i++) {
            if (mapped_lexical[i].ordinal >= index->count) {
                status = KC_EMBDEX_EINDEX;
                goto finished;
            }
            evidence[mapped_lexical[i].ordinal].mapped_rank = i + 1;
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
            evidence[i].mapped == 0 &&
            evidence[i].lexical_rank == 0 &&
            evidence[i].mapped_rank == 0) continue;
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
        if (!tokens.truncated)
            evidence[i].literal_starts = kc_embdex_literal_starts(
                &tokens, document.text, &evidence[i].literal_length);
        if (!tokens.truncated) {
            char *folded_content = kc_embdex_term_fold(
                index->records[i].content);

            if (folded_content == NULL) {
                free(document.text);
                status = KC_EMBDEX_ENOMEM;
                goto finished;
            }
            evidence[i].full_phrase = kc_embdex_contains_term(
                folded_content, folded_query);
            free(folded_content);
        }
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
        if (!tokens.truncated && index->mapped_lexical != NULL &&
            mapped_tokens.count > 0 && !mapped_tokens.truncated)
        {
            char *replacement = NULL;
            char *folded_replacement = NULL;

            status = kc_embdex_transform_text(index,
                index->records[i].content, 0, NULL, &replacement);
            if (status != KC_EMBDEX_OK) goto finished;
            folded_replacement = kc_embdex_term_fold(replacement);
            if (folded_replacement == NULL) {
                free(replacement);
                status = KC_EMBDEX_ENOMEM;
                goto finished;
            }
            evidence[i].mapped_full_phrase = kc_embdex_contains_term(
                folded_replacement, folded_mapped_query);
            free(folded_replacement);
            if (mapped_aligned) {
                kc_embdex_text_t normalized = {0};

                result = kc_bm25_tokens(index->mapped_lexical, replacement,
                    kc_embdex_text_append, &normalized);
                if (result != KC_BM25_OK || normalized.failed) {
                    free(replacement);
                    free(normalized.text);
                    status = normalized.failed ? KC_EMBDEX_ENOMEM :
                        KC_EMBDEX_ELEXICAL;
                    goto finished;
                }
                if (normalized.text != NULL)
                    evidence[i].mapped_starts = kc_embdex_literal_starts(
                        &mapped_tokens, normalized.text,
                        &evidence[i].mapped_length);
                free(normalized.text);
            }
            free(replacement);
        }
    }

    if (!tokens.truncated)
        kc_embdex_cover_rank(evidence, index->count, tokens.count);

    status = kc_embdex_variant_count(index, query, &variant_plan);
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
        if (!tokens.truncated) {
            for (size_t i = 0; i < index->count; i++)
                if (evidence[i].full_phrase ||
                    evidence[i].mapped_full_phrase)
                    literal_sufficient = 1;
        }
        if (tokens.truncated || !literal_sufficient) {
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

                        ranks[i].ordinal = i;
                        ranks[i].score = cosine;
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
                    if (variants > 1) {
                        qsort(ranks, index->count, sizeof(*ranks),
                            kc_embdex_rank_compare);
                        for (size_t i = 0; i < index->count; i++) {
                            size_t ordinal = ranks[i].ordinal;

                            evidence[ordinal].fusion_sum +=
                                2.0 / (2.0 + (double)i);
                        }
                    }
                } else {
                    /*
                     * HNSW discovers neighbors; acceptance thresholds must
                     * not discard them before the final ranking.
                     */
                    int result = kc_hnsw_search(index->graph,
                        embedding, window, -1.0,
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
                        ranks[i].ordinal = (size_t)ordinal;
                        ranks[i].score = semantic[i].score;
                        if (variant_scores != NULL)
                            variant_scores[(size_t)ordinal * variants +
                                unique_variants] = semantic[i].score;
                        if (!item->semantic_measured ||
                            semantic[i].score > item->semantic)
                        {
                            item->semantic = semantic[i].score;
                            item->semantic_measured = 1;
                            item->semantic_found =
                                semantic[i].score >= semantic_floor;
                            item->semantic_threshold = candidate_floor;
                            item->semantic_variant = variant;
                        }
                    }
                    if (variants > 1) {
                        qsort(ranks, semantic_count, sizeof(*ranks),
                            kc_embdex_rank_compare);
                        for (size_t i = 0; i < semantic_count; i++) {
                            size_t ordinal = ranks[i].ordinal;

                            evidence[ordinal].fusion_sum +=
                                2.0 / (2.0 + (double)i);
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
                    evidence[i].fusion_rrf = unique_variants > 0 ?
                        evidence[i].fusion_sum / (double)unique_variants : 0;
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
        uint64_t mask = item->exact | item->fuzzy |
            item->mapped | item->phrase;
        size_t exact = kc_embdex_popcount(item->exact | item->mapped);
        size_t corrected = kc_embdex_popcount(item->fuzzy & ~item->exact);
        double weight_covered = 0, weight_exact = 0, weight_corrected = 0;
        double coverage, precise, mapped_weight = 0;
        double phrase = (double)item->longest / (double)tokens.count;

        for (size_t j = 0; j < tokens.count; j++) {
            uint64_t bit = UINT64_C(1) << j;

            if ((mask & bit) != 0) weight_covered += weights[j];
            if ((item->exact & bit) != 0) weight_exact += weights[j];
            if ((item->mapped & ~item->exact & bit) != 0)
                mapped_weight += weights[j];
            if ((item->fuzzy & ~(item->exact | item->mapped) & bit) != 0)
                weight_corrected += weights[j];
        }
        coverage = total_weight > 0 ? weight_covered / total_weight : 0;
        precise = total_weight > 0 ?
            (weight_exact + 0.75 * mapped_weight) / total_weight : 0;
        item->coverage = coverage;
        item->mapped_coverage = mapped_tokens.count != 0 ?
            (double)kc_embdex_popcount(item->mapped_hits) /
            (double)mapped_tokens.count : 0;
        item->precision = precise;
        double score;
        const char *reason = item->composition_rank != 0 ?
            "literal_composition" :
            kc_embdex_accept(item, tokens.count,
                exact, corrected, coverage, semantic_only_floor,
                tokens.truncated, item->mapped_coverage,
                mapped_tokens.count, mapped_tokens.truncated);

        if (reason == NULL && mask == 0 && item->mapped_hits == 0 &&
            item->lexical_rank == 0 && item->mapped_rank == 0 &&
            !item->semantic_measured)
        {
            item->reason = "not_discovered";
            continue;
        }
        item->accepted = reason != NULL;
        item->reason = reason != NULL ? reason : "weak_candidate";
        score = kc_embdex_score(item, coverage, precise,
            total_weight > 0 ? weight_corrected / total_weight : 0,
            phrase);
        if (unique_variants > 1 && item->semantic_found)
            score += 0.20 * (item->fusion_rrf - item->semantic);
        if (item->mapped_rank != 0)
            score += 0.05 * coverage *
                (10.0 / (10.0 + (double)item->mapped_rank));
        if (item->composition_rank == 1) score += 6.0;
        else if (item->composition_rank == 2) score += 3.0;
        item->score = score;
        ranks[rank_count].ordinal = i;
        ranks[rank_count++].score = score;
    }

    qsort(ranks, rank_count, sizeof(*ranks), kc_embdex_rank_compare);
    /*
     * Preserve every association, while discounting repeated IDs so
     * one command cannot occupy the entire bounded result list.
     */
    {
        kc_embdex_rank_id_t *ids = (kc_embdex_rank_id_t *)malloc(
            rank_count * sizeof(*ids));

        if (ids == NULL && rank_count != 0) {
            status = KC_EMBDEX_ENOMEM;
            goto finished;
        }
        for (size_t i = 0; i < rank_count; i++) {
            ids[i].id = index->records[ranks[i].ordinal].id;
            ids[i].rank = i;
        }
        qsort(ids, rank_count, sizeof(*ids),
            kc_embdex_rank_id_compare);
        for (size_t i = 0, repeated = 0; i < rank_count; i++) {
            if (i == 0 || strcmp(ids[i].id, ids[i - 1].id) != 0)
                repeated = 0;
            else
                repeated++;
            if (repeated != 0) {
                size_t position = ids[i].rank;
                ranks[position].score /= (double)(repeated + 1);
                evidence[ranks[position].ordinal].score =
                    ranks[position].score;
            }
        }
        free(ids);
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
    kc_bm25_free(mapped_lexical);
    kc_hnsw_free(semantic);
    kc_emb_free(embedding);
    kc_embdex_tokens_close(&tokens);
    kc_embdex_tokens_close(&mapped_tokens);
    free(joined);
    free(folded_query);
    free(folded_mapped_query);
    free(mapped_query);
    free(symbol_query);
    for (size_t i = 0; i < KC_EMBDEX_VARIANTS; i++)
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
    kc_bm25_close(index->mapped_lexical);
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
