/**
 * test.c - Semantic Index Contract Tests
 * Summary: Checks semantic association, binary persistence and input errors.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libsemidx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define KC_TEST_CLI ".\\semidx.exe"
#define KC_TEST_NULL "NUL"
#else
#define KC_TEST_CLI "./semidx"
#define KC_TEST_NULL "/dev/null"
#endif

#ifdef __EMSCRIPTEN__
static int semidx_test_total = 7;
#else
static int semidx_test_total = 8;
#endif
static int semidx_test_current = 0;

/**
 * Print a numbered contract test result and its behavior.
 * @param name Name of the public contract case.
 * @param detail Behavior exercised by the test.
 * @param failed Nonzero when the case failed.
 * @return Zero on success, one on failure.
 */
static int semidx_test_result(
    const char *name,
    const char *detail,
    int failed
)
{
    printf("[%d/%d] [%s] %s: %s\n",
        semidx_test_current, semidx_test_total,
        failed ? "FAIL" : "PASS", name, detail);
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
            failed |= results[i].id == NULL || results[i].content == NULL;
            if (results[i].id != NULL) {
                repeated += strcmp(results[i].id, "document") == 0;
            }
        }
        failed |= repeated != 2;
    }
    kc_semidx_free(results);
    kc_semidx_close(index);
    return semidx_test_result("build_and_search",
        "preserves repeated IDs and ranks distinct records", failed);
}

/**
 * Test the index file round-trip without the original corpus.
 * @return Zero when all checks succeed, one otherwise.
 */
static int semidx_test_persistence(void)
{
    const char *path = "semidx-test-index.bin";
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
        kc_semidx_free(results);
        results = NULL;
        failed |= kc_semidx_search_mode(opened, "laughter",
            KC_SEMIDX_MODE_LEXICAL, NULL, &results, &count) != KC_SEMIDX_OK;
        failed |= count != 1 || results == NULL;
        if (results != NULL && count == 1)
            failed |= strcmp(results[0].id, "laugh") != 0;
        kc_semidx_free(results);
        results = NULL;
        failed |= kc_semidx_search_mode(opened, "laughter",
            KC_SEMIDX_MODE_HYBRID, NULL, &results, &count) != KC_SEMIDX_OK;
        failed |= count != 2 || results == NULL;
        if (results != NULL && count == 2)
            failed |= strcmp(results[0].id, "laugh") != 0;
        kc_semidx_free(results);
        results = NULL;
        failed |= kc_semidx_search_mode(opened, "laughter",
            KC_SEMIDX_MODE_RERANK, NULL, &results, &count) != KC_SEMIDX_OK;
        failed |= count != 1 || results == NULL;
        if (results != NULL && count == 1) {
            failed |= strcmp(results[0].id, "laugh") != 0;
            failed |= !isfinite(results[0].score);
        }
    }
    kc_semidx_free(results);
    kc_semidx_close(built);
    kc_semidx_close(opened);
    remove(path);
    return semidx_test_result("binary_roundtrip",
        "restores associations without re-embedding", failed);
}

/**
 * Test invalid input, corrupted binaries and an empty corpus.
 * @return Zero when all checks succeed, one otherwise.
 */
static int semidx_test_errors(void)
{
    const char *path = "semidx-test-index.bin";
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
        failed |= kc_semidx_open(&opened, path) != KC_SEMIDX_OK;
        failed |= opened == NULL || kc_semidx_count(opened) != 0;
        kc_semidx_close(opened);
        opened = NULL;

        file = fopen(path, "r+b");
        if (file != NULL) {
            if (fseek(file, 52, SEEK_SET) == 0) {
                int value = fgetc(file);

                if (value == EOF || fseek(file, 52, SEEK_SET) != 0 ||
                    fputc(value ^ 1, file) == EOF) {
                    failed = 1;
                }
            } else failed = 1;
            failed |= fclose(file) != 0;
            failed |= kc_semidx_open(&opened, path) != KC_SEMIDX_EFORMAT;
            failed |= opened != NULL;
            kc_semidx_close(opened);
            opened = NULL;
        } else failed = 1;

        failed |= kc_semidx_save(index, path) != KC_SEMIDX_OK;
        file = fopen(path, "r+b");
        if (file != NULL) {
            if (fseek(file, 68, SEEK_SET) == 0) {
                failed |= fputc(0xFF, file) == EOF;
            } else failed = 1;
            failed |= fclose(file) != 0;
            failed |= kc_semidx_open(&opened, path) != KC_SEMIDX_EFORMAT;
            failed |= opened != NULL;
            kc_semidx_close(opened);
            opened = NULL;
        } else failed = 1;
        kc_semidx_close(index);
        index = NULL;
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
    return semidx_test_result("invalid_inputs",
        "rejects invalid records and corrupted indexes", failed);
}

