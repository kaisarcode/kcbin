/**
 * libembdex.h - Semantic Index Public API
 * Summary: Build and query portable indexes of text associations.
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

#define KC_EMBDEX_MODE_SEMANTIC 1
#define KC_EMBDEX_MODE_LEXICAL 2
#define KC_EMBDEX_MODE_HYBRID 3
#define KC_EMBDEX_MODE_RERANK 4

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
    const double *threshold;
} kc_embdex_search_options_t;

/**
 * Build an index from text associations, allowing duplicate and arbitrary IDs.
 * Input strings are borrowed for the call and copied into the index.
 * The caller owns the returned index and releases it with kc_embdex_close().
 * @param out Receives the newly built index.
 * @param documents Array of ID and content pairs, or NULL when count is zero.
 * @param count Number of documents in the array.
 * @return KC_EMBDEX_OK or a negative status code.
 */
int kc_embdex_build(
    kc_embdex_t **out,
    const kc_embdex_document_t *documents,
    size_t count
);

/**
 * Open a saved binary index using file-backed memory.
 * The source dataset is not needed; the HNSW graph is rebuilt in memory.
 * @param out Receives the opened index.
 * @param path Existing binary index file.
 * @return KC_EMBDEX_OK or a negative status code.
 */
int kc_embdex_open(kc_embdex_t **out, const char *path);

/**
 * Persist a newly built index as one portable binary file.
 * An opened index is read-only and cannot be saved again.
 * The current target file is overwritten and saving is not atomic.
 * @param index Index created with kc_embdex_build().
 * @param path Destination file.
 * @return KC_EMBDEX_OK or a negative status code.
 */
int kc_embdex_save(const kc_embdex_t *index, const char *path);

/**
 * Search by semantic similarity and return matching associations.
 * Omitted options default to ten results and minimum cosine score -1.
 * Results are caller-owned; ID and content strings borrow index storage.
 * Duplicate document IDs remain distinct matches.
 * @param index Index to query.
 * @param query Natural-language query string.
 * @param options Optional limit and minimum cosine similarity.
 * @param out_results Receives result array, released with kc_embdex_free().
 * @param out_count Receives number of matches.
 * @return KC_EMBDEX_OK or a negative status code.
 */
int kc_embdex_search(
    const kc_embdex_t *index,
    const char *query,
    const kc_embdex_search_options_t *options,
    kc_embdex_result_t **out_results,
    size_t *out_count
);

/**
 * Search using the semantic, lexical, hybrid RRF, or lexical-first reranking
 * strategy. Semantic mode is equivalent to kc_embdex_search().
 * Lexical scores use BM25. Hybrid scores use reciprocal rank fusion with
 * constant 60. Rerank scores use cosine similarity over the leading BM25
 * candidates. Hybrid and rerank consider at least 100 candidates per source,
 * or the requested result limit when larger.
 * The threshold applies to cosine similarity in semantic and rerank modes
 * and to the semantic branch in hybrid mode; lexical mode ignores it.
 * Documents with repeated public IDs are joined by their private ordinal.
 * @param index Built or opened index.
 * @param query UTF-8 search query.
 * @param mode KC_EMBDEX_MODE_* search mode.
 * @param options Optional result limit and cosine threshold.
 * @param out_results Receives caller-owned results.
 * Release results with kc_embdex_free().
 * @param out_count Receives the result count.
 * @return KC_EMBDEX_OK or an error status.
 */
int kc_embdex_search_mode(
    const kc_embdex_t *index,
    const char *query,
    int mode,
    const kc_embdex_search_options_t *options,
    kc_embdex_result_t **out_results,
    size_t *out_count
);

/**
 * Return the number of indexed associations, including repeated IDs.
 * @param index Index handle.
 * @return Number of indexed associations, or zero for NULL.
 */
size_t kc_embdex_count(const kc_embdex_t *index);

/**
 * Release the result array allocated by kc_embdex_search().
 * @param ptr Result array or NULL; borrowed strings are not released.
 * @return void
 */
void kc_embdex_free(void *ptr);

/**
 * Release an index and its mapped file or owned vectors.
 * @param index Index handle or NULL.
 * @return void
 */
void kc_embdex_close(kc_embdex_t *index);

/**
 * Return an explanatory message for a public status code.
 * @param status Status code.
 * @return Static status message.
 */
const char *kc_embdex_strerror(int status);

/**
 * Return the build timestamp generated by the build system.
 * @return Unix timestamp or zero if unspecified.
 */
uint64_t kc_embdex_version(void);

#ifdef __cplusplus
}
#endif

#endif
