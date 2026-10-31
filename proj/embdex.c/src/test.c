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
#define KC_TEST_TOTAL 8
#else
#define KC_TEST_TOTAL 12
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
    const kc_embdex_document_t documents[] = {
        {"record", "one indexed association"}
    };
    const char *path = "embdex-test-corrupt.bin";
    kc_embdex_t *index = NULL;
    unsigned char header[104];
    FILE *file;
    int failed = 0;

    remove(path);
    failed |= kc_embdex_build(path, documents, 1) != KC_EMBDEX_OK;
    if (!failed) {
        file = fopen(path, "rb");
        if (file == NULL) failed = 1;
        else {
            failed |= fread(header, 1, sizeof(header), file) !=
                sizeof(header);
            failed |= fclose(file) != 0;
            if (!failed)
                failed |= header[8] != 1 || header[9] != 0 ||
                    header[10] != 0 || header[11] != 0;
        }
    }
    if (!failed) {
        failed |= kc_embdex_open(&index, path) != KC_EMBDEX_OK;
        kc_embdex_close(index);
        index = NULL;
    }
    if (!failed) {
        file = fopen(path, "r+b");
        if (file == NULL) failed = 1;
        else {
            failed |= fseek(file, 8, SEEK_SET) != 0;
            if (!failed) failed |= fputc(2, file) == EOF;
            failed |= fclose(file) != 0;
        }
    }
    if (!failed)
        failed |= kc_embdex_open(&index, path) != KC_EMBDEX_EFORMAT;
    failed |= index != NULL;
    kc_embdex_close(index);
    index = NULL;
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
        "writes V1 and rejects invalid or corrupt indexes", failed);
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

typedef struct {
    size_t count;
    size_t want, drink, water, phrase;
    double phrase_score, drink_score, water_score;
} kc_test_composition_t;

/**
 * Record phrase and single-word association positions.
 * @param result Borrowed ranked association.
 * @param userdata Observed positions and scores.
 * @return Zero to continue.
 */
static int kc_test_composition_receive(const kc_embdex_result_t *result,
    void *userdata)
{
    kc_test_composition_t *state = (kc_test_composition_t *)userdata;
    size_t position = ++state->count;

    if (strcmp(result->id, "want") == 0) state->want = position;
    if (strcmp(result->id, "drink") == 0) {
        state->drink = position;
        state->drink_score = result->score;
    }
    if (strcmp(result->id, "water") == 0) {
        state->water = position;
        state->water_score = result->score;
    }
    if (strcmp(result->id, "drink-water") == 0) {
        state->phrase = position;
        state->phrase_score = result->score;
    }
    return 0;
}

/**
 * Preserve both literal segmentations and prioritize the longer n-gram.
 * @return Zero when phrase coverage outranks separated single words.
 */
