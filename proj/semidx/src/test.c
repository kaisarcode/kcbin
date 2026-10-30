/**
 * test.c - Semantic Index Contract Tests
 * Summary: Checks semantic association, binary persistence and input errors.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libsemidx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Print a named test outcome and return its failure count.
 * @param name Public test case name.
 * @param failed Nonzero when the test failed.
 * @return Zero for a pass, one for a failure.
 */
static int semidx_test_result(const char *name, int failed)
{
    printf("[%s] %s\n", failed ? "FAIL" : "PASS", name);
    return failed ? 1 : 0;
}

/**
 * Test repeated association IDs, automatic embeddings and result limits.
 * @return Zero when all checks succeed, one otherwise.
 */
static int semidx_test_build(void)
{
    const kc_semidx_document_t corpus[] = {
        {"document", "Security and authentication between peers"},
        {"document", "Encryption and public keys"},
        {"other", "Storing personal documents"}
    };
    kc_semidx_t *index = NULL;
    kc_semidx_result_t *results = NULL;
    size_t result_count = 0;
    size_t limit = 3;
    double threshold = -1.0;
    kc_semidx_search_options_t options = {
        .limit = &limit,
        .threshold = &threshold
    };
    int failed = 0;
    int repeated = 0;

    failed |= kc_semidx_build(&index, corpus, 3) != KC_SEMIDX_OK;
    if (index != NULL) {
        failed |= kc_semidx_count(index) != 3;
        failed |= kc_semidx_search(
            index, "secure communication", &options, &results, &result_count
        ) != KC_SEMIDX_OK;
        failed |= result_count != 3;
        for (size_t i = 0; i < result_count; i++) {
            repeated += strcmp(results[i].id, "document") == 0;
            failed |= results[i].content == NULL;
        }
        failed |= repeated != 2;
    }
    kc_semidx_free(results);
    kc_semidx_close(index);
    return semidx_test_result("build_and_search", failed);
}

/**
 * Test the index file round-trip without the original corpus.
 * @param path Writable test file path.
 * @return Zero when all checks succeed, one otherwise.
 */
static int semidx_test_persistence(const char *path)
{
    const kc_semidx_document_t corpus[] = {
        {"laugh", "A smile, laughter and something funny"},
        {"file", "A serious technical document"}
    };
    kc_semidx_t *built = NULL;
    kc_semidx_t *opened = NULL;
    kc_semidx_result_t *results = NULL;
    size_t count = 0;
    int failed = 0;

    failed |= kc_semidx_build(&built, corpus, 2) != KC_SEMIDX_OK;
    if (built != NULL) {
        failed |= kc_semidx_save(built, path) != KC_SEMIDX_OK;
        kc_semidx_close(built);
        built = NULL;
        if (!failed) {
            failed |= kc_semidx_open(&opened, path) != KC_SEMIDX_OK;
        }
    }
    if (opened != NULL) {
        failed |= kc_semidx_count(opened) != 2;
        failed |= kc_semidx_search(
            opened, "something amusing", NULL, &results, &count
        ) != KC_SEMIDX_OK;
        failed |= count != 2;
        for (size_t i = 0; i < count; i++) {
            failed |= results[i].id == NULL;
            failed |= results[i].content == NULL;
        }
        failed |= kc_semidx_save(opened, path) != KC_SEMIDX_ESTATE;
    }
    kc_semidx_free(results);
    kc_semidx_close(built);
    kc_semidx_close(opened);
    remove(path);
    return semidx_test_result("binary_roundtrip", failed);
}

/**
 * Test invalid input, corrupted binaries and an empty corpus.
 * @param path Writable test file path.
 * @return Zero when all checks succeed, one otherwise.
 */
static int semidx_test_errors(const char *path)
{
    const kc_semidx_document_t invalid[] = {{NULL, "text"}};
    kc_semidx_t *index = NULL;
    kc_semidx_t *opened = NULL;
    kc_semidx_result_t *results = NULL;
    size_t count = 9;
    FILE *file;
    int failed = 0;

    failed |= kc_semidx_build(&index, invalid, 1) != KC_SEMIDX_EINVAL;
    failed |= index != NULL;
    failed |= kc_semidx_build(&index, NULL, 0) != KC_SEMIDX_OK;
    if (index != NULL) {
        failed |= kc_semidx_search(
            index, "anything", NULL, &results, &count
        ) != KC_SEMIDX_OK;
        failed |= count != 0;
        failed |= results != NULL;
        failed |= kc_semidx_save(index, path) != KC_SEMIDX_OK;
        kc_semidx_close(index);
        index = NULL;
        failed |= kc_semidx_open(&opened, path) != KC_SEMIDX_OK;
        failed |= opened == NULL || kc_semidx_count(opened) != 0;
        kc_semidx_close(opened);
        opened = NULL;
    }
    file = fopen(path, "wb");
    if (file != NULL) {
        failed |= fwrite("bad", 1, 3, file) != 3;
        failed |= fclose(file) != 0;
        failed |= kc_semidx_open(&opened, path) != KC_SEMIDX_EFORMAT;
        failed |= opened != NULL;
    } else {
        failed = 1;
    }
    kc_semidx_close(opened);
    remove(path);
    return semidx_test_result("invalid_inputs", failed);
}

/**
 * Run the public semantic index contract tests.
 * @param argc Command-line argument count.
 * @param argv Command-line arguments containing one temporary file path.
 * @return Process exit status.
 */
int main(int argc, char **argv)
{
    int failed = 0;

    if (argc != 2) {
        fprintf(stderr, "Usage: semidx-test <temporary-index-file>\n");
        return 1;
    }
    failed += semidx_test_build();
    failed += semidx_test_persistence(argv[1]);
    failed += semidx_test_errors(argv[1]);
    return failed == 0 ? 0 : 1;
}
