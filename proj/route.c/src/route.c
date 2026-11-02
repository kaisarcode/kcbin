/**
 * route.c - HTTP route selector CLI.
 * Summary: Resolves request methods and URI targets into route IDs.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libroute.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Print command syntax and supported options.
 * @return None.
 */
static void route_usage(void)
{
    printf("Usage: route match <method> <target> -r <method> <pattern> <id> [-r ...]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  match  Select the first accepted route and print its JSON result\n");
    printf("\n");
    printf("Options:\n");
    printf("  -r, --route <method> <pattern> <id>  Register an ordered route\n");
    printf("  -h, --help                          Show this help\n");
    printf("  -v, --version                       Show version\n");
}

/**
 * Quote a string as one JSON value with escaped control bytes.
 * @param text Null-terminated string to emit.
 * @return None.
 */
static void route_json_string(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    putchar('"');
    for (; *p; ++p) {
        if (*p == '"' || *p == '\\') {
            putchar('\\');
            putchar(*p);
        } else if (*p < 32U) {
            printf("\\u%04x", (unsigned)*p);
        } else {
            putchar(*p);
        }
    }
    putchar('"');
}

/**
 * Emit one selected route with its canonical path, query and captures.
 * @param match Borrowed route result.
 * @param userdata Unused callback context.
 * @return Nonzero to stop at this route.
 */
static int route_print(const kc_route_match_t *match, void *userdata)
{
    size_t i;

    (void)userdata;
    fputs("{\"id\":", stdout);
    route_json_string(match->id);
    fputs(",\"method\":", stdout);
    route_json_string(match->method);
    fputs(",\"path\":", stdout);
    route_json_string(match->path);
    fputs(",\"query\":", stdout);
    if (match->query) route_json_string(match->query);
    else fputs("null", stdout);
    fputs(",\"captures\":[", stdout);
    for (i = 0U; i < match->capture_count; ++i) {
        if (i) putchar(',');
        route_json_string(match->captures[i]);
    }
    fputs("]}\n", stdout);
    return 1;
}

/**
 * Resolve a request using route registrations supplied as CLI arguments.
 * @param argc Full process argument count.
 * @param argv Full process argument vector.
 * @return Zero for valid requests, nonzero on invalid input or API failure.
 */
static int route_match(int argc, char **argv)
{
    kc_route_t *route = NULL;
    int i, status, matched = 0;

    if (argc < 8) {
        route_usage();
        return 1;
    }
    status = kc_route_open(&route);
    if (status != KC_ROUTE_OK) goto failed;
    for (i = 4; i < argc; i += 4) {
        if (argc - i < 4 ||
            (strcmp(argv[i], "-r") != 0 &&
             strcmp(argv[i], "--route") != 0)) {
            fprintf(stderr, "route: expected -r <method> <pattern> <id>\n");
            kc_route_close(route);
            return 1;
        }
        status = kc_route_add(route, argv[i + 1], argv[i + 2],
            argv[i + 3], route_print, NULL);
        if (status != KC_ROUTE_OK) goto failed;
    }
    status = kc_route_run(route, argv[2], argv[3], &matched);
    if (status != KC_ROUTE_OK) goto failed;
    if (!matched) fputs("null\n", stdout);
    kc_route_close(route);
    return 0;

failed:
    fprintf(stderr, "route: %s\n", kc_route_strerror(status));
    kc_route_close(route);
    return 1;
}

/**
 * Dispatch command-line requests.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv)
{
    if (argc == 2 && (!strcmp(argv[1], "-h") ||
        !strcmp(argv[1], "--help"))) {
        route_usage();
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "-v") ||
        !strcmp(argv[1], "--version"))) {
        printf("%llu\n", (unsigned long long)kc_route_version());
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "match"))
        return route_match(argc, argv);
    route_usage();
    return 1;
}
