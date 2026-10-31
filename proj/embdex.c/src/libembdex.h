/**
 * libembdex.h - Text association retrieval.
 * Summary: Public API for building and searching portable indexes.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_EMBDEX_H
#define KC_EMBDEX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_embdex kc_embdex_t;

#define KC_EMBDEX_OK 0
#define KC_EMBDEX_EINVAL -1
#define KC_EMBDEX_ENOMEM -2
#define KC_EMBDEX_EIO -3
#define KC_EMBDEX_EFORMAT -4
#define KC_EMBDEX_EEMBED -5
#define KC_EMBDEX_EINDEX -6
#define KC_EMBDEX_ESTATE -7
#define KC_EMBDEX_ELEXICAL -8

typedef struct {
    const char *id;
    const char *content;
} kc_embdex_document_t;

typedef struct {
    const char *id;
    const char *content;
    double score;
} kc_embdex_result_t;

typedef struct {
    const size_t *limit;
} kc_embdex_search_options_t;

/**
 * Identify the current phase of a build.
 */
typedef enum {
    KC_EMBDEX_BUILD_PREPARE,
    KC_EMBDEX_BUILD_EMBED,
    KC_EMBDEX_BUILD_SAVE,
    KC_EMBDEX_BUILD_DONE
} kc_embdex_build_stage_t;

/**
 * Observe build phases without changing build results.
 * The embedding phase reports completed associations and their total.
 * Other phases report zero for both counters. Callback data is borrowed.
 * @param stage Current build phase.
 * @param completed Number of associations already embedded.
 * @param total Total associations requiring an embedding.
 * @param userdata Caller-owned context.
 * @return None.
 */
typedef void (*kc_embdex_progress_fn)(
    kc_embdex_build_stage_t stage,
    size_t completed,
    size_t total,
    void *userdata
);

/**
 * Configure optional synchronous progress notifications during a build.
 * Omit this structure or set progress to NULL to disable notifications.
 */
typedef struct {
    kc_embdex_progress_fn progress;
    void *userdata;
} kc_embdex_build_options_t;

/**
 * Visit one relevant association. The result and its strings are borrowed
 * for the duration of the callback. A nonzero return stops delivery without
 * signaling a search failure.
 * @param result Current relevant association and score.
 * @param userdata Caller-provided context.
 * @return Zero to continue, nonzero to stop delivering results.
 */
typedef int (*kc_embdex_result_fn)(
    const kc_embdex_result_t *result,
    void *userdata
);

/**
 * Create and persist an index from independent ID/text associations.
 * IDs may repeat. The input is borrowed for the duration of the call.
 * On success the destination contains the complete reusable index; no
 * separate save or close operation is required for building.
 * @param path Destination index file, replaced on success.
 * @param documents Array of associations, or NULL when count is zero.
 * @param count Number of associations.
 * @return KC_EMBDEX_OK or a negative status code.
 */
int kc_embdex_build(
    const char *path,
    const kc_embdex_document_t *documents,
    size_t count
);

/**
 * Compile associations with any number of independent word dictionaries.
 * Each dictionary is a TSV file of source-word / replacement-word pairs.
 * Repeated sources provide alternative substitutions; exact duplicate pairs
 * across files are deduplicated. Earlier files take precedence for the
 * primary substitution used when compiling vectors and normalized BM25.
 * The resulting map embeds its merged dictionary and can be opened without
 * any build inputs. No language labels or per-word semantic metadata are
 * interpreted.
 * @param path Destination compiled binary map.
 * @param documents Input ID/text associations.
 * @param count Number of associations.
 * @param dictionaries Array of dictionary file paths, or NULL when empty.
 * @param dictionary_count Number of dictionary files.
 * @param options Optional progress callback; NULL disables notifications.
 * @return KC_EMBDEX_OK or a negative status.
 */
int kc_embdex_build_with_dictionaries(
    const char *path,
    const kc_embdex_document_t *documents,
    size_t count,
    const char *const *dictionaries,
    size_t dictionary_count,
    const kc_embdex_build_options_t *options
);

/**
 * Open an existing index for repeated queries.
 * @param out Receives the opened index handle.
 * @param path Path to the index file.
 * @return KC_EMBDEX_OK or a negative status code.
 */
int kc_embdex_open(kc_embdex_t **out, const char *path);

/**
 * Find relevant associations using automatic retrieval.
 * Results are delivered in descending score order. No result allocations
 * are transferred to the caller. An omitted limit defaults to ten; a
 * limit of zero delivers no results. The callback may stop early.
 * @param index Opened index.
 * @param query UTF-8 query text.
 * @param options Optional maximum number of results.
 * @param callback Receives borrowed results during this call.
 * @param userdata Caller-provided context.
 * @return KC_EMBDEX_OK or a negative status code.
 */
int kc_embdex_search(
    const kc_embdex_t *index,
    const char *query,
    const kc_embdex_search_options_t *options,
    kc_embdex_result_fn callback,
    void *userdata
);

/**
 * Release an opened index and its resources.
 * @param index Opened index handle, or NULL.
 * @return None.
 */
void kc_embdex_close(kc_embdex_t *index);

/**
 * Describe a public error status.
 * @param status Returned status code.
 * @return Static error description.
 */
const char *kc_embdex_strerror(int status);

/**
 * Return the generated build timestamp.
 * @return Unix timestamp, or zero if unspecified.
 */
uint64_t kc_embdex_version(void);

#ifdef __cplusplus
}
#endif

#endif
