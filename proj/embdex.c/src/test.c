/**
 * test.c - Semantic Index Contract Tests
 * Summary: Checks semantic association, binary persistence and input errors.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libembdex.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define KC_TEST_CLI ".\\embdex.exe"
#define KC_TEST_NULL "NUL"
#else
#define KC_TEST_CLI "./embdex"
#define KC_TEST_NULL "/dev/null"
#endif

#ifdef __EMSCRIPTEN__
static int embdex_test_total = 7;
#else
static int embdex_test_total = 9;
#endif
static int embdex_test_current = 0;

/**
 * Print a numbered contract test result and its behavior.
 * @param name Name of the public contract case.
 * @param detail Behavior exercised by the test.
 * @param failed Nonzero when the case failed.
 * @return Zero on success, one on failure.
 */
static int embdex_test_result(
    const char *name,
    const char *detail,
    int failed
)
{
    printf("[%d/%d] [%s] %s: %s\n",
        embdex_test_current, embdex_test_total,
        failed ? "FAIL" : "PASS", name, detail);
    return failed ? 1 : 0;
}

/**
 * Test repeated association IDs, automatic embeddings and result limits.
 * @return Zero when all checks succeed, one otherwise.
 */
static int embdex_test_build(void)
{
    const kc_embdex_document_t corpus[] = {
        {"document", "Security and authentication between peers"},
        {"document", "Encryption and public keys"},
        {"other", "Storing personal documents"}
    };
    kc_embdex_t *index = NULL;
    kc_embdex_result_t *results = NULL;
    size_t result_count = 0;
    size_t limit = 3;
    double threshold = -1.0;
    kc_embdex_search_options_t options = {
        .limit = &limit,
        .threshold = &threshold
    };
    int failed = 0;
    int repeated = 0;

    failed |= kc_embdex_build(&index, corpus, 3) != KC_EMBDEX_OK;
    if (index != NULL) {
        failed |= kc_embdex_count(index) != 3;
        failed |= kc_embdex_search(
            index, "secure communication", &options, &results, &result_count
        ) != KC_EMBDEX_OK;
        failed |= result_count != 3;
        for (size_t i = 0; i < result_count; i++) {
            failed |= results[i].id == NULL || results[i].content == NULL;
            if (results[i].id != NULL) {
                repeated += strcmp(results[i].id, "document") == 0;
            }
        }
        failed |= repeated != 2;
    }
    kc_embdex_free(results);
    kc_embdex_close(index);
    return embdex_test_result("build_and_search",
        "preserves repeated IDs and ranks distinct records", failed);
}

/**
 * Test the index file round-trip without the original corpus.
 * @return Zero when all checks succeed, one otherwise.
 */
static int embdex_test_persistence(void)
{
    const char *path = "embdex-test-index.bin";
    const kc_embdex_document_t corpus[] = {
        {"laugh", "A smile, laughter and something funny"},
        {"file", "A serious technical document"}
    };
    kc_embdex_t *built = NULL;
    kc_embdex_t *opened = NULL;
    kc_embdex_result_t *results = NULL;
    size_t count = 0;
    int failed = 0;

    failed |= kc_embdex_build(&built, corpus, 2) != KC_EMBDEX_OK;
    if (built != NULL) {
        failed |= kc_embdex_save(built, path) != KC_EMBDEX_OK;
        kc_embdex_close(built);
        built = NULL;
        if (!failed) {
            failed |= kc_embdex_open(&opened, path) != KC_EMBDEX_OK;
        }
    }
    if (opened != NULL) {
        failed |= kc_embdex_count(opened) != 2;
        failed |= kc_embdex_search(
            opened, "something amusing", NULL, &results, &count
        ) != KC_EMBDEX_OK;
        failed |= count != 2;
        for (size_t i = 0; i < count; i++) {
            failed |= results[i].id == NULL;
            failed |= results[i].content == NULL;
        }
        failed |= kc_embdex_save(opened, path) != KC_EMBDEX_ESTATE;
        kc_embdex_free(results);
        results = NULL;
        failed |= kc_embdex_search_mode(opened, "laughter",
            KC_EMBDEX_MODE_LEXICAL, NULL, &results, &count) != KC_EMBDEX_OK;
        failed |= count != 1 || results == NULL;
        if (results != NULL && count == 1)
            failed |= strcmp(results[0].id, "laugh") != 0;
        kc_embdex_free(results);
        results = NULL;
        failed |= kc_embdex_search_mode(opened, "laughter",
            KC_EMBDEX_MODE_HYBRID, NULL, &results, &count) != KC_EMBDEX_OK;
        failed |= count != 2 || results == NULL;
        if (results != NULL && count == 2)
            failed |= strcmp(results[0].id, "laugh") != 0;
        kc_embdex_free(results);
        results = NULL;
        failed |= kc_embdex_search_mode(opened, "laughter",
            KC_EMBDEX_MODE_RERANK, NULL, &results, &count) != KC_EMBDEX_OK;
        failed |= count != 1 || results == NULL;
        if (results != NULL && count == 1) {
            failed |= strcmp(results[0].id, "laugh") != 0;
            failed |= !isfinite(results[0].score);
        }
    }
    kc_embdex_free(results);
    kc_embdex_close(built);
    kc_embdex_close(opened);
    remove(path);
    return embdex_test_result("binary_roundtrip",
        "restores associations without re-embedding", failed);
}

