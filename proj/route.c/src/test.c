/**
 * test.c - HTTP route public contract tests.
 * Summary: Tests ordered selection, URI matching, callbacks and CLI behavior.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libroute.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifdef _WIN32
#define KC_TEST_CLI ".\\route.exe"
#define KC_TEST_PATTERN "\"/notes/([0-9]+)\""
#define KC_TEST_HANDLER "\"route-test.exe --echo\""
#else
#define KC_TEST_CLI "./route"
#define KC_TEST_PATTERN "'/notes/([0-9]+)'"
#define KC_TEST_HANDLER "\"./route-test --echo\""
#endif

#ifdef __EMSCRIPTEN__
#define KC_TEST_TOTAL 11
#else
#define KC_TEST_TOTAL 13
#endif

static int kc_test_current;
static int kc_test_failed;

typedef struct {
    const char *expected_id;
    const char *expected_path;
    const char *expected_query;
    const char *expected_capture;
    size_t visits;
    int reject;
    int valid;
} kc_test_state_t;

/**
 * Report one numbered test case and update the aggregate failure count.
 * @param name Case name.
 * @param detail Contract being checked.
 * @param failed Nonzero when test failed.
 * @return None.
 */
static void kc_test_result(const char *name, const char *detail, int failed)
{
    ++kc_test_current;
    kc_test_failed += failed != 0;
    printf("[%d/%d] [%s] %s: %s\n", kc_test_current, KC_TEST_TOTAL,
        failed ? "FAIL" : "PASS", name, detail);
}

/**
 * Compare one borrowed route match and optionally continue routing.
 * @param match Borrowed route match.
 * @param userdata Mutable test expectations.
 * @return Zero when configured to continue, one to accept.
 */
static int kc_test_receive(const kc_route_match_t *match, void *userdata)
{
    kc_test_state_t *state = (kc_test_state_t *)userdata;

    state->visits++;
    if (strcmp(match->id, state->expected_id) != 0 ||
        strcmp(match->path, state->expected_path) != 0 ||
        (state->expected_query &&
            (!match->query ||
            strcmp(match->query, state->expected_query) != 0)) ||
        (state->expected_capture &&
            (match->capture_count == 0U ||
            strcmp(match->captures[0], state->expected_capture) != 0)))
        state->valid = 0;
    return state->reject ? 0 : 1;
}

/**
 * Verify method matching, path canonicalization, and query preservation.
 * @return Nonzero when any contract assertion fails.
 */
static int kc_test_basic(void)
{
    kc_route_t *route = NULL;
    kc_test_state_t state = {"home", "/", "x=1", NULL, 0U, 0, 1};
    int found = 0, failed = 0;

    failed |= kc_route_open(&route) != KC_ROUTE_OK;
    if (!failed) {
        failed |= kc_route_add(route, "get", "/", "home",
            kc_test_receive, &state) != KC_ROUTE_OK;
        failed |= kc_route_run(route, "GET", "///?x=1", &found) != KC_ROUTE_OK;
        failed |= found != 1 || !state.valid || state.visits != 1U;
    }
    kc_route_close(route);
    return failed;
}

/**
 * Verify regular-expression captures and path normalization.
 * @return Nonzero on assertion failure.
 */
static int kc_test_capture(void)
{
    kc_route_t *route = NULL;
    kc_test_state_t state = {"item", "/notes/42", NULL, "42", 0U, 0, 1};
    int found = 0, failed = 0;

    failed |= kc_route_open(&route) != KC_ROUTE_OK;
    if (!failed) {
        failed |= kc_route_add(route, "GET", "/notes/([0-9]+)", "item",
            kc_test_receive, &state) != KC_ROUTE_OK;
        failed |= kc_route_run(route, "GET", "//notes//42/",
            &found) != KC_ROUTE_OK;
        failed |= found != 1 || state.visits != 1U || !state.valid;
    }
    kc_route_close(route);
    return failed;
}

/**
 * Verify that a false callback yields the next matching route.
 * @return Nonzero on assertion failure.
 */
