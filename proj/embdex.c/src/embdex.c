/**
 * embdex.c - Semantic Index Command-Line Interface
 * Summary: Builds and queries indexes using JSON text associations.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libembdex.h"
#include "parson.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Print the supported commands and options.
 * @param name Executable name.
 * @return void
 */
static void embdex_usage(const char *name)
{
    printf("Usage: %s <command> <index> [options]\n", name);
    printf("\n");
    printf("Commands:\n");
    printf("  build <index>           Build an index from JSON associations\n");
    printf("  search <index> <query>  Find relevant associations\n");
    printf("\n");
    printf("Options:\n");
    printf("  -i, --input <file>      Read associations from file (default: stdin)\n");
    printf("  -d, --dictionary <file> Add word substitutions (repeatable, build only)\n");
    printf("  -p, --progress          Show build progress on stderr\n");
    printf("  -l, --limit <N>         Maximum results (default: 10)\n");
    printf("  -h, --help              Show this help\n");
    printf("  -v, --version           Show version\n");
}

/**
 * Parse a nonnegative decimal result limit.
 * @param text Argument to parse.
 * @param out Receives the validated limit.
 * @return Zero on success, nonzero on invalid input.
 */
static int embdex_parse_limit(const char *text, size_t *out)
{
    unsigned long long value;
    char *end;

    if (text == NULL || text[0] == '\0' ||
        text[0] == '-' || text[0] == '+') {
        return 1;
    }
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        (unsigned long long)(size_t)value != value) {
        return 1;
    }
    *out = (size_t)value;
    return 0;
}

/**
 * Read a whole text stream into dynamically allocated storage.
 * @param stream Open input stream.
 * @param out Receives the owned NUL-terminated data.
 * @param out_size Receives the exact byte length.
 * @return Zero on success, nonzero on I/O or allocation failure.
 */
static int embdex_read_stream(
    FILE *stream,
    char **out,
    size_t *out_size
)
{
    size_t size = 0;
    size_t capacity = 0;
    char *buffer = NULL;
    char chunk[4096];
    size_t count;

    *out = NULL;
    *out_size = 0;
    while ((count = fread(chunk, 1, sizeof(chunk), stream)) > 0) {
        size_t required;
        char *next;

        if (count >= SIZE_MAX - size) {
            free(buffer);
            return 1;
        }
        required = size + count + 1;
        if (required > capacity) {
            size_t next_capacity = capacity == 0 ? 4096 : capacity;

            while (next_capacity < required) {
                if (next_capacity > SIZE_MAX / 2) {
                    free(buffer);
                    return 1;
                }
                next_capacity *= 2;
            }
            next = (char *)realloc(buffer, next_capacity);
            if (next == NULL) {
                free(buffer);
                return 1;
            }
            buffer = next;
            capacity = next_capacity;
        }
        memcpy(buffer + size, chunk, count);
        size += count;
    }
    if (ferror(stream)) {
        free(buffer);
        return 1;
    }
    if (buffer == NULL) {
        buffer = (char *)malloc(1);
        if (buffer == NULL) {
            return 1;
        }
    }
    buffer[size] = '\0';
    *out = buffer;
    *out_size = size;
    return 0;
}

/**
 * Parse a JSON array of ID/text associations into borrowed document pairs.
 * The returned references remain valid until the JSON root is freed.
 * @param buffer NUL-terminated JSON document.
 * @param out Receives the allocated array of borrowed references.
 * @param out_count Receives the number of associations.
 * @param root Receives the owned JSON tree.
 * @return Zero on success, nonzero for malformed input or allocation failure.
 */