/**
 * Build a compact corpus with duplicate identifiers and different terms.
 * @return Owned searchable index, or NULL if building fails.
 */
static kc_semidx_t *semidx_test_modes_index(void)
{
    const kc_semidx_document_t corpus[] = {
        {"shared", "alpha banana banana"},
        {"shared", "banana peach"},
        {"other", "coconut peach"}
    };
    kc_semidx_t *index = NULL;

    if (kc_semidx_build(&index, corpus, 3) != KC_SEMIDX_OK)
        return NULL;
    return index;
}

/**
 * Test exact BM25 candidates, scores, repeated IDs and lexical-only queries.
 * @return Zero when all lexical contracts hold, otherwise one.
 */
static int semidx_test_lexical(void)
{
    kc_semidx_t *index = semidx_test_modes_index();
    kc_semidx_result_t *results = NULL;
    size_t count = 99, limit = 2;
    kc_semidx_search_options_t options = {.limit = &limit};
    int failed = 0;

    if (index == NULL)
        return semidx_test_result("lexical_search",
            "finds exact tokens while preserving distinct associations", 1);
    failed |= kc_semidx_search_mode(index, "banana",
        KC_SEMIDX_MODE_LEXICAL, &options, &results, &count) != KC_SEMIDX_OK;
    failed |= count != 2 || results == NULL;
    if (results != NULL && count == 2) {
        failed |= strcmp(results[0].id, "shared") != 0;
        failed |= strcmp(results[1].id, "shared") != 0;
        failed |= strcmp(results[0].content, "alpha banana banana") != 0;
        failed |= strcmp(results[1].content, "banana peach") != 0;
        failed |= !(results[0].score > results[1].score);
    }
    kc_semidx_free(results);
    results = NULL;
    failed |= kc_semidx_search_mode(index, "alpha",
        KC_SEMIDX_MODE_LEXICAL, NULL, &results, &count) != KC_SEMIDX_OK;
    failed |= count != 1 || results == NULL;
    if (results != NULL && count == 1)
        failed |= strcmp(results[0].content, "alpha banana banana") != 0;
    kc_semidx_free(results);
    results = NULL;
    failed |= kc_semidx_search_mode(index, "nonexistentterm",
        KC_SEMIDX_MODE_LEXICAL, NULL, &results, &count) != KC_SEMIDX_OK;
    failed |= count != 0 || results != NULL;
    kc_semidx_close(index);
    return semidx_test_result("lexical_search",
        "finds exact tokens while preserving distinct associations", failed);
}

/**
 * Test RRF rank fusion, semantic-only candidates and deterministic overlap.
 * @return Zero when the hybrid ranking contract holds, otherwise one.
 */