static int kc_test_composition(void)
{
    const kc_embdex_document_t documents[] = {
        {"want", "quiero"},
        {"drink", "tomar"},
        {"water", "agua"},
        {"drink-water", "tomar agua"}
    };
    const char *path = "embdex-test-composition.bin";
    kc_embdex_t *index = NULL;
    kc_test_composition_t state = {0};
    int failed = 0;

    remove(path);
    failed |= kc_embdex_build(path, documents, 4) != KC_EMBDEX_OK;
    if (!failed)
        failed |= kc_embdex_open(&index, path) != KC_EMBDEX_OK;
    if (!failed) {
        failed |= kc_embdex_search(index, "quiero tomar agua",
            NULL, kc_test_composition_receive, &state) != KC_EMBDEX_OK;
        failed |= state.count != 4 || state.want == 0 ||
            state.drink == 0 || state.water == 0 || state.phrase == 0;
        failed |= state.phrase >= state.drink ||
            state.phrase >= state.water;
        failed |= state.phrase_score <= state.drink_score ||
            state.phrase_score <= state.water_score;
    }
    kc_embdex_close(index);
    remove(path);
    return kc_test_result("composition",
        "ranks longer literal n-grams without discarding single words",
        failed);
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

typedef struct {
    size_t next;
    unsigned phase;
    int failed;
} kc_test_progress_t;

/**
 * Verify the order and counters of optional build progress notifications.
 * @param stage Current build phase.
 * @param completed Completed embedding count.
 * @param total Total embedding count.
 * @param userdata Mutable test observations.
 * @return None.
 */
static void kc_test_progress_receive(kc_embdex_build_stage_t stage,
    size_t completed, size_t total, void *userdata)
{
    kc_test_progress_t *state = (kc_test_progress_t *)userdata;

    if (stage == KC_EMBDEX_BUILD_PREPARE) {
        if (state->phase != 0 || completed != 0 || total != 0)
            state->failed = 1;
        state->phase = 1;
    } else if (stage == KC_EMBDEX_BUILD_EMBED) {
        if (state->phase != 1 || total != 3 ||
            completed != state->next)
            state->failed = 1;
        state->next++;
    } else if (stage == KC_EMBDEX_BUILD_SAVE) {
        if (state->phase != 1 || state->next != 4 ||
            completed != 0 || total != 0)
            state->failed = 1;
        state->phase = 2;
    } else if (stage == KC_EMBDEX_BUILD_DONE) {
        if (state->phase != 2 || completed != 0 || total != 0)
            state->failed = 1;
        state->phase = 3;
    } else {
        state->failed = 1;
    }
}

/**
 * Confirm that build progress counts completed embeddings and reports save.
 * @return Zero when the callback receives complete, ordered build progress.
 */
static int kc_test_progress(void)
{
    const kc_embdex_document_t documents[] = {
        {"one", "first association"},
        {"two", "second association"},
        {"three", "third association"}
    };
    const char *path = "embdex-test-progress.bin";
    kc_test_progress_t state = {0};
    kc_embdex_build_options_t options = {
        .progress = kc_test_progress_receive,
        .userdata = &state
    };
    kc_embdex_t *index = NULL;
    int failed;

    remove(path);
    failed = kc_embdex_build_with_dictionaries(path, documents, 3,
        NULL, 0, &options) != KC_EMBDEX_OK;
    failed |= state.failed || state.phase != 3 || state.next != 4;
    if (!failed)
        failed |= kc_embdex_open(&index, path) != KC_EMBDEX_OK;
    kc_embdex_close(index);
    remove(path);
    return kc_test_result("progress",
        "reports preparation, embedding counts, save and completion", failed);
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
    file = fopen(dictionary_a, "w");
    if (file == NULL) failed = 1;
    else {
        failed |= fputs("guardar\tstore\n", file) == EOF;
        failed |= fputs("contraseñas\tpasswords\n", file) == EOF;
        failed |= fclose(file) != 0;
    }
    file = fopen(dictionary_b, "w");
    if (file == NULL) failed = 1;
    else {
        failed |= fputs("claves\tpasswords\n", file) == EOF;
        failed |= fputs("claves\tkeys\n", file) == EOF;
        failed |= fputs("guardar\tstore\n", file) == EOF;
        failed |= fclose(file) != 0;
    }
    if (!failed)
        failed |= kc_embdex_build_with_dictionaries(path, documents, 2,
            dictionaries, 2, NULL) != KC_EMBDEX_OK;
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
    index = NULL;
    if (!failed) {
        file = fopen(path, "r+b");
        if (file == NULL) failed = 1;
        else {
            long offset = 24L + (long)strlen("bge-small-en-v1.5") + 1;

            failed |= fseek(file, offset, SEEK_SET) != 0;
            if (!failed) failed |= fputc('X', file) == EOF;
            failed |= fclose(file) != 0;
        }
        if (!failed)
            failed |= kc_embdex_open(&index, path) != KC_EMBDEX_EFORMAT;
        failed |= index != NULL;
        kc_embdex_close(index);
    }
    remove(path);
    return kc_test_result("wordmap",
        "merges dictionaries and validates embedded map identity",
        failed);
}

/**
 * Configure the diagnostic stream only during a test search.
 * @param path Trace file or NULL to disable tracing.
 * @return Zero when the environment was updated.
 */
static int kc_test_trace_env(const char *path)
{
#ifdef _WIN32
    return _putenv_s("KC_EMBDEX_TRACE", path == NULL ? "" : path);
#else
    return path == NULL ?
        unsetenv("KC_EMBDEX_TRACE") :
        setenv("KC_EMBDEX_TRACE", path, 1);
#endif
}

/**
 * Check how many distinct semantic embeddings the query evaluated.
 * @param path JSONL evidence trace for one search.
 * @param count Required embeddings for each association trace.
 * @return Zero when records exist and all report the requested count.
 */
static int kc_test_trace_embeddings(const char *path, size_t count)
{
    char expected[64], line[8192];
    FILE *file = fopen(path, "r");
    int found = 0, failed = 0;

    if (file == NULL) return 1;
    if (snprintf(expected, sizeof(expected),
        "\"variants_evaluated\":%zu,", count) < 0) {
        fclose(file);
        return 1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        found++;
        if (strstr(line, expected) == NULL) failed = 1;
    }
    failed |= ferror(file) != 0;
    failed |= fclose(file) != 0;
    return failed || found == 0;
}

/**
 * Confirm that complete literal matches bypass inference even for Top K,
 * and a partially unmatched query still enters the semantic fallback.
 * @return Zero when the fallback gate matches the expected behavior.
 */
static int kc_test_semantic_fallback(void)
{
    const kc_embdex_document_t documents[] = {
        {"want", "quiero"},
        {"drink", "tomar"},
        {"water", "agua"},
        {"drink-water", "tomar agua"}
    };
    const char *index_path = "embdex-test-fallback.bin";
    const char *trace_path = "embdex-test-fallback.jsonl";
    kc_embdex_t *index = NULL;
    kc_test_composition_t results = {0};
    size_t limit = 1;
    kc_embdex_search_options_t options = { .limit = &limit };
    int failed = 0;

    remove(index_path);
    remove(trace_path);
    failed |= kc_embdex_build(index_path, documents, 4) != KC_EMBDEX_OK;
    if (!failed)
        failed |= kc_embdex_open(&index, index_path) != KC_EMBDEX_OK;
    if (!failed) {
        failed |= kc_test_trace_env(trace_path) != 0;
        for (size_t run = 0; run < 2 && !failed; run++) {
            limit = run == 0 ? 1 : 10;
            failed |= kc_embdex_search(index, "quiero tomar agua",
                &options, kc_test_composition_receive,
                &results) != KC_EMBDEX_OK;
            failed |= kc_test_trace_embeddings(trace_path, 0);
            remove(trace_path);
            results = (kc_test_composition_t){0};
            if (failed) break;
            failed |= kc_embdex_search(index, "tomar", &options,
                kc_test_composition_receive, &results) != KC_EMBDEX_OK;
            failed |= kc_test_trace_embeddings(trace_path, 0);
            remove(trace_path);
            results = (kc_test_composition_t){0};
            if (failed) break;
            failed |= kc_embdex_search(index, "quiero tomar desconocido",
                &options, kc_test_composition_receive,
                &results) != KC_EMBDEX_OK;
            failed |= kc_test_trace_embeddings(trace_path, 1);
            remove(trace_path);
            results = (kc_test_composition_t){0};
        }
    }
    failed |= kc_test_trace_env(NULL) != 0;
    kc_embdex_close(index);
    remove(index_path);
    remove(trace_path);
    return kc_test_result("semantic_fallback",
        "runs NLP only when literal coverage is insufficient", failed);
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
    int failed = 0, usage = 0, commands = 0, options = 0;
    int progress = 0, modes = 0;

    failed |= system(KC_TEST_CLI " --help > embdex-cli-info.txt 2>&1") != 0;
    file = fopen("embdex-cli-info.txt", "r");
    if (file == NULL) failed = 1;
    else {
        while (fgets(line, sizeof(line), file) != NULL) {
            usage |= strstr(line, "Usage: ") != NULL;
            commands |= strstr(line, "Commands:") != NULL;
            options |= strstr(line, "Options:") != NULL;
            progress |= strstr(line, "--progress") != NULL;
            modes |= strstr(line, "--mode") != NULL;
        }
        failed |= ferror(file) != 0;
        failed |= fclose(file) != 0;
    }
    failed |= !usage || !commands || !options || !progress || modes;
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
        "-i embdex-cli-input.tsv > embdex-cli-silent.txt 2>&1"
    ) != 0;
    if (!failed) {
        file = fopen("embdex-cli-silent.txt", "r");
        if (file == NULL) failed = 1;
        else {
            failed |= fgetc(file) != EOF || ferror(file) != 0;
            failed |= fclose(file) != 0;
        }
    }
    remove("embdex-cli-silent.txt");
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
    file = fopen("embdex-cli-dict-a.tsv", "w");
    if (file == NULL) failed = 1;
    else {
        failed |= fputs("authentication\tverification\n", file) == EOF;
        failed |= fclose(file) != 0;
    }
    file = fopen("embdex-cli-dict-b.tsv", "w");
    if (file == NULL) failed = 1;
    else {
        failed |= fputs("remote\tdistant\n", file) == EOF;
        failed |= fclose(file) != 0;
    }
    if (!failed) failed |= system(
        KC_TEST_CLI " build embdex-cli-index.bin "
        "-i embdex-cli-input.tsv -d embdex-cli-dict-a.tsv "
        "--dictionary embdex-cli-dict-b.tsv --progress "
        "> " KC_TEST_NULL " 2> embdex-cli-progress.txt"
    ) != 0;
    if (!failed) {
        int prepare = 0, embed_start = 0, embed_middle = 0;
        int embed_end = 0, save = 0, done = 0;

        file = fopen("embdex-cli-progress.txt", "r");
        if (file == NULL) failed = 1;
        else {
            while (fgets(line, sizeof(line), file) != NULL) {
                prepare |= strcmp(line, "embdex: preparing\n") == 0;
                embed_start |= strstr(line,
                    "\rembdex: embeddings 0% (0/2)") != NULL;
                embed_middle |= strstr(line,
                    "\rembdex: embeddings 50% (1/2)") != NULL;
                embed_end |= strstr(line,
                    "\rembdex: embeddings 100% (2/2)") != NULL;
                save |= strcmp(line, "embdex: saving\n") == 0;
                done |= strcmp(line, "embdex: done\n") == 0;
            }
            failed |= ferror(file) != 0;
            failed |= fclose(file) != 0;
            failed |= !prepare || !embed_start || !embed_middle ||
                !embed_end || !save || !done;
        }
    }
    remove("embdex-cli-progress.txt");
    remove("embdex-cli-dict-a.tsv");
    remove("embdex-cli-dict-b.tsv");
    if (!failed) failed |= system(
        KC_TEST_CLI " search embdex-cli-index.bin "
        "\"verification distant peers\" --limit 2 "
        "> embdex-cli-output.tsv 2>&1"
    ) != 0;
    found = 0;
    file = fopen("embdex-cli-output.tsv", "r");
    if (file == NULL) failed = 1;
    else {
        while (fgets(line, sizeof(line), file) != NULL)
            found |= strstr(line, "shared\t") == line;
        failed |= ferror(file) != 0;
        failed |= fclose(file) != 0;
    }
    failed |= !found;
    remove("embdex-cli-input.tsv");
    remove("embdex-cli-index.bin");
    remove("embdex-cli-output.tsv");
    return kc_test_result("cli",
        "builds, merges dictionary TSVs and searches compiled maps", failed);
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
    if (strcmp(name, "composition") == 0) return kc_test_composition();
    if (strcmp(name, "query") == 0) return kc_test_query();
    if (strcmp(name, "progress") == 0) return kc_test_progress();
#ifndef __EMSCRIPTEN__
    if (strcmp(name, "wordmap") == 0) return kc_test_wordmap();
    if (strcmp(name, "semantic_fallback") == 0)
        return kc_test_semantic_fallback();
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
        "relevance", "composition", "query", "progress",
#ifndef __EMSCRIPTEN__
        "wordmap", "semantic_fallback", "cli_info", "cli",
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