static int embdex_parse_records(const char *buffer,
    kc_embdex_document_t **out, size_t *out_count, JSON_Value **root)
{
    JSON_Array *array;
    kc_embdex_document_t *documents;
    size_t count;

    *root = json_parse_string(buffer);
    *out = NULL;
    *out_count = 0;
    if (*root == NULL) return 1;
    array = json_value_get_array(*root);
    if (array == NULL) return 1;
    count = json_array_get_count(array);
    if (count > SIZE_MAX / sizeof(*documents)) return 1;
    documents = count == 0 ? NULL :
        (kc_embdex_document_t *)calloc(count, sizeof(*documents));
    if (count != 0 && documents == NULL) return 1;

    for (size_t i = 0; i < count; i++) {
        JSON_Object *entry = json_array_get_object(array, i);
        JSON_Value *id_value;
        JSON_Value *text_value;
        const char *id;
        const char *text;

        if (entry == NULL) {
            free(documents);
            return 1;
        }
        id_value = json_object_get_value(entry, "id");
        text_value = json_object_get_value(entry, "text");
        if (id_value == NULL || text_value == NULL ||
            json_value_get_type(id_value) != JSONString ||
            json_value_get_type(text_value) != JSONString) {
            free(documents);
            return 1;
        }
        id = json_value_get_string(id_value);
        text = json_value_get_string(text_value);
        if (id[0] == '\0' || text[0] == '\0') {
            free(documents);
            return 1;
        }
        documents[i].id = id;
        documents[i].content = text;
    }
    *out = documents;
    *out_count = count;
    return 0;
}

typedef struct {
    unsigned last;
    int active;
} embdex_progress_t;

/**
 * Report build stages and each percent of embedding work on stderr.
 * @param stage Current library build stage.
 * @param completed Number of associations embedded in this stage.
 * @param total Total associations to embed.
 * @param userdata CLI progress state.
 * @return None.
 */
static void embdex_progress(kc_embdex_build_stage_t stage,
    size_t completed, size_t total, void *userdata)
{
    embdex_progress_t *state = (embdex_progress_t *)userdata;

    switch (stage) {
    case KC_EMBDEX_BUILD_PREPARE:
        fprintf(stderr, "embdex: preparing\n");
        break;
    case KC_EMBDEX_BUILD_EMBED:
        if (total == 0) {
            if (state->last != 0) {
                fprintf(stderr, "\rembdex: embeddings 0/0");
                state->last = 0;
                state->active = 1;
                fflush(stderr);
            }
        } else {
            unsigned percent = (unsigned)((double)completed * 100.0 /
                (double)total);

            if (percent != state->last) {
                fprintf(stderr, "\rembdex: embeddings %u%% (%zu/%zu)",
                    percent, completed, total);
                state->last = percent;
                state->active = 1;
                fflush(stderr);
            }
        }
        break;
    case KC_EMBDEX_BUILD_SAVE:
        if (state->active) {
            fputc('\n', stderr);
            state->active = 0;
        }
        fprintf(stderr, "embdex: saving\n");
        break;
    case KC_EMBDEX_BUILD_DONE:
        fprintf(stderr, "embdex: done\n");
        break;
    }
}

/**
 * Create a binary index from a file or standard input.
 * @param path Destination binary index.
 * @param input Optional text input file path.
 * @param dictionaries Ordered dictionary file paths.
 * @param dictionary_count Number of dictionaries.
 * @param progress Nonzero to report build progress on stderr.
 * @return Process exit status.
 */
static int embdex_build(const char *path, const char *input,
    const char *const *dictionaries, size_t dictionary_count, int progress)
{
    FILE *file = stdin;
    char *buffer = NULL;
    size_t buffer_size = 0;
    size_t count = 0;
    kc_embdex_document_t *documents = NULL;
    JSON_Value *root = NULL;
    embdex_progress_t state = { .last = 101 };
    kc_embdex_build_options_t options = {
        .progress = embdex_progress,
        .userdata = &state
    };
    int status;
    int result = 1;

    if (input != NULL) {
        file = fopen(input, "rb");
        if (file == NULL) {
            fprintf(stderr, "embdex: cannot open input file\n");
            return 1;
        }
    }
    status = embdex_read_stream(file, &buffer, &buffer_size);
    if (input != NULL) {
        fclose(file);
    }
    if (status != 0 || buffer == NULL || strlen(buffer) != buffer_size ||
        embdex_parse_records(buffer, &documents, &count, &root) != 0) {
        fprintf(stderr, "embdex: invalid JSON input\n");
        goto finished;
    }
    status = kc_embdex_build_with_dictionaries(path, documents, count,
        dictionaries, dictionary_count, progress ? &options : NULL);
    if (status != KC_EMBDEX_OK) {
        if (state.active) fputc('\n', stderr);
        fprintf(stderr, "embdex: %s\n", kc_embdex_strerror(status));
        goto finished;
    }
    result = 0;

finished:
    free(documents);
    json_value_free(root);
    free(buffer);
    return result;
}