static int semidx_test_hybrid(void)
{
    kc_semidx_t *index = semidx_test_modes_index();
    kc_semidx_result_t *lexical = NULL;
    kc_semidx_result_t *semantic = NULL;
    kc_semidx_result_t *results = NULL;
    size_t lexical_count = 0, semantic_count = 0, count = 0;
    int failed = 0;

    if (index == NULL)
        return semidx_test_result("hybrid_search",
            "fuses BM25 and HNSW positions by ordinal with RRF", 1);
    failed |= kc_semidx_search_mode(index, "alpha",
        KC_SEMIDX_MODE_LEXICAL, NULL,
        &lexical, &lexical_count) != KC_SEMIDX_OK;
    failed |= lexical_count != 1 || lexical == NULL;
    failed |= kc_semidx_search(index, "alpha", NULL,
        &semantic, &semantic_count) != KC_SEMIDX_OK;
    failed |= semantic_count != 3 || semantic == NULL;
    failed |= kc_semidx_search_mode(index, "alpha",
        KC_SEMIDX_MODE_HYBRID, NULL,
        &results, &count) != KC_SEMIDX_OK;
    failed |= count != 3 || results == NULL;
    if (lexical != NULL && semantic != NULL && results != NULL &&
        lexical_count == 1 && semantic_count == 3 && count == 3)
    {
        for (size_t i = 0; i < count; i++) {
            double expected = 0.0;

            for (size_t j = 0; j < lexical_count; j++) {
                if (strcmp(results[i].content, lexical[j].content) == 0)
                    expected += 1.0 / (61.0 + (double)j);
            }
            for (size_t j = 0; j < semantic_count; j++) {
                if (strcmp(results[i].content, semantic[j].content) == 0)
                    expected += 1.0 / (61.0 + (double)j);
            }
            failed |= !isfinite(results[i].score);
            failed |= fabs(results[i].score - expected) > 1e-12;
            if (i > 0)
                failed |= results[i - 1].score < results[i].score;
        }
    }
    kc_semidx_free(results);
    results = NULL;
    failed |= kc_semidx_search_mode(index, "nonexistentterm",
        KC_SEMIDX_MODE_HYBRID, NULL, &results, &count) != KC_SEMIDX_OK;
    failed |= count != 3 || results == NULL;
    kc_semidx_free(results);
    kc_semidx_free(semantic);
    kc_semidx_free(lexical);
    kc_semidx_close(index);
    return semidx_test_result("hybrid_search",
        "fuses BM25 and HNSW positions by ordinal with RRF", failed);
}

/**
 * Check cosine reranking is constrained to the lexical candidate set.
 * @return Zero when reranking and exclusions are correct, otherwise one.
 */
static int semidx_test_rerank(void)
{
    kc_semidx_t *index = semidx_test_modes_index();
    kc_semidx_result_t *results = NULL;
    size_t count = 0, limit = 1;
    kc_semidx_search_options_t options = {.limit = &limit};
    int failed = 0;

    if (index == NULL)
        return semidx_test_result("rerank_search",
            "scores only lexical matches using stored document embeddings", 1);
    failed |= kc_semidx_search_mode(index, "banana",
        KC_SEMIDX_MODE_RERANK, NULL, &results, &count) != KC_SEMIDX_OK;
    failed |= count != 2 || results == NULL;
    if (results != NULL && count == 2) {
        int a = 0, b = 0;

        for (size_t i = 0; i < count; i++) {
            a += strcmp(results[i].content, "alpha banana banana") == 0;
            b += strcmp(results[i].content, "banana peach") == 0;
            failed |= !isfinite(results[i].score);
            failed |= results[i].score < -1.0 || results[i].score > 1.0;
        }
        failed |= a != 1 || b != 1;
        failed |= results[0].score < results[1].score;
    }
    kc_semidx_free(results);
    results = NULL;
    failed |= kc_semidx_search_mode(index, "banana",
        KC_SEMIDX_MODE_RERANK, &options, &results, &count) != KC_SEMIDX_OK;
    failed |= count != 1 || results == NULL;
    kc_semidx_free(results);
    results = NULL;
    failed |= kc_semidx_search_mode(index, "nonexistentterm",
        KC_SEMIDX_MODE_RERANK, NULL, &results, &count) != KC_SEMIDX_OK;
    failed |= count != 0 || results != NULL;
    kc_semidx_close(index);
    return semidx_test_result("rerank_search",
        "scores only lexical matches using stored document embeddings", failed);
}

/**
 * Check legacy semantic behavior and the new mode validation contracts.
 * @return Zero when all mode argument and state checks hold, otherwise one.
 */
