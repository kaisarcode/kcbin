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
#define KC_TEST_CLI ".\\route.exe"
#else
#define KC_TEST_CLI "./route"
#endif

#define KC_TEST_TOTAL 10

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
 * Verify a normalized path never loses a route's accepted identity.
 * @return Nonzero on assertion failure.
 */
static int kc_test_http(void)
{
    kc_route_t *route = NULL;
    kc_http_request_t request = {0};
    int found = 0, failed = 0;

    request.method = "GET";
    request.target = "/assets/app.js?x=1";
    failed |= kc_route_open(&route) != KC_ROUTE_OK;
    if (!failed) {
        failed |= kc_route_add(route, "GET", "/assets/.*", "asset",
            NULL, NULL) != KC_ROUTE_OK;
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
 * Verify CLI JSON output with captures and method.
 * @return Nonzero on assertion failure.
 */
static int kc_test_cli(void)
{
    FILE *input;
    char buffer[512] = {0};
    int failed;

    failed = kc_test_command(KC_TEST_CLI
        " match GET /notes/42 -r GET '/notes/([0-9]+)' item"
        " > route-test.json") != 0;
    input = fopen("route-test.json", "rb");
    if (!input) return 1;
    failed |= fgets(buffer, sizeof(buffer), input) == NULL;
    failed |= strstr(buffer, "\"id\":\"item\"") == NULL;
    failed |= strstr(buffer, "\"captures\":[\"42\"]") == NULL;
    fclose(input);
    remove("route-test.json");
    return failed;
}

/**
 * Verify malformed CLI invocations fail and unmatched requests emit null.
 * @return Nonzero on assertion failure.
 */
static int kc_test_cli_errors(void)
{
    FILE *input;
    char buffer[32] = {0};
    int failed;

    failed = kc_test_command(KC_TEST_CLI
        " match GET /not-found -r GET /found found"
        " > route-test.json") != 0;
    input = fopen("route-test.json", "rb");
    if (!input) return 1;
    failed |= fgets(buffer, sizeof(buffer), input) == NULL;
    failed |= strcmp(buffer, "null\n") != 0;
    fclose(input);
    remove("route-test.json");
#ifdef _WIN32
    failed |= kc_test_command(KC_TEST_CLI " match GET / -r GET > NUL 2>&1") == 0;
#else
    failed |= kc_test_command(KC_TEST_CLI " match GET / -r GET > /dev/null 2>&1") == 0;
#endif
    return failed;
}

/**
 * Execute the named contract cases.
 * @return Zero when every case passes.
 */
int main(void)
{
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
    kc_test_result("cli", "prints selected route and captures as JSON",
        kc_test_cli());
    kc_test_result("cli_errors", "reports null matches and rejects bad syntax",
        kc_test_cli_errors());
    printf("%d passed, %d failed\n",
        kc_test_current - kc_test_failed, kc_test_failed);
    return kc_test_failed ? 1 : 0;
}
