/**
 * test.c - Embdex public contract tests.
 * Summary: Checks persistent build, retrieval callbacks and CLI behavior.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && \
    !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "libembdex.h"

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
#define KC_TEST_TOTAL 6
#else
#define KC_TEST_TOTAL 9
#endif

static int kc_test_current = 0;

typedef struct {
    size_t count;
    size_t maximum;
    int found;
    int repeated;
    double previous;
    int ordered;
} kc_test_results_t;

/**
 * Record one public contract assertion with a numbered result.
 * @param name Case identifier.
 * @param detail Contract being exercised.
 * @param failed Whether any assertion failed.
 * @return Zero on success, one otherwise.
 */
static int kc_test_result(const char *name, const char *detail, int failed)
{
    printf("[%d/%d] [%s] %s: %s\n",
        kc_test_current, KC_TEST_TOTAL,
        failed ? "FAIL" : "PASS", name, detail);
    return failed ? 1 : 0;
}

/**
 * Inspect one borrowed result and optionally stop early.
 * @param result Borrowed association and score.
 * @param userdata Mutable test observations.
 * @return Nonzero after the configured result count has been delivered.
 */
static int kc_test_receive(const kc_embdex_result_t *result, void *userdata)
{
    kc_test_results_t *state = (kc_test_results_t *)userdata;

    if (result == NULL || result->id == NULL || result->content == NULL)
        return 1;
    if (state->count != 0 && result->score > state->previous)
        state->ordered = 0;
    state->previous = result->score;
    state->count++;
    if (strcmp(result->id, "shared") == 0) state->repeated++;
    if (strstr(result->content, "authentication") != NULL)
        state->found = 1;
    return state->maximum != 0 && state->count >= state->maximum;
}

/**
 * Build a reusable corpus and verify callbacks, limits and ordering.
 * @return Zero when indexed associations remain searchable.
 */
static int kc_test_search(void)
{
    const kc_embdex_document_t documents[] = {
        {"shared", "authentication and encrypted peer communication"},
        {"shared", "authentication and public keys"},
        {"other", "gardening flowers and plants"}
    };
    const char *path = "embdex-test-index.bin";
    kc_embdex_t *index = NULL;
    kc_test_results_t state = {0};
    size_t limit = 3, zero = 0;
    kc_embdex_search_options_t options = { .limit = &limit };
    int failed = 0;

    remove(path);
    failed |= kc_embdex_build(path, documents, 3) != KC_EMBDEX_OK;
    if (!failed) failed |= kc_embdex_open(&index, path) != KC_EMBDEX_OK;
    if (!failed) {
        state.ordered = 1;
        failed |= kc_embdex_search(index, "authentication", &options,
            kc_test_receive, &state) != KC_EMBDEX_OK;
        failed |= state.count == 0 || state.count > 3;
        failed |= !state.found || !state.ordered;
        state = (kc_test_results_t){0};
        state.ordered = 1;
        state.maximum = 1;
        failed |= kc_embdex_search(index, "authentication", NULL,
            kc_test_receive, &state) != KC_EMBDEX_OK;
        failed |= state.count != 1;
        options.limit = &zero;
        state = (kc_test_results_t){0};
        failed |= kc_embdex_search(index, "authentication", &options,
            kc_test_receive, &state) != KC_EMBDEX_OK;
        failed |= state.count != 0;
    }
    kc_embdex_close(index);
    remove(path);
    return kc_test_result("search",
        "builds once and delivers sorted borrowed results", failed);
}

/**
 * Check that repeated public IDs represent independent associations.
 * @return Zero when both associations can be retrieved independently.
 */
static int kc_test_duplicates(void)
{
    const kc_embdex_document_t documents[] = {
        {"shared", "authentication between peers"},
        {"shared", "authentication between clients"}
    };
    const char *path = "embdex-test-duplicates.bin";
    kc_embdex_t *index = NULL;
    kc_test_results_t state = {0};
    int failed = 0;

    remove(path);
    failed |= kc_embdex_build(path, documents, 2) != KC_EMBDEX_OK;
    if (!failed) failed |= kc_embdex_open(&index, path) != KC_EMBDEX_OK;
    if (!failed) {
        failed |= kc_embdex_search(index, "authentication", NULL,
            kc_test_receive, &state) != KC_EMBDEX_OK;
        failed |= state.count != 2 || state.repeated != 2;
    }
    kc_embdex_close(index);
    remove(path);
    return kc_test_result("duplicates",
        "keeps duplicate IDs as distinct indexed records", failed);
}