/**
 * Append one borrowed search result to a JSON array.
 * @param result Ranked association.
 * @param userdata Destination JSON array.
 * @return Zero on success, nonzero on allocation failure.
 */
static int embdex_print_result(const kc_embdex_result_t *result,
    void *userdata)
{
    JSON_Array *results = (JSON_Array *)userdata;
    JSON_Value *entry = json_value_init_object();
    JSON_Object *object;

    if (entry == NULL) return 1;
    object = json_value_get_object(entry);
    if (json_object_set_string(object, "id", result->id) != JSONSuccess ||
        json_object_set_number(object, "score", result->score) != JSONSuccess ||
        json_object_set_string(object, "text", result->content) != JSONSuccess ||
        json_array_append_value(results, entry) != JSONSuccess) {
        json_value_free(entry);
        return 1;
    }
    return 0;
}

/**
 * Search an index and print ordered associations as one JSON array.
 * @param path Saved index location.
 * @param query User query.
 * @param limit Maximum result count.
 * @return Process status.
 */
static int embdex_search(const char *path, const char *query, size_t limit)
{
    kc_embdex_t *index = NULL;
    kc_embdex_search_options_t options = { .limit = &limit };
    JSON_Value *root = json_value_init_array();
    JSON_Array *results;
    char *output;
    int status;

    if (root == NULL) return 1;
    results = json_value_get_array(root);
    status = kc_embdex_open(&index, path);
    if (status == KC_EMBDEX_OK)
        status = kc_embdex_search(index, query, &options,
            embdex_print_result, results);
    kc_embdex_close(index);
    if (status != KC_EMBDEX_OK) {
        fprintf(stderr, "embdex: %s\\n", kc_embdex_strerror(status));
        json_value_free(root);
        return 1;
    }
    output = json_serialize_to_string(root);
    if (output == NULL) {
        json_value_free(root);
        return 1;
    }
    status = puts(output) == EOF || fflush(stdout) != 0;
    json_free_serialized_string(output);
    json_value_free(root);
    return status ? 1 : 0;
}

/**
 * Dispatch the CLI operations on persistent indexes.
 * @param argc Argument count.
 * @param argv Command-line arguments.
 * @return Process exit status.
 */
int main(int argc, char **argv)
{
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0))
    {
        embdex_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-v") == 0 ||
        strcmp(argv[1], "--version") == 0))
    {
        printf("%" PRIu64 "\n", kc_embdex_version());
        return 0;
    }
    if (argc < 3) {
        embdex_usage(argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "build") == 0) {
        const char *input = NULL;
        const char **dictionaries = NULL;
        size_t dictionary_count = 0;
        int progress = 0;
        int result;

        if (argc > 3) {
            dictionaries = (const char **)calloc((size_t)(argc - 3),
                sizeof(*dictionaries));
            if (dictionaries == NULL) {
                fprintf(stderr, "embdex: out of memory\n");
                return 1;
            }
        }
        for (int i = 3; i < argc; i++) {
            if ((strcmp(argv[i], "-i") == 0 ||
                strcmp(argv[i], "--input") == 0) &&
                i + 1 < argc && input == NULL)
            {
                input = argv[++i];
            } else if ((strcmp(argv[i], "-d") == 0 ||
                strcmp(argv[i], "--dictionary") == 0) &&
                i + 1 < argc)
            {
                dictionaries[dictionary_count++] = argv[++i];
            } else if ((strcmp(argv[i], "-p") == 0 ||
                strcmp(argv[i], "--progress") == 0) && !progress)
            {
                progress = 1;
            } else {
                fprintf(stderr, "embdex: invalid build arguments\n");
                free(dictionaries);
                return 1;
            }
        }
        result = embdex_build(argv[2], input,
            dictionaries, dictionary_count, progress);
        free(dictionaries);
        return result;
    }
    if (strcmp(argv[1], "search") == 0 && argc >= 4) {
        size_t limit = 10;

        for (int i = 4; i < argc; i++) {
            if ((strcmp(argv[i], "-l") == 0 ||
                strcmp(argv[i], "--limit") == 0) &&
                ++i < argc && embdex_parse_limit(argv[i], &limit) == 0)
                continue;
            fprintf(stderr, "embdex: invalid search arguments\n");
            return 1;
        }
        return embdex_search(argv[2], argv[3], limit);
    }
    fprintf(stderr, "embdex: invalid command\n");
    return 1;
}
