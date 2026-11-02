/**
 * libroute.h - HTTP route selection.
 * Summary: Matches ordered method and path patterns against HTTP requests.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_ROUTE_H
#define KC_ROUTE_H

#include <stddef.h>
#include <stdint.h>
#include "libhttp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KC_ROUTE_OK 0
#define KC_ROUTE_EINVAL -1
#define KC_ROUTE_ENOMEM -2
#define KC_ROUTE_EPATTERN -3
#define KC_ROUTE_EURI -4
#define KC_ROUTE_CAPTURES 16

typedef struct kc_route kc_route_t;

typedef struct {
    const char *method;
    const char *path;
    const char *query;
    const char *id;
    const char *const *captures;
    size_t capture_count;
} kc_route_match_t;

/**
 * Process a matching route. All pointers remain borrowed until return.
 * Return zero to check the next route, or nonzero to accept the match.
 * @param match Borrowed method, canonical path, query, route ID, and captures.
 * @param userdata Callback context owned by the caller.
 * @return Zero to continue, nonzero to stop routing.
 */
typedef int (*kc_route_fn)(const kc_route_match_t *match, void *userdata);

/**
 * Create an ordered route registry.
 * @param out Receives the owned registry.
 * @return KC_ROUTE_OK or a negative status code.
 */
int kc_route_open(kc_route_t **out);

/**
 * Register an HTTP method and portable route path expression.
 * The pattern matches the complete canonical path. "ALL" accepts
 * any method. NULL handler accepts the first matching route.
 * @param route Registry receiving the entry.
 * @param method HTTP method or ALL, case-insensitive.
 * @param pattern Portable regular expression subset without delimiters.
 * @param id Destination ID copied into the registry.
 * @param handler Optional callback; zero continues to subsequent entries.
 * @param userdata Callback context, borrowed until registry destruction.
 * @return KC_ROUTE_OK or a negative status code.
 */
int kc_route_add(kc_route_t *route, const char *method, const char *pattern,
    const char *id, kc_route_fn handler, void *userdata);

/**
 * Route a method and origin-form HTTP request target.
 * Repeated and trailing slashes are normalized; query is not matched.
 * No HTTP response or redirect is produced by this operation.
 * @param route Registry to search.
 * @param method Request method.
 * @param target URI target beginning with a slash.
 * @param matched Receives whether a registration accepted the request.
 * @return KC_ROUTE_OK or a negative status code.
 */
int kc_route_run(const kc_route_t *route, const char *method,
    const char *target, int *matched);

/**
 * Route one parsed HTTP request through its method and target.
 * @param route Registry to search.
 * @param request Borrowed request supplied by libhttp.
 * @param matched Receives one when an entry accepts the request.
 * @return KC_ROUTE_OK or a negative status code.
 */
int kc_route_http(const kc_route_t *route, const kc_http_request_t *request,
    int *matched);

/**
 * Release an ordered registry and its compiled patterns.
 * @param route Registry or NULL.
 * @return None.
 */
void kc_route_close(kc_route_t *route);

/**
 * Describe a public route status.
 * @param status Route API status code.
 * @return Static description.
 */
const char *kc_route_strerror(int status);

/**
 * Return the generated build timestamp.
 * @return Build timestamp, or zero when absent.
 */
uint64_t kc_route_version(void);

#ifdef __cplusplus
}
#endif

#endif