static int kc_test_fallback(void)
{
    kc_route_t *route = NULL;
    kc_test_state_t first = {"first", "/x", NULL, NULL, 0U, 1, 1};
    kc_test_state_t second = {"second", "/x", NULL, NULL, 0U, 0, 1};
    int found = 0, failed = 0;

    failed |= kc_route_open(&route) != KC_ROUTE_OK;
    if (!failed) {
        failed |= kc_route_add(route, "ALL", "/x", "first",
            kc_test_receive, &first) != KC_ROUTE_OK;
        failed |= kc_route_add(route, "GET", "/x", "second",
            kc_test_receive, &second) != KC_ROUTE_OK;
        failed |= kc_route_run(route, "GET", "/x", &found) != KC_ROUTE_OK;
        failed |= found != 1 || first.visits != 1U ||
            second.visits != 1U || !first.valid || !second.valid;
    }
    kc_route_close(route);
    return failed;
}

/**
 * Verify that a different method does not execute the matching route.
 * @return Nonzero on assertion failure.
 */
static int kc_test_method(void)
{
    kc_route_t *route = NULL;
    int found = 1, failed = 0;

    failed |= kc_route_open(&route) != KC_ROUTE_OK;
    if (!failed) {
        failed |= kc_route_add(route, "POST", "/x", "post",
            NULL, NULL) != KC_ROUTE_OK;
        failed |= kc_route_run(route, "GET", "/x", &found) != KC_ROUTE_OK;
        failed |= found != 0;
    }
    kc_route_close(route);
    return failed;
}

/**
 * Verify a parsed HTTP request remains available to the routed handler.
 * @param match Borrowed route and HTTP request.
 * @param userdata Expected request address.
 * @return One when the complete request is preserved.
 */
static int kc_test_http_handler(const kc_route_match_t *match, void *userdata)
{
    const kc_http_request_t *request = (const kc_http_request_t *)userdata;

    return match->request == request && match->request->body_size == 4U &&
        memcmp(match->request->body, "test", 4U) == 0 &&
        match->request->header_count == 1U;
}

/**
 * Verify a normalized path never loses a route's accepted identity.
 * @return Nonzero on assertion failure.
 */
static int kc_test_http(void)
{
    kc_route_t *route = NULL;
    kc_http_request_t request = {0};
    int found = 0, failed = 0;

    kc_http_field_t headers[] = {{"content-type", "text/plain"}};

    request.method = "GET";
    request.target = "/assets/app.js?x=1";
    request.headers = headers;
    request.header_count = 1U;
    request.body = "test";
    request.body_size = 4U;
    failed |= kc_route_open(&route) != KC_ROUTE_OK;
    if (!failed) {
        failed |= kc_route_add(route, "GET", "/assets/.*", "asset",
            kc_test_http_handler, &request) != KC_ROUTE_OK;
        failed |= kc_route_http(route, &request, &found) != KC_ROUTE_OK;
        failed |= found != 1;
    }
    kc_route_close(route);
    return failed;
}

/**
 * Verify invalid pattern rejection leaves a usable registry.
 * @return Nonzero on assertion failure.
 */
static int kc_test_pattern(void)
{
    kc_route_t *route = NULL;
    int found = 0, failed = 0;

    failed |= kc_route_open(&route) != KC_ROUTE_OK;
    if (!failed) {
        failed |= kc_route_add(route, "GET", "([", "bad",
            NULL, NULL) != KC_ROUTE_EPATTERN;
        failed |= kc_route_add(route, "GET", "/", "ok",
            NULL, NULL) != KC_ROUTE_OK;
        failed |= kc_route_run(route, "GET", "/", &found) != KC_ROUTE_OK;
        failed |= found != 1;
    }
    kc_route_close(route);
    return failed;
}

/**
 * Verify non-origin-form targets are rejected without matching.
 * @return Nonzero on assertion failure.
 */
static int kc_test_uri(void)
{
    kc_route_t *route = NULL;
    int found = 1, failed = 0;

    failed |= kc_route_open(&route) != KC_ROUTE_OK;
    if (!failed) {
        failed |= kc_route_run(route, "GET", "https://site/",
            &found) != KC_ROUTE_EURI;
        failed |= found != 0;
    }
    kc_route_close(route);
    return failed;
}

/**
 * Verify null-argument contracts and status descriptions.
 * @return Nonzero on assertion failure.
 */