/**
 * Test invalid input, corrupted binaries and an empty corpus.
 * @return Zero when all checks succeed, one otherwise.
 */
static int embdex_test_errors(void)
{
    const char *path = "embdex-test-index.bin";
    const kc_embdex_document_t invalid[] = {{NULL, "text"}};
    kc_embdex_t *index = NULL;
    kc_embdex_t *opened = NULL;
    kc_embdex_result_t *results = NULL;
    size_t count = 9;
    FILE *file;
    int failed = 0;

    failed |= kc_embdex_build(&index, invalid, 1) != KC_EMBDEX_EINVAL;
    failed |= index != NULL;
    failed |= kc_embdex_build(&index, NULL, 0) != KC_EMBDEX_OK;
    if (index != NULL) {
        failed |= kc_embdex_search(
            index, "anything", NULL, &results, &count
        ) != KC_EMBDEX_OK;
        failed |= count != 0;
        failed |= results != NULL;
        failed |= kc_embdex_save(index, path) != KC_EMBDEX_OK;
        failed |= kc_embdex_open(&opened, path) != KC_EMBDEX_OK;
        failed |= opened == NULL || kc_embdex_count(opened) != 0;
        kc_embdex_close(opened);
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
            failed |= kc_embdex_open(&opened, path) != KC_EMBDEX_EFORMAT;
            failed |= opened != NULL;
            kc_embdex_close(opened);
            opened = NULL;
        } else failed = 1;

        failed |= kc_embdex_save(index, path) != KC_EMBDEX_OK;
        file = fopen(path, "r+b");
        if (file != NULL) {
            if (fseek(file, 68, SEEK_SET) == 0) {
                failed |= fputc(0xFF, file) == EOF;
            } else failed = 1;
            failed |= fclose(file) != 0;
            failed |= kc_embdex_open(&opened, path) != KC_EMBDEX_EFORMAT;
            failed |= opened != NULL;
            kc_embdex_close(opened);
            opened = NULL;
        } else failed = 1;
        kc_embdex_close(index);
        index = NULL;
    }
    file = fopen(path, "wb");
    if (file != NULL) {
        failed |= fwrite("bad", 1, 3, file) != 3;
        failed |= fclose(file) != 0;
        failed |= kc_embdex_open(&opened, path) != KC_EMBDEX_EFORMAT;
        failed |= opened != NULL;
    } else {
        failed = 1;
    }
    kc_embdex_close(opened);
    remove(path);
    return embdex_test_result("invalid_inputs",
        "rejects invalid records and corrupted indexes", failed);
}

/**
 * Build a compact corpus with duplicate identifiers and different terms.
 * @return Owned searchable index, or NULL if building fails.
 */
static kc_embdex_t *embdex_test_modes_index(void)
{
    const kc_embdex_document_t corpus[] = {
        {"shared", "alpha banana banana"},
        {"shared", "banana peach"},
        {"other", "coconut peach"}
    };
    kc_embdex_t *index = NULL;

    if (kc_embdex_build(&index, corpus, 3) != KC_EMBDEX_OK)
        return NULL;
    return index;
}

/**
 * Test exact BM25 candidates, scores, repeated IDs and lexical-only queries.
 * @return Zero when all lexical contracts hold, otherwise one.
 */
