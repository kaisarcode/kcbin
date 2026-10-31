/**
 * embdex.c - Semantic Index Command-Line Interface
 * Summary: Builds and queries indexes from tab-separated text associations.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libembdex.h"

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
    printf("  build <index>           Build an index from TSV associations\n");
    printf("  search <index> <query>  Find relevant associations\n");
    printf("\n");
    printf("Options:\n");
    printf("  -i, --input <file>      Read associations from file (default: stdin)\n");
    printf("  -l, --limit <N>         Maximum results (default: 10)\n");
    printf("  -h, --help              Show this help\n");
    printf("  -v, --version           Show version\n");
}

/**
 * Parse a strictly positive decimal result limit.
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
        value == 0 || (unsigned long long)(size_t)value != value) {
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
 * Convert tab-separated input lines into borrowed document pairs.
 * @param buffer Mutable input buffer without embedded NUL characters.
 * @param out Receives the allocated array of borrowed references.
 * @param out_count Receives the number of parsed associations.
 * @return Zero on success, nonzero for malformed input or allocation failure.
 */
static int embdex_parse_records(
    char *buffer,
    kc_embdex_document_t **out,
    size_t *out_count
)
{
    size_t capacity = 0;
    size_t count = 0;
    kc_embdex_document_t *documents = NULL;
    char *cursor = buffer;

    *out = NULL;
    *out_count = 0;
    while (*cursor != '\0') {
        char *line = cursor;
        char *newline = strchr(line, '\n');
        char *tab;
        size_t length;

        if (newline != NULL) {
            *newline = '\0';
            cursor = newline + 1;
        } else {
            cursor += strlen(cursor);
        }
        length = strlen(line);
        if (length > 0 && line[length - 1] == '\r') {
            line[--length] = '\0';
        }
        if (length == 0) {
            continue;
        }
        tab = strchr(line, '\t');
        if (tab == NULL || tab == line ||
            tab[1] == '\0' || strchr(tab + 1, '\t') != NULL) {
            free(documents);
            return 1;
        }
        *tab = '\0';
        if (count == capacity) {
            size_t next_capacity = capacity == 0 ? 16 : capacity * 2;
            kc_embdex_document_t *next;

            if (next_capacity < capacity ||
                next_capacity > SIZE_MAX / sizeof(*documents)) {
                free(documents);
                return 1;
            }
            next = (kc_embdex_document_t *)realloc(
                documents, next_capacity * sizeof(*documents)
            );
            if (next == NULL) {
                free(documents);
                return 1;
            }
            documents = next;
            capacity = next_capacity;
        }
        documents[count].id = line;
        documents[count].content = tab + 1;
        count++;
    }
    *out = documents;
    *out_count = count;
    return 0;
}

/**
 * Create a binary index from a file or standard input.
 * @param path Destination binary index.
 * @param input Optional text input file path.
 * @return Process exit status.
 */
static int embdex_build(const char *path, const char *input)
{
    FILE *file = stdin;
    char *buffer = NULL;
    size_t buffer_size = 0;
    size_t count = 0;
    kc_embdex_document_t *documents = NULL;
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
    if (status != 0 || strlen(buffer) != buffer_size ||
        embdex_parse_records(buffer, &documents, &count) != 0) {
        fprintf(stderr, "embdex: invalid tab-separated input\n");
        goto finished;
    }
    status = kc_embdex_build(path, documents, count);
    if (status != KC_EMBDEX_OK) {
        fprintf(stderr, "embdex: %s\n", kc_embdex_strerror(status));
        goto finished;
    }
    result = 0;

finished:
    free(documents);
    free(buffer);
    return result;
}

/**
 * Print one borrowed search result in the CLI output format.
 * @param result Matching association.
 * @param userdata Unused caller context.
 * @return Zero on success, nonzero when output fails.
 */
static int embdex_print_result(
    const kc_embdex_result_t *result,
    void *userdata
)
{
    (void)userdata;
    return printf("%s\t%.12g\t%s\n",
        result->id, result->score, result->content) < 0;
}

/**
 * Search an index and print ordered associations.
 * @param path Saved index location.
 * @param query User query.
 * @param limit Maximum result count.
 * @return Process status.
 */
static int embdex_search(const char *path, const char *query, size_t limit)
{
    kc_embdex_t *index = NULL;
    kc_embdex_search_options_t options = { .limit = &limit };
    int status;

    status = kc_embdex_open(&index, path);
    if (status == KC_EMBDEX_OK)
        status = kc_embdex_search(index, query, &options,
            embdex_print_result, NULL);
    kc_embdex_close(index);
    if (status != KC_EMBDEX_OK) {
        fprintf(stderr, "embdex: %s\n", kc_embdex_strerror(status));
        return 1;
    }
    return fflush(stdout) == 0 ? 0 : 1;
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

        for (int i = 3; i < argc; i++) {
            if ((strcmp(argv[i], "-i") == 0 ||
                strcmp(argv[i], "--input") == 0) &&
                ++i < argc && input == NULL)
            {
                input = argv[i];
            } else {
                fprintf(stderr, "embdex: invalid build arguments\n");
                return 1;
            }
        }
        return embdex_build(argv[2], input);
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
