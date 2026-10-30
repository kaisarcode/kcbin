/**
 * semidx.c - Semantic Index Command-Line Interface
 * Summary: Builds and queries indexes from tab-separated text associations.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libsemidx.h"

#include <errno.h>
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
static void semidx_usage(const char *name)
{
    printf("Usage: %s build <index.bin> [-i file]\n", name);
    printf("       %s search <index.bin> <query> [-l N] [-t score]\n", name);
    printf("       %s -h | -v\n", name);
    printf("\n");
    printf("Build input: one id<TAB>content association per line.\n");
    printf("Without -i, build reads standard input.\n");
    printf("Search output: id<TAB>score<TAB>content per line.\n");
    printf("Options:\n");
    printf("  -i, --input <file>  Read associations from a file\n");
    printf("  -l, --limit <N>     Maximum results (default: 10)\n");
    printf("  -t, --threshold N  Minimum cosine similarity (default: -1)\n");
    printf("  -h, --help         Show this help\n");
    printf("  -v, --version      Show build version\n");
}

/**
 * Parse a strictly positive decimal result limit.
 * @param text Argument to parse.
 * @param out Receives the validated limit.
 * @return Zero on success, nonzero on invalid input.
 */
static int semidx_parse_limit(const char *text, size_t *out)
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
 * Parse a finite cosine similarity threshold.
 * @param text Argument to parse.
 * @param out Receives the cosine threshold.
 * @return Zero on success, nonzero on invalid input.
 */
static int semidx_parse_threshold(const char *text, double *out)
{
    double value;
    char *end;

    if (text == NULL || text[0] == '\0') {
        return 1;
    }
    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' ||
        !isfinite(value) || value < -1.0 || value > 1.0) {
        return 1;
    }
    *out = value;
    return 0;
}

/**
 * Read a whole text stream into dynamically allocated storage.
 * @param stream Open input stream.
 * @param out Receives the owned NUL-terminated data.
 * @param out_size Receives the exact byte length.
 * @return Zero on success, nonzero on I/O or allocation failure.
 */
static int semidx_read_stream(
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
static int semidx_parse_records(
    char *buffer,
    kc_semidx_document_t **out,
    size_t *out_count
)
{
    size_t capacity = 0;
    size_t count = 0;
    kc_semidx_document_t *documents = NULL;
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
            kc_semidx_document_t *next;

            if (next_capacity < capacity ||
                next_capacity > SIZE_MAX / sizeof(*documents)) {
                free(documents);
                return 1;
            }
            next = (kc_semidx_document_t *)realloc(
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
static int semidx_build(const char *path, const char *input)
{
    FILE *file = stdin;
    char *buffer = NULL;
    size_t buffer_size = 0;
    size_t count = 0;
    kc_semidx_document_t *documents = NULL;
    kc_semidx_t *index = NULL;
    int status;
    int result = 1;

    if (input != NULL) {
        file = fopen(input, "rb");
        if (file == NULL) {
            fprintf(stderr, "semidx: cannot open input file\n");
            return 1;
        }
    }
    status = semidx_read_stream(file, &buffer, &buffer_size);
    if (input != NULL) {
        fclose(file);
    }
    if (status != 0 || strlen(buffer) != buffer_size ||
        semidx_parse_records(buffer, &documents, &count) != 0) {
        fprintf(stderr, "semidx: invalid tab-separated input\n");
        goto finished;
    }
    status = kc_semidx_build(&index, documents, count);
    if (status == KC_SEMIDX_OK) {
        status = kc_semidx_save(index, path);
    }
    if (status != KC_SEMIDX_OK) {
        fprintf(stderr, "semidx: %s\n", kc_semidx_strerror(status));
        goto finished;
    }
    result = 0;

finished:
    kc_semidx_close(index);
    free(documents);
    free(buffer);
    return result;
}

/**
 * Query an existing binary index and print tab-separated matches.
 * @param path Source binary index.
 * @param query Natural-language query.
 * @param limit Maximum number of returned matches.
 * @param threshold Minimum cosine similarity score.
 * @return Process exit status.
 */
static int semidx_search(
    const char *path,
    const char *query,
    size_t limit,
    double threshold
)
{
    kc_semidx_t *index = NULL;
    kc_semidx_result_t *results = NULL;
    size_t count = 0;
    kc_semidx_search_options_t options = {
        .limit = &limit,
        .threshold = &threshold
    };
    int status;
    int result = 1;

    status = kc_semidx_open(&index, path);
    if (status == KC_SEMIDX_OK) {
        status = kc_semidx_search(
            index, query, &options, &results, &count
        );
    }
    if (status != KC_SEMIDX_OK) {
        fprintf(stderr, "semidx: %s\n", kc_semidx_strerror(status));
        goto finished;
    }
    for (size_t i = 0; i < count; i++) {
        if (printf("%s\t%.12g\t%s\n",
            results[i].id, results[i].score, results[i].content) < 0) {
            goto finished;
        }
    }
    result = fflush(stdout) == 0 ? 0 : 1;

finished:
    kc_semidx_free(results);
    kc_semidx_close(index);
    return result;
}

/**
 * Dispatch the semantic index CLI commands.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv)
{
    if (argc == 2 &&
        (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0)) {
        semidx_usage(argv[0]);
        return 0;
    }
    if (argc == 2 &&
        (strcmp(argv[1], "-v") == 0 ||
        strcmp(argv[1], "--version") == 0)) {
        printf("%" PRIu64 "\n", kc_semidx_version());
        return 0;
    }
    if (argc < 3) {
        semidx_usage(argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "build") == 0) {
        const char *input = NULL;

        for (int i = 3; i < argc; i++) {
            if ((strcmp(argv[i], "-i") == 0 ||
                strcmp(argv[i], "--input") == 0) &&
                ++i < argc && input == NULL) {
                input = argv[i];
            } else {
                fprintf(stderr, "semidx: invalid build arguments\n");
                return 1;
            }
        }
        return semidx_build(argv[2], input);
    }
    if (strcmp(argv[1], "search") == 0 && argc >= 4) {
        size_t limit = 10;
        double threshold = -1.0;

        for (int i = 4; i < argc; i++) {
            if ((strcmp(argv[i], "-l") == 0 ||
                strcmp(argv[i], "--limit") == 0) &&
                ++i < argc && semidx_parse_limit(argv[i], &limit) == 0) {
                continue;
            }
            if ((strcmp(argv[i], "-t") == 0 ||
                strcmp(argv[i], "--threshold") == 0) &&
                ++i < argc &&
                semidx_parse_threshold(argv[i], &threshold) == 0) {
                continue;
            }
            fprintf(stderr, "semidx: invalid search arguments\n");
            return 1;
        }
        return semidx_search(argv[2], argv[3], limit, threshold);
    }
    fprintf(stderr, "semidx: invalid command\n");
    return 1;
}