static int kc_test_invalid(void)
{
    kc_route_t *route = NULL;
    int found = 0, failed = 0;

    failed |= kc_route_open(NULL) != KC_ROUTE_EINVAL;
    failed |= kc_route_open(&route) != KC_ROUTE_OK;
    failed |= kc_route_add(route, NULL, "/", "x",
        NULL, NULL) != KC_ROUTE_EINVAL;
    failed |= kc_route_run(NULL, "GET", "/", &found) != KC_ROUTE_EINVAL;
    failed |= kc_route_http(route, NULL, &found) != KC_ROUTE_EINVAL;
    failed |= !strstr(kc_route_strerror(KC_ROUTE_EURI), "URI");
    kc_route_close(route);
    kc_route_close(NULL);
    return failed;
}

#ifndef __EMSCRIPTEN__
/**
 * Exercise the released CLI through the shell.
 * @param command CLI invocation.
 * @return Process status.
 */
static int kc_test_command(const char *command)
{
    return system(command);
}

/**
 * Write an HTTP request fixture with a body.
 * @return Zero when the fixture was written.
 */
static int kc_test_input(void)
{
    const char wire[] = "POST /notes/42?view=full HTTP/1.1\r\n"
        "Host: localhost\r\nContent-Length: 4\r\n\r\ntest";
    FILE *file = fopen("route-test.http", "wb");
    int failed;

    if (!file) return 1;
    failed = fwrite(wire, 1U, sizeof(wire) - 1U, file) !=
        sizeof(wire) - 1U;
    failed |= fclose(file) != 0;
    return failed;
}

/**
 * Verify stdin HTTP parsing and execution of a matching handler.
 * @return Nonzero on assertion failure.
 */
static int kc_test_cli(void)
{
    FILE *output;
    char buffer[512] = {0};
    int failed = kc_test_input();

    if (failed) return 1;
    failed |= kc_test_command(KC_TEST_CLI
        " -r POST " KC_TEST_PATTERN " " KC_TEST_HANDLER
        " < route-test.http > route-test.out 2> route-test.err") != 0;
    output = fopen("route-test.out", "rb");
    if (!output) failed = 1;
    else {
        size_t count = fread(buffer, 1U, sizeof(buffer) - 1U, output);

        failed |= count == 0U;
        failed |= strstr(buffer, "POST /notes/42?view=full HTTP/1.1") == NULL;
        failed |= strstr(buffer, "test") == NULL;
        fclose(output);
    }
    remove("route-test.http");
    remove("route-test.out");
    remove("route-test.err");
    return failed;
}

/**
 * Verify unmatched, malformed and empty HTTP streams fail.
 * @return Nonzero on assertion failure.
 */
static int kc_test_cli_errors(void)
{
    int failed = kc_test_input();

    if (failed) return 1;
#ifdef _WIN32
    failed |= kc_test_command(KC_TEST_CLI
        " -r GET /notes/42 more < route-test.http > NUL 2>&1") == 0;
    failed |= kc_test_command(KC_TEST_CLI
        " -r POST /notes/42 more < NUL > NUL 2>&1") == 0;
    failed |= kc_test_command(KC_TEST_CLI
        " -r POST > NUL 2>&1") == 0;
#else
    failed |= kc_test_command(KC_TEST_CLI
        " -r GET /notes/42 cat < route-test.http > /dev/null 2>&1") == 0;
    failed |= kc_test_command(KC_TEST_CLI
        " -r POST /notes/42 cat < /dev/null > /dev/null 2>&1") == 0;
    failed |= kc_test_command(KC_TEST_CLI
        " -r POST > /dev/null 2>&1") == 0;
#endif
    remove("route-test.http");
    return failed;
}

#endif

/**
 * Verify portable alternation, classes and repeated groups.
 * @return Nonzero when a representative portable expression fails.
 */
static int kc_test_portable(void)
{
    kc_route_t *route = NULL;
    int found = 0, failed = 0;

    failed |= kc_route_open(&route) != KC_ROUTE_OK;
    if (!failed) {
        failed |= kc_route_add(route, "GET",
            "/(docs|pages)/([a-z]+-[0-9]+)", "item",
            NULL, NULL) != KC_ROUTE_OK;
        failed |= kc_route_run(route, "GET", "/pages/abc-28",
            &found) != KC_ROUTE_OK;
        failed |= found != 1;
        found = 0;
        failed |= kc_route_run(route, "GET", "/other/abc-28",
            &found) != KC_ROUTE_OK;
        failed |= found != 0;
    }
    kc_route_close(route);
    return failed;
}