/**
 * Validate error statuses and invalid public arguments.
 * @return Zero when errors are reported without leaking index ownership.
 */
static int kc_test_invalid(void)
{
    const kc_embdex_document_t bad[] = {{NULL, "content"}};
    const kc_embdex_document_t valid[] = {{"id", "text"}};
    kc_embdex_t *index = NULL;
    int failed = 0;

    failed |= kc_embdex_build(NULL, valid, 1) != KC_EMBDEX_EINVAL;
    failed |= kc_embdex_build("", valid, 1) != KC_EMBDEX_EINVAL;
    failed |= kc_embdex_build("unused.bin", NULL, 1) != KC_EMBDEX_EINVAL;
    failed |= kc_embdex_build("unused.bin", bad, 1) != KC_EMBDEX_EINVAL;
    failed |= kc_embdex_open(NULL, "unused.bin") != KC_EMBDEX_EINVAL;
    failed |= kc_embdex_open(&index, "") != KC_EMBDEX_EINVAL;
    failed |= kc_embdex_search(NULL, "hello", NULL,
        kc_test_receive, NULL) != KC_EMBDEX_EINVAL;
    failed |= kc_embdex_strerror(KC_EMBDEX_EINVAL) == NULL;
    kc_embdex_close(index);
    kc_embdex_close(NULL);
    remove("unused.bin");
    return kc_test_result("invalid",
        "rejects invalid build, open and search arguments", failed);
}

/**
 * Reject a truncated or corrupted persistent index.
 * @return Zero when malformed content cannot be opened as an index.
 */
static int kc_test_format(void)
{
    const char *path = "embdex-test-corrupt.bin";
    kc_embdex_t *index = NULL;
    FILE *file;
    int failed = 0;

    file = fopen(path, "wb");
    if (file == NULL) failed = 1;
    else {
        failed |= fwrite("invalid", 1, 7, file) != 7;
        failed |= fclose(file) != 0;
    }
    if (!failed)
        failed |= kc_embdex_open(&index, path) != KC_EMBDEX_EFORMAT;
    failed |= index != NULL;
    kc_embdex_close(index);
    remove(path);
    return kc_test_result("format",
        "rejects corrupt persisted data without partial handles", failed);
}

typedef struct {
    size_t count;
    double first;
    char first_id[64];
} kc_embdex_match_test_t;

/**
 * Inspect one retrieved association for result ranking assertions.
 * @param result Borrowed ranked association.
 * @param userdata Mutable observations.
 * @return Zero to continue collecting.
 */
static int kc_test_match(const kc_embdex_result_t *result, void *userdata)
{
    kc_embdex_match_test_t *state = (kc_embdex_match_test_t *)userdata;

    if (state->count == 0) {
        size_t size = strlen(result->id);

        if (size >= sizeof(state->first_id)) return 1;
        memcpy(state->first_id, result->id, size + 1);
        state->first = result->score;
    }
    state->count++;
    return 0;
}

/**
 * Verify phrase coverage and Unicode spelling alternatives over dataset words.
 * @return Zero when recovered associations outrank partial matches.
 */
static int kc_test_relevance(void)
{
    const kc_embdex_document_t documents[] = {
        {"meatball", "Albóndigas caseras con salsa de tomate"},
        {"pasta", "Pasta con salsa de tomate"},
        {"cutlet", "Milanesa con puré"}
    };
    kc_embdex_t *index = NULL;
    kc_embdex_match_test_t result = {0};
    const char *path = "embdex-test-relevance.bin";
    int failed = 0;

    remove(path);
    failed |= kc_embdex_build(path, documents, 3) != KC_EMBDEX_OK;
    if (!failed)
        failed |= kc_embdex_open(&index, path) != KC_EMBDEX_OK;
    if (!failed) {
        failed |= kc_embdex_search(index, "almondigas caseras", NULL,
            kc_test_match, &result) != KC_EMBDEX_OK;
        failed |= result.count == 0 ||
            strcmp(result.first_id, "meatball") != 0;
        result = (kc_embdex_match_test_t){0};
        failed |= kc_embdex_search(index, "albondigas casers lloviendo",
            NULL, kc_test_match, &result) != KC_EMBDEX_OK;
        failed |= result.count == 0 ||
            strcmp(result.first_id, "meatball") != 0;
        result = (kc_embdex_match_test_t){0};
        failed |= kc_embdex_search(index, "salsa de tomate", NULL,
            kc_test_match, &result) != KC_EMBDEX_OK;
        failed |= result.count == 0 ||
            (strcmp(result.first_id, "pasta") != 0 &&
            strcmp(result.first_id, "meatball") != 0);
    }
    kc_embdex_close(index);
    remove(path);
    return kc_test_result("relevance",
        "recovers misspellings and full phrases before partial matches", failed);
}