static int embdex_test_lexical(void)
{
    kc_embdex_t *index = embdex_test_modes_index();
    kc_embdex_result_t *results = NULL;
    size_t count = 99, limit = 2;
    kc_embdex_search_options_t options = {.limit = &limit};
    int failed = 0;

    if (index == NULL)
        return embdex_test_result("lexical_search",
            "finds exact tokens while preserving distinct associations", 1);
    failed |= kc_embdex_search_mode(index, "banana",
        KC_EMBDEX_MODE_LEXICAL, &options, &results, &count) != KC_EMBDEX_OK;
    failed |= count != 2 || results == NULL;
    if (results != NULL && count == 2) {
        failed |= strcmp(results[0].id, "shared") != 0;
        failed |= strcmp(results[1].id, "shared") != 0;
        failed |= strcmp(results[0].content, "alpha banana banana") != 0;
        failed |= strcmp(results[1].content, "banana peach") != 0;
        failed |= !(results[0].score > results[1].score);
    }
    kc_embdex_free(results);
    results = NULL;
    failed |= kc_embdex_search_mode(index, "alpha",
        KC_EMBDEX_MODE_LEXICAL, NULL, &results, &count) != KC_EMBDEX_OK;
    failed |= count != 1 || results == NULL;
    if (results != NULL && count == 1)
        failed |= strcmp(results[0].content, "alpha banana banana") != 0;
    kc_embdex_free(results);
    results = NULL;
    failed |= kc_embdex_search_mode(index, "nonexistentterm",
        KC_EMBDEX_MODE_LEXICAL, NULL, &results, &count) != KC_EMBDEX_OK;
    failed |= count != 0 || results != NULL;
    kc_embdex_close(index);
    return embdex_test_result("lexical_search",
        "finds exact tokens while preserving distinct associations", failed);
}

/**
 * Test RRF rank fusion, semantic-only candidates and deterministic overlap.
 * @return Zero when the hybrid ranking contract holds, otherwise one.
 */
static int embdex_test_hybrid(void)
{
    kc_embdex_t *index = embdex_test_modes_index();
    kc_embdex_result_t *lexical = NULL;
    kc_embdex_result_t *semantic = NULL;
    kc_embdex_result_t *results = NULL;
    size_t lexical_count = 0, semantic_count = 0, count = 0;
    int failed = 0;

    if (index == NULL)
        return embdex_test_result("hybrid_search",
            "fuses BM25 and HNSW positions by ordinal with RRF", 1);
    failed |= kc_embdex_search_mode(index, "alpha",
        KC_EMBDEX_MODE_LEXICAL, NULL,
        &lexical, &lexical_count) != KC_EMBDEX_OK;
    failed |= lexical_count != 1 || lexical == NULL;
    failed |= kc_embdex_search(index, "alpha", NULL,
        &semantic, &semantic_count) != KC_EMBDEX_OK;
    failed |= semantic_count != 3 || semantic == NULL;
    failed |= kc_embdex_search_mode(index, "alpha",
        KC_EMBDEX_MODE_HYBRID, NULL,
        &results, &count) != KC_EMBDEX_OK;
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
    kc_embdex_free(results);
    results = NULL;
    failed |= kc_embdex_search_mode(index, "nonexistentterm",
        KC_EMBDEX_MODE_HYBRID, NULL, &results, &count) != KC_EMBDEX_OK;
    failed |= count != 3 || results == NULL;
    kc_embdex_free(results);
    kc_embdex_free(semantic);
    kc_embdex_free(lexical);
    kc_embdex_close(index);
    return embdex_test_result("hybrid_search",
        "fuses BM25 and HNSW positions by ordinal with RRF", failed);
}

/**
 * Check cosine reranking is constrained to the lexical candidate set.
 * @return Zero when reranking and exclusions are correct, otherwise one.
 */
static int embdex_test_rerank(void)
{
    kc_embdex_t *index = embdex_test_modes_index();
    kc_embdex_result_t *results = NULL;
    size_t count = 0, limit = 1;
    kc_embdex_search_options_t options = {.limit = &limit};
    int failed = 0;

    if (index == NULL)
        return embdex_test_result("rerank_search",
            "scores only lexical matches using stored document embeddings", 1);
    failed |= kc_embdex_search_mode(index, "banana",
        KC_EMBDEX_MODE_RERANK, NULL, &results, &count) != KC_EMBDEX_OK;
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
    kc_embdex_free(results);
    results = NULL;
    failed |= kc_embdex_search_mode(index, "banana",
        KC_EMBDEX_MODE_RERANK, &options, &results, &count) != KC_EMBDEX_OK;
    failed |= count != 1 || results == NULL;
    kc_embdex_free(results);
    results = NULL;
    failed |= kc_embdex_search_mode(index, "nonexistentterm",
        KC_EMBDEX_MODE_RERANK, NULL, &results, &count) != KC_EMBDEX_OK;
    failed |= count != 0 || results != NULL;
    kc_embdex_close(index);
    return embdex_test_result("rerank_search",
        "scores only lexical matches using stored document embeddings", failed);
}