static int semidx_test_mode_contract(void)
{
    kc_semidx_t *index = semidx_test_modes_index();
    kc_semidx_result_t *results = (kc_semidx_result_t *)1;
    kc_semidx_result_t *reference = NULL;
    size_t count = 99, reference_count = 0, limit = 0;
    double bad = 2.0;
    kc_semidx_search_options_t options = {
        .limit = &limit, .threshold = &bad
    };
    int failed = 0;

    if (index == NULL)
        return semidx_test_result("mode_contract",
            "retains semantic behavior and validates modes and options", 1);
    failed |= kc_semidx_search_mode(index, "alpha", 99, NULL,
        &results, &count) != KC_SEMIDX_EINVAL;
    failed |= results != NULL || count != 0;
    failed |= kc_semidx_search_mode(index, "alpha",
        KC_SEMIDX_MODE_HYBRID, &options, &results,
        &count) != KC_SEMIDX_EINVAL;
    failed |= results != NULL || count != 0;
    bad = -1.0;
    failed |= kc_semidx_search_mode(index, "alpha",
        KC_SEMIDX_MODE_HYBRID, &options, &results,
        &count) != KC_SEMIDX_OK;
    failed |= results != NULL || count != 0;
    limit = 3;
    failed |= kc_semidx_search(index, "alpha", &options,
        &reference, &reference_count) != KC_SEMIDX_OK;
    failed |= kc_semidx_search_mode(index, "alpha",
        KC_SEMIDX_MODE_SEMANTIC, &options,
        &results, &count) != KC_SEMIDX_OK;
    failed |= count != reference_count;
    if (results != NULL && reference != NULL && count == reference_count) {
        for (size_t i = 0; i < count; i++) {
            failed |= strcmp(results[i].content, reference[i].content) != 0;
            failed |= results[i].score != reference[i].score;
        }
    } else failed = 1;
    kc_semidx_free(results);
    kc_semidx_free(reference);
    results = NULL;
    count = 99;
    failed |= kc_semidx_search_mode(NULL, "alpha",
        KC_SEMIDX_MODE_LEXICAL, NULL, &results,
        &count) != KC_SEMIDX_EINVAL;
    failed |= count != 0 || results != NULL;
    failed |= kc_semidx_strerror(KC_SEMIDX_ELEXICAL) == NULL;
    kc_semidx_close(index);
    return semidx_test_result("mode_contract",
        "retains semantic behavior and validates modes and options", failed);
}

#ifndef __EMSCRIPTEN__
/**
 * Check that the CLI builds and searches tab-separated associations.
 * @return Zero when the CLI contract works, one otherwise.
 */