/**
 * Validate empty queries and malformed Unicode before dispatching inference.
 * @return Zero when invalid or empty text cannot generate spurious results.
 */
static int kc_test_query(void)
{
    const kc_embdex_document_t documents[] = {
        {"one", "Welcome to the project"}
    };
    const char *path = "embdex-test-query.bin";
    kc_embdex_match_test_t result = {0};
    kc_embdex_t *index = NULL;
    int failed = 0;

    remove(path);
    failed |= kc_embdex_build(path, documents, 1) != KC_EMBDEX_OK;
    if (!failed)
        failed |= kc_embdex_open(&index, path) != KC_EMBDEX_OK;
    if (!failed) {
        failed |= kc_embdex_search(index, "", NULL,
            kc_test_match, &result) != KC_EMBDEX_OK;
        failed |= result.count != 0;
        failed |= kc_embdex_search(index, "\xC3\x28", NULL,
            kc_test_match, &result) != KC_EMBDEX_ELEXICAL;
        failed |= result.count != 0;
    }
    kc_embdex_close(index);
    remove(path);
    return kc_test_result("query",
        "does not match empty text and rejects malformed UTF-8", failed);
}

#ifndef __EMSCRIPTEN__
/**
 * Ensure several independent dictionaries compile into a self-contained map.
 * Dictionary files are deleted before opening and querying the saved index.
 * @return Zero when independent word substitutions work after reopen.
 */
static int kc_test_wordmap(void)
{
    const kc_embdex_document_t documents[] = {
        {"secret", "guardar contraseñas privadas"},
        {"garden", "cuidar plantas en el jardín"}
    };
    const char *dictionary_a = "embdex-test-dict-a.tsv";
    const char *dictionary_b = "embdex-test-dict-b.tsv";
    const char *dictionaries[] = { dictionary_a, dictionary_b };
    const char *path = "embdex-test-wordmap.bin";
    kc_embdex_t *index = NULL;
    kc_embdex_match_test_t result = {0};
    FILE *file;
    int failed = 0;

    remove(path);
    file = fopen(dictionary_a, "wb");
    if (file == NULL) failed = 1;
    else {
        failed |= fputs("guardar\tstore\n", file) == EOF;
        failed |= fputs("contraseñas\tpasswords\n", file) == EOF;
        failed |= fclose(file) != 0;
    }
    file = fopen(dictionary_b, "wb");
    if (file == NULL) failed = 1;
    else {
        failed |= fputs("claves\tpasswords\n", file) == EOF;
        failed |= fputs("claves\tkeys\n", file) == EOF;
        failed |= fputs("guardar\tstore\n", file) == EOF;
        failed |= fclose(file) != 0;
    }
    if (!failed)
        failed |= kc_embdex_build_with_dictionaries(path, documents, 2,
            dictionaries, 2) != KC_EMBDEX_OK;
    remove(dictionary_a);
    remove(dictionary_b);
    if (!failed) failed |= kc_embdex_open(&index, path) != KC_EMBDEX_OK;
    if (!failed) {
        failed |= kc_embdex_search(index, "guardar claves", NULL,
            kc_test_match, &result) != KC_EMBDEX_OK;
        failed |= result.count == 0 ||
            strcmp(result.first_id, "secret") != 0;
    }
    kc_embdex_close(index);
    remove(path);
    return kc_test_result("wordmap",
        "merges dictionaries and searches standalone compiled mmap maps",
        failed);
}

/**
 * Check conventional CLI help and build-version output.
 * @return Zero when help and version operations are self-contained.
 */