/**
 * Check legacy semantic behavior and the new mode validation contracts.
 * @return Zero when all mode argument and state checks hold, otherwise one.
 */
static int embdex_test_mode_contract(void)
{
    kc_embdex_t *index = embdex_test_modes_index();
    kc_embdex_result_t *results = (kc_embdex_result_t *)1;
    kc_embdex_result_t *reference = NULL;
    size_t count = 99, reference_count = 0, limit = 0;
    double bad = 2.0;
    kc_embdex_search_options_t options = {
        .limit = &limit, .threshold = &bad
    };
    int failed = 0;

    if (index == NULL)
        return embdex_test_result("mode_contract",
            "retains semantic behavior and validates modes and options", 1);
    failed |= kc_embdex_search_mode(index, "alpha", 99, NULL,
        &results, &count) != KC_EMBDEX_EINVAL;
    failed |= results != NULL || count != 0;
    failed |= kc_embdex_search_mode(index, "alpha",
        KC_EMBDEX_MODE_HYBRID, &options, &results,
        &count) != KC_EMBDEX_EINVAL;
    failed |= results != NULL || count != 0;
    bad = -1.0;
    failed |= kc_embdex_search_mode(index, "alpha",
        KC_EMBDEX_MODE_HYBRID, &options, &results,
        &count) != KC_EMBDEX_OK;
    failed |= results != NULL || count != 0;
    limit = 3;
    failed |= kc_embdex_search(index, "alpha", &options,
        &reference, &reference_count) != KC_EMBDEX_OK;
    failed |= kc_embdex_search_mode(index, "alpha",
        KC_EMBDEX_MODE_SEMANTIC, &options,
        &results, &count) != KC_EMBDEX_OK;
    failed |= count != reference_count;
    if (results != NULL && reference != NULL && count == reference_count) {
        for (size_t i = 0; i < count; i++) {
            failed |= strcmp(results[i].content, reference[i].content) != 0;
            failed |= results[i].score != reference[i].score;
        }
    } else failed = 1;
    kc_embdex_free(results);
    kc_embdex_free(reference);
    results = NULL;
    count = 99;
    failed |= kc_embdex_search_mode(NULL, "alpha",
        KC_EMBDEX_MODE_LEXICAL, NULL, &results,
        &count) != KC_EMBDEX_EINVAL;
    failed |= count != 0 || results != NULL;
    failed |= kc_embdex_strerror(KC_EMBDEX_ELEXICAL) == NULL;
    kc_embdex_close(index);
    return embdex_test_result("mode_contract",
        "retains semantic behavior and validates modes and options", failed);
}

#ifndef __EMSCRIPTEN__
/**
 * Verify the CLI help and version output using a text stream.
 * @return Zero when the public CLI information follows the kclib contract.
 */
static int embdex_test_cli_info(void)
{
    const char *path = "embdex-cli-info.txt";
    FILE *file;
    char line[512];
    int failed = 0;
    int usage = 0, commands = 0, options = 0;
    int build = 0, search = 0, help = 0, version = 0;

    failed |= system(
        KC_TEST_CLI " --help > embdex-cli-info.txt 2>&1"
    ) != 0;
    file = fopen(path, "r");
    if (file == NULL) {
        failed = 1;
    } else {
        while (fgets(line, sizeof(line), file) != NULL) {
            usage |= strstr(line, "Usage: ") != NULL &&
                strstr(line, " <command> <index> [options]") != NULL;
            commands |= strcmp(line, "Commands:\n") == 0;
            options |= strcmp(line, "Options:\n") == 0;
            build |= strstr(line, "  build <index>") == line;
            search |= strstr(line, "  search <index> <query>") == line;
            help |= strstr(line, "-h, --help") != NULL;
            version |= strstr(line, "-v, --version") != NULL;
        }
        failed |= ferror(file) != 0;
        failed |= fclose(file) != 0;
    }
    failed |= !(usage && commands && options && build && search &&
        help && version);

    failed |= system(
        KC_TEST_CLI " --version > embdex-cli-info.txt 2>&1"
    ) != 0;
    file = fopen(path, "r");
    if (file == NULL) {
        failed = 1;
    } else {
        unsigned long long build_version = 0;
        char extra;

        failed |= fscanf(file, "%llu %c", &build_version, &extra) != 1;
        failed |= fclose(file) != 0;
        failed |= build_version != (unsigned long long)kc_embdex_version();
    }
    failed |= system(
        KC_TEST_CLI " --help build > " KC_TEST_NULL " 2>&1"
    ) == 0;
    failed |= system(
        KC_TEST_CLI " --version search > " KC_TEST_NULL " 2>&1"
    ) == 0;
    remove(path);
    return embdex_test_result("cli_info",
        "prints kclib command help and the library build version", failed);
}