static int semidx_test_cli(void)
{
    FILE *file;
    char line[4096];
    int failed = 0;
    int rows = 0;
    int found_security = 0;
    int found_storage = 0;

    file = fopen("semidx-cli-input.tsv", "wb");
    if (file == NULL) {
        failed = 1;
    } else {
        failed |= fputs(
            "shared\tAuthentication and encrypted peer communication\n",
            file
        ) == EOF;
        failed |= fputs(
            "shared\tStoring files and indexing documents\n",
            file
        ) == EOF;
        failed |= fclose(file) != 0;
    }
    failed |= system(
        KC_TEST_CLI " -h > " KC_TEST_NULL " 2>&1"
    ) != 0;
    failed |= system(
        KC_TEST_CLI " -v > " KC_TEST_NULL " 2>&1"
    ) != 0;
    if (!failed) {
        failed |= system(
            KC_TEST_CLI " build semidx-cli-index.bin -i "
            "semidx-cli-input.tsv > " KC_TEST_NULL " 2>&1"
        ) != 0;
    }
    if (!failed) {
        failed |= system(
            KC_TEST_CLI " search semidx-cli-index.bin "
            "security -l 2 > semidx-cli-output.tsv 2>&1"
        ) != 0;
    }
    file = fopen("semidx-cli-output.tsv", "rb");
    if (file != NULL) {
        while (fgets(line, sizeof(line), file) != NULL) {
            rows++;
            failed |= strncmp(line, "shared\t", 7) != 0;
            found_security |= strstr(line, "encrypted peer") != NULL;
            found_storage |= strstr(line, "indexing documents") != NULL;
        }
        failed |= fclose(file) != 0;
    } else {
        failed = 1;
    }
    failed |= rows != 2 || !found_security || !found_storage;

    failed |= system(
        KC_TEST_CLI " search semidx-cli-index.bin "
        "authentication --mode lexical > semidx-cli-output.tsv 2>&1"
    ) != 0;
    file = fopen("semidx-cli-output.tsv", "rb");
    if (file != NULL) {
        failed |= fgets(line, sizeof(line), file) == NULL;
        if (line[0] != '\0') {
            failed |= strncmp(line, "shared\t", 7) != 0;
            failed |= strstr(line, "encrypted peer") == NULL;
        }
        failed |= fgets(line, sizeof(line), file) != NULL;
        failed |= fclose(file) != 0;
    } else failed = 1;

    failed |= system(
        KC_TEST_CLI " search semidx-cli-index.bin "
        "authentication -m hybrid > semidx-cli-output.tsv 2>&1"
    ) != 0;
    file = fopen("semidx-cli-output.tsv", "rb");
    if (file != NULL) {
        rows = 0;
        while (fgets(line, sizeof(line), file) != NULL) rows++;
        failed |= rows != 2;
        failed |= fclose(file) != 0;
    } else failed = 1;
    failed |= system(
        KC_TEST_CLI " search semidx-cli-index.bin "
        "authentication -m rerank > semidx-cli-output.tsv 2>&1"
    ) != 0;
    file = fopen("semidx-cli-output.tsv", "rb");
    if (file != NULL) {
        rows = 0;
        while (fgets(line, sizeof(line), file) != NULL) {
            rows++;
            failed |= strstr(line, "encrypted peer") == NULL;
        }
        failed |= rows != 1;
        failed |= fclose(file) != 0;
    } else failed = 1;
    failed |= system(
        KC_TEST_CLI " search semidx-cli-index.bin "
        "authentication -m unsupported > " KC_TEST_NULL " 2>&1"
    ) == 0;

    remove("semidx-cli-input.tsv");
    remove("semidx-cli-index.bin");
    remove("semidx-cli-output.tsv");
    return semidx_test_result("semidx_cli",
        "builds and queries tab-separated text without a JSON parser", failed);
}

#endif

/**
 * Run one named test and accumulate its result.
 * @param name Selected contract case name.
 * @return Zero when the selected test succeeds, one or two otherwise.
 */
static int semidx_run_case(const char *name)
{
    semidx_test_current++;
    if (strcmp(name, "build_and_search") == 0) {
        return semidx_test_build();
    }
    if (strcmp(name, "lexical_search") == 0) {
        return semidx_test_lexical();
    }
    if (strcmp(name, "hybrid_search") == 0) {
        return semidx_test_hybrid();
    }
    if (strcmp(name, "rerank_search") == 0) {
        return semidx_test_rerank();
    }
    if (strcmp(name, "mode_contract") == 0) {
        return semidx_test_mode_contract();
    }
    if (strcmp(name, "binary_roundtrip") == 0) {
        return semidx_test_persistence();
    }
    if (strcmp(name, "invalid_inputs") == 0) {
        return semidx_test_errors();
    }
#ifndef __EMSCRIPTEN__
    if (strcmp(name, "semidx_cli") == 0) {
        return semidx_test_cli();
    }
#endif
    fprintf(stderr, "unknown test case: %s\n", name);
    return 2;
}

/**
 * Run all public contract cases or one selected case.
 * @param argc Argument count.
 * @param argv Argument vector with case name or all.
 * @return Process status, zero on success.
 */
int main(int argc, char **argv)
{
    const char *cases[] = {
        "build_and_search",
        "lexical_search",
        "hybrid_search",
        "rerank_search",
        "mode_contract",
        "binary_roundtrip",
        "invalid_inputs",
#ifndef __EMSCRIPTEN__
        "semidx_cli",
#endif
    };
    int failed = 0;

    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument\n");
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            failed += semidx_run_case(cases[i]);
        }
        printf("%d passed, %d failed\n",
            semidx_test_total - failed, failed);
        return failed == 0 ? 0 : 1;
    }
    semidx_test_total = 1;
    return semidx_run_case(argv[1]);
}
