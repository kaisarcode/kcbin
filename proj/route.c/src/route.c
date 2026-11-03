/**
 * route.c - HTTP request dispatcher CLI.
 * Summary: Routes HTTP messages from stdin to configured handler commands.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libroute.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define route_popen _popen
#define route_pclose _pclose
#else
#define route_popen popen
#define route_pclose pclose
#endif

typedef struct {
    kc_route_t *route;
    int failed;
    size_t requests;
} route_state_t;

/**
 * Show usage and available options.
 * @return None.
 */
static void route_usage(void)
{
    printf("Usage: route -r <method> <pattern> <command> [-r ...] < request.http\n");
    printf("\n");
    printf("Options:\n");
    printf("  -r, --route <method> <pattern> <command>  Register a handler command\n");
    printf("  -h, --help                               Show this help\n");
    printf("  -v, --version                            Show version\n");
}

/**
 * Forward a complete HTTP request to the matched handler via stdin.
 * Handler stdout is inherited as the command's response stream.
 * @param match Borrowed route match with parsed request.
 * @param userdata Mutable CLI execution status.
 * @return One to accept the match.
 */
static int route_execute(const kc_route_match_t *match, void *userdata)
{
    route_state_t *state = (route_state_t *)userdata;
    FILE *process;
    kc_http_request_build_t request = {0};
    void *bytes = NULL;
    size_t size = 0U;
    int status;

    if (!match->request) {
        state->failed = 1;
        return 1;
    }
    request.version = match->request->version;
    request.method = match->request->method;
    request.target = match->request->target;
    request.headers = match->request->headers;
    request.header_count = match->request->header_count;
    request.body = match->request->body;
    request.body_size = match->request->body_size;
    request.trailers = match->request->trailers;
    request.trailer_count = match->request->trailer_count;
    request.chunked = match->request->chunked;
    status = kc_http_request(&request, &bytes, &size);
    if (status != KC_HTTP_OK) {
        fprintf(stderr, "route: cannot build HTTP request: %s\n",
            kc_http_strerror(status));
        state->failed = 1;
        return 1;
    }
    process = route_popen(match->id, "w");
    if (!process) {
        fprintf(stderr, "route: cannot run handler: %s\n", match->id);
        kc_http_free(bytes);
        state->failed = 1;
        return 1;
    }
    #ifdef _WIN32
    _setmode(_fileno(process), _O_BINARY);
    #endif
    if (fwrite(bytes, 1U, size, process) != size) {
        fprintf(stderr, "route: cannot write handler input\n");
        state->failed = 1;
    }
    kc_http_free(bytes);
    if (route_pclose(process) != 0) {
        fprintf(stderr, "route: handler failed: %s\n", match->id);
        state->failed = 1;
    }
    return 1;
}

/**
 * Handle one fully parsed HTTP request.
 * @param request Borrowed libhttp callback view.
 * @param userdata CLI state and registered routes.
 * @return None.
 */
static void route_request(const kc_http_request_t *request, void *userdata)
{
    route_state_t *state = (route_state_t *)userdata;
    int matched = 0;
    int status;

    if (state->failed) return;
    ++state->requests;
    status = kc_route_http(state->route, request, &matched);
    if (status != KC_ROUTE_OK) {
        fprintf(stderr, "route: %s\n", kc_route_strerror(status));
        state->failed = 1;
    } else if (!matched) {
        fprintf(stderr, "route: no matching route for %s %s\n",
            request->method, request->target);
        state->failed = 1;
    }
}

/**
 * Report errors from the incremental HTTP parser.
 * @param status libhttp error code.
 * @param userdata Mutable CLI state.
 * @return None.
 */
static void route_error(int status, void *userdata)
{
    route_state_t *state = (route_state_t *)userdata;

    fprintf(stderr, "route: invalid HTTP input: %s\n",
        kc_http_strerror(status));
    state->failed = 1;
}

/**
 * Parse stdin with libhttp and dispatch each complete request.
 * @param state Registered routes and error state.
 * @return Zero on successful dispatch; nonzero on error.
 */
static int route_stream(route_state_t *state)
{
    kc_http_parser_t *parser = NULL;
    unsigned char buffer[8192];
    size_t size;
    int status;

    #ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    #endif
    status = kc_http_parser_open(&parser, route_request, NULL,
        route_error, state);
    if (status != KC_HTTP_OK) {
        fprintf(stderr, "route: %s\n", kc_http_strerror(status));
        return 1;
    }
    while (!state->failed &&
        (size = fread(buffer, 1U, sizeof(buffer), stdin)) > 0U) {
        status = kc_http_parser_write(parser, buffer, size);
        if (status != KC_HTTP_OK) {
            if (!state->failed)
                fprintf(stderr, "route: %s\n", kc_http_strerror(status));
            state->failed = 1;
        }
    }
    if (ferror(stdin)) {
        fprintf(stderr, "route: cannot read stdin\n");
        state->failed = 1;
    }
    kc_http_parser_close(parser);
    if (state->requests == 0U) {
        fprintf(stderr, "route: no HTTP request received\n");
        state->failed = 1;
    }
    return state->failed ? 1 : 0;
}

/**
 * Register command handlers and process incoming HTTP data.
 * @param argc Argument count.
 * @param argv Command line.
 * @return Process exit status.
 */
int main(int argc, char **argv)
{
    route_state_t state = {0};
    int i, status, result;

    if (argc == 2 &&
        (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        route_usage();
        return 0;
    }
    if (argc == 2 &&
        (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version"))) {
        printf("%llu\n", (unsigned long long)kc_route_version());
        return 0;
    }
    if (argc < 5 || (argc - 1) % 4 != 0) {
        route_usage();
        return 1;
    }
    status = kc_route_open(&state.route);
    if (status != KC_ROUTE_OK) {
        fprintf(stderr, "route: %s\n", kc_route_strerror(status));
        return 1;
    }
    for (i = 1; i < argc; i += 4) {
        if (strcmp(argv[i], "-r") != 0 &&
            strcmp(argv[i], "--route") != 0) {
            fprintf(stderr, "route: expected -r <method> <pattern> <command>\n");
            kc_route_close(state.route);
            return 1;
        }
        status = kc_route_add(state.route, argv[i + 1], argv[i + 2],
            argv[i + 3], route_execute, &state);
        if (status != KC_ROUTE_OK) {
            fprintf(stderr, "route: %s\n", kc_route_strerror(status));
            kc_route_close(state.route);
            return 1;
        }
    }
    result = route_stream(&state);
    kc_route_close(state.route);
    return result;
}