/**
 * Check that the CLI builds and searches tab-separated associations.
 * @return Zero when the CLI contract works, one otherwise.
 */
static int embdex_test_cli(void)
{
    FILE *file;
    char line[4096];
    int failed = 0;
    int rows = 0;
    int found_security = 0;
    int found_storage = 0;

    file = fopen("embdex-cli-input.tsv", "wb");
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
            KC_TEST_CLI " build embdex-cli-index.bin -i "
            "embdex-cli-input.tsv > " KC_TEST_NULL " 2>&1"
        ) != 0;
    }
    if (!failed) {
        failed |= system(
            KC_TEST_CLI " search embdex-cli-index.bin "
            "security -l 2 > embdex-cli-output.tsv 2>&1"
        ) != 0;
    }
    file = fopen("embdex-cli-output.tsv", "rb");
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
        KC_TEST_CLI " search embdex-cli-index.bin "
        "authentication --mode lexical > embdex-cli-output.tsv 2>&1"
    ) != 0;
    file = fopen("embdex-cli-output.tsv", "rb");
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
        KC_TEST_CLI " search embdex-cli-index.bin "
        "authentication -m hybrid > embdex-cli-output.tsv 2>&1"
    ) != 0;
    file = fopen("embdex-cli-output.tsv", "rb");
    if (file != NULL) {
        rows = 0;
        while (fgets(line, sizeof(line), file) != NULL) rows++;
        failed |= rows != 2;
        failed |= fclose(file) != 0;
    } else failed = 1;
    failed |= system(
        KC_TEST_CLI " search embdex-cli-index.bin "
        "authentication -m rerank > embdex-cli-output.tsv 2>&1"
    ) != 0;
    file = fopen("embdex-cli-output.tsv", "rb");
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
        KC_TEST_CLI " search embdex-cli-index.bin "
        "authentication -m unsupported > " KC_TEST_NULL " 2>&1"
    ) == 0;

    remove("embdex-cli-input.tsv");
    remove("embdex-cli-index.bin");
    remove("embdex-cli-output.tsv");
    return embdex_test_result("embdex_cli",
        "builds and queries tab-separated text without a JSON parser", failed);
}

#endif

/**
 * Run one named test and accumulate its result.
 * @param name Selected contract case name.
 * @return Zero when the selected test succeeds, one or two otherwise.
 */
static int embdex_run_case(const char *name)
{
    embdex_test_current++;
    if (strcmp(name, "build_and_search") == 0) {
        return embdex_test_build();
    }
    if (strcmp(name, "lexical_search") == 0) {
        return embdex_test_lexical();
    }
    if (strcmp(name, "hybrid_search") == 0) {
        return embdex_test_hybrid();
    }
    if (strcmp(name, "rerank_search") == 0) {
        return embdex_test_rerank();
    }
    if (strcmp(name, "mode_contract") == 0) {
        return embdex_test_mode_contract();
    }
    if (strcmp(name, "binary_roundtrip") == 0) {
        return embdex_test_persistence();
    }
    if (strcmp(name, "invalid_inputs") == 0) {
        return embdex_test_errors();
    }
#ifndef __EMSCRIPTEN__
    if (strcmp(name, "cli_info") == 0) {
        return embdex_test_cli_info();
    }
    if (strcmp(name, "embdex_cli") == 0) {
        return embdex_test_cli();
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
        "cli_info",
        "embdex_cli",
#endif
    };
    int failed = 0;

    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument\n");
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            failed += embdex_run_case(cases[i]);
        }
        printf("%d passed, %d failed\n",
            embdex_test_total - failed, failed);
        return failed == 0 ? 0 : 1;
    }
    embdex_test_total = 1;
    return embdex_run_case(argv[1]);
}