static int kc_test_cli_info(void)
{
    FILE *file;
    char line[512];
    unsigned long long version = 0;
    char extra;
    int failed = 0, usage = 0, commands = 0, options = 0, modes = 0;

    failed |= system(KC_TEST_CLI " --help > embdex-cli-info.txt 2>&1") != 0;
    file = fopen("embdex-cli-info.txt", "r");
    if (file == NULL) failed = 1;
    else {
        while (fgets(line, sizeof(line), file) != NULL) {
            usage |= strstr(line, "Usage: ") != NULL;
            commands |= strstr(line, "Commands:") != NULL;
            options |= strstr(line, "Options:") != NULL;
            modes |= strstr(line, "--mode") != NULL;
        }
        failed |= ferror(file) != 0;
        failed |= fclose(file) != 0;
    }
    failed |= !usage || !commands || !options || modes;
    failed |= system(KC_TEST_CLI " --version > embdex-cli-info.txt 2>&1") != 0;
    file = fopen("embdex-cli-info.txt", "r");
    if (file == NULL) failed = 1;
    else {
        failed |= fscanf(file, "%llu %c", &version, &extra) != 1;
        failed |= fclose(file) != 0;
        failed |= version != (unsigned long long)kc_embdex_version();
    }
    failed |= system(
        KC_TEST_CLI " --help build > " KC_TEST_NULL " 2>&1"
    ) == 0;
    remove("embdex-cli-info.txt");
    return kc_test_result("cli_info",
        "follows kclib help and version conventions", failed);
}

/**
 * Build and search through the TSV command-line interface.
 * @return Zero when output contains the expected indexed association.
 */
static int kc_test_cli(void)
{
    FILE *file;
    char line[512];
    int failed = 0, found = 0;

    file = fopen("embdex-cli-input.tsv", "w");
    if (file == NULL) failed = 1;
    else {
        failed |= fputs(
            "shared\tauthentication for remote peers\n", file) == EOF;
        failed |= fputs(
            "other\tgardening and watering plants\n", file) == EOF;
        failed |= fclose(file) != 0;
    }
    if (!failed) failed |= system(
        KC_TEST_CLI " build embdex-cli-index.bin "
        "-i embdex-cli-input.tsv > " KC_TEST_NULL " 2>&1"
    ) != 0;
    if (!failed) failed |= system(
        KC_TEST_CLI " search embdex-cli-index.bin authentication "
        "--limit 2 > embdex-cli-output.tsv 2>&1"
    ) != 0;
    file = fopen("embdex-cli-output.tsv", "r");
    if (file == NULL) failed = 1;
    else {
        while (fgets(line, sizeof(line), file) != NULL)
            found |= strstr(line,
                "shared\t") == line &&
                strstr(line, "authentication for remote peers") != NULL;
        failed |= ferror(file) != 0;
        failed |= fclose(file) != 0;
    }
    failed |= !found;
    failed |= system(
        KC_TEST_CLI " search embdex-cli-index.bin authentication "
        "--mode hybrid > " KC_TEST_NULL " 2>&1"
    ) == 0;
    remove("embdex-cli-input.tsv");
    remove("embdex-cli-index.bin");
    remove("embdex-cli-output.tsv");
    return kc_test_result("cli",
        "builds and searches TSV with automatic strategy", failed);
}
#endif

/**
 * Run one named public contract test.
 * @param name Name of the case to execute.
 * @return Zero on pass, one on failure, two on unknown case.
 */
static int kc_test_run(const char *name)
{
    kc_test_current++;
    if (strcmp(name, "search") == 0) return kc_test_search();
    if (strcmp(name, "duplicates") == 0) return kc_test_duplicates();
    if (strcmp(name, "invalid") == 0) return kc_test_invalid();
    if (strcmp(name, "format") == 0) return kc_test_format();
    if (strcmp(name, "relevance") == 0) return kc_test_relevance();
    if (strcmp(name, "query") == 0) return kc_test_query();
#ifndef __EMSCRIPTEN__
    if (strcmp(name, "wordmap") == 0) return kc_test_wordmap();
    if (strcmp(name, "cli_info") == 0) return kc_test_cli_info();
    if (strcmp(name, "cli") == 0) return kc_test_cli();
#endif
    fprintf(stderr, "unknown test case: %s\n", name);
    return 2;
}

/**
 * Execute all tests or the requested named case.
 * @param argc Number of command-line arguments.
 * @param argv Test selector or all.
 * @return Zero on success, otherwise nonzero.
 */
int main(int argc, char **argv)
{
    const char *cases[] = {
        "search", "duplicates", "invalid", "format",
        "relevance", "query",
#ifndef __EMSCRIPTEN__
        "wordmap", "cli_info", "cli",
#endif
    };
    int failed = 0;

    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument\n");
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
            failed += kc_test_run(cases[i]);
        printf("%d passed, %d failed\n", KC_TEST_TOTAL - failed, failed);
        return failed == 0 ? 0 : 1;
    }
    return kc_test_run(argv[1]);
}