/**
 * Verify an unmatched optional group is delivered as an empty capture.
 * @return Nonzero when optional captures or byte ranges are misinterpreted.
 */
static int kc_test_optional(void)
{
    kc_route_t *route = NULL;
    kc_test_state_t state = {"optional", "/notes/b", NULL, "",
        0U, 0, 1};
    int found = 0, failed = 0;

    failed |= kc_route_open(&route) != KC_ROUTE_OK;
    if (!failed) {
        failed |= kc_route_add(route, "GET", "/notes/(a)?b", "optional",
            kc_test_receive, &state) != KC_ROUTE_OK;
        failed |= kc_route_run(route, "GET", "/notes/b",
            &found) != KC_ROUTE_OK;
        failed |= found != 1 || !state.valid || state.visits != 1U;
    }
    kc_route_close(route);
    return failed;
}

/**
 * Verify regex evaluation-limit errors abort dispatch instead of falling back.
 * @return Nonzero when the matching limit is suppressed.
 */
static int kc_test_limit(void)
{
    kc_route_t *route = NULL;
    char target[262];
    int found = 0, failed = 0;

    target[0] = '/';
    memset(target + 1U, 'a', sizeof(target) - 2U);
    target[sizeof(target) - 1U] = '\0';
    failed |= kc_route_open(&route) != KC_ROUTE_OK;
    if (!failed) {
        failed |= kc_route_add(route, "GET", "/(a+)+b", "slow",
            NULL, NULL) != KC_ROUTE_OK;
        failed |= kc_route_add(route, "ALL", "/.*", "fallback",
            NULL, NULL) != KC_ROUTE_OK;
        failed |= kc_route_run(route, "GET", target,
            &found) != KC_ROUTE_ELIMIT;
        failed |= found != 0;
        failed |= strstr(kc_route_strerror(KC_ROUTE_ELIMIT), "limit") == NULL;
    }
    kc_route_close(route);
    return failed;
}

/**
 * Echo raw HTTP input as a deterministic executable handler.
 * @return Zero if every byte was forwarded.
 */
static int kc_test_echo(void)
{
    unsigned char bytes[8192];
    size_t count;

#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
        _setmode(_fileno(stdout), _O_BINARY) == -1)
        return 1;
#endif
    while ((count = fread(bytes, 1U, sizeof(bytes), stdin)) > 0U) {
        if (fwrite(bytes, 1U, count, stdout) != count) return 1;
    }
    return ferror(stdin) || fflush(stdout) != 0 ? 1 : 0;
}

/**
 * Execute the named contract cases or serve as an HTTP echo handler.
 * @param argc Process argument count.
 * @param argv Process argument vector.
 * @return Zero when every case passes or raw input was echoed.
 */
int main(int argc, char **argv)
{
#ifndef __EMSCRIPTEN__
    if (argc == 2 && strcmp(argv[1], "--echo") == 0)
        return kc_test_echo();
#else
    (void)argc;
    (void)argv;
#endif
    kc_test_result("basic", "matches normalized root and preserves query",
        kc_test_basic());
    kc_test_result("capture", "delivers regex captures from canonical URI",
        kc_test_capture());
    kc_test_result("fallback", "continues when a callback declines",
        kc_test_fallback());
    kc_test_result("method", "matches only the requested HTTP method",
        kc_test_method());
    kc_test_result("http", "accepts a borrowed libhttp request",
        kc_test_http());
    kc_test_result("pattern", "rejects malformed patterns without state damage",
        kc_test_pattern());
    kc_test_result("uri", "rejects unsupported request targets",
        kc_test_uri());
    kc_test_result("invalid", "validates input and handles empty cleanup",
        kc_test_invalid());
#ifndef __EMSCRIPTEN__
    kc_test_result("cli", "dispatches stdin HTTP request to a handler",
        kc_test_cli());
    kc_test_result("cli_errors", "rejects unmatched requests and invalid input",
        kc_test_cli_errors());
#endif
    kc_test_result("portable", "matches alternation, classes and repeats",
        kc_test_portable());
    kc_test_result("optional", "returns empty captures for unmatched groups",
        kc_test_optional());
    kc_test_result("limit", "propagates regex budget failures without fallback",
        kc_test_limit());
    printf("%d passed, %d failed\n",
        kc_test_current - kc_test_failed, kc_test_failed);
    return kc_test_failed ? 1 : 0;
}
