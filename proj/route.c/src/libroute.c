/**
 * libroute.c - HTTP route selection.
 * Summary: Matches ordered method and URI patterns using POSIX regex.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libroute.h"

#include <ctype.h>
#include <regex.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_ROUTE_BUILD_VERSION
#define KC_ROUTE_BUILD_VERSION 0
#endif

typedef struct kc_route_entry {
    char *method;
    char *id;
    regex_t regex;
    kc_route_fn handler;
    void *userdata;
    struct kc_route_entry *next;
} kc_route_entry_t;

struct kc_route {
    kc_route_entry_t *first;
    kc_route_entry_t *last;
};

/**
 * Copy a string into owned memory.
 * @param text Source string.
 * @return Allocated copy or NULL.
 */
static char *kc_route_copy(const char *text)
{
    size_t size = strlen(text) + 1U;
    char *copy = (char *)malloc(size);

    if (copy) memcpy(copy, text, size);
    return copy;
}

/**
 * Split a request target into an owned canonical path and optional query.
 * @param target Borrowed HTTP origin-form target.
 * @param path Receives owned path.
 * @param query Receives owned query or NULL.
 * @return Route status code.
 */
static int kc_route_target(const char *target, char **path, char **query)
{
    const char *mark;
    size_t i, length, used = 0U;
    char *clean;

    if (!target || target[0] != '/' || !path || !query)
        return KC_ROUTE_EURI;
    *path = NULL;
    *query = NULL;
    mark = strchr(target, '?');
    length = mark ? (size_t)(mark - target) : strlen(target);
    clean = (char *)malloc(length + 1U);
    if (!clean) return KC_ROUTE_ENOMEM;
    for (i = 0U; i < length; ++i) {
        unsigned char ch = (unsigned char)target[i];

        if (ch < 32U || ch == 127U || ch == '#') {
            free(clean);
            return KC_ROUTE_EURI;
        }
        if (ch == '/' && used > 0U && clean[used - 1U] == '/') continue;
        clean[used++] = (char)ch;
    }
    while (used > 1U && clean[used - 1U] == '/') --used;
    clean[used] = '\0';
    if (mark) {
        *query = kc_route_copy(mark + 1U);
        if (!*query) {
            free(clean);
            return KC_ROUTE_ENOMEM;
        }
    }
    *path = clean;
    return KC_ROUTE_OK;
}

/**
 * Open an empty registry.
 * @param out Receives the registry.
 * @return Route status code.
 */
int kc_route_open(kc_route_t **out)
{
    if (!out) return KC_ROUTE_EINVAL;
    *out = (kc_route_t *)calloc(1U, sizeof(**out));
    return *out ? KC_ROUTE_OK : KC_ROUTE_ENOMEM;
}

/**
 * Register an ordered method and full-path pattern.
 * @param route Registry.
 * @param method Method or ALL.
 * @param pattern POSIX extended pattern.
 * @param id Destination identifier.
 * @param handler Optional match visitor.
 * @param userdata Visitor context.
 * @return Route status code.
 */
int kc_route_add(kc_route_t *route, const char *method, const char *pattern,
    const char *id, kc_route_fn handler, void *userdata)
{
    kc_route_entry_t *entry;
    char *expression;
    size_t i, length;

    if (!route || !method || !*method || !pattern || !id || !*id)
        return KC_ROUTE_EINVAL;
    length = strlen(pattern);
    if (length > SIZE_MAX - 3U) return KC_ROUTE_EINVAL;
    entry = (kc_route_entry_t *)calloc(1U, sizeof(*entry));
    if (!entry) return KC_ROUTE_ENOMEM;
    entry->method = kc_route_copy(method);
    entry->id = kc_route_copy(id);
    expression = (char *)malloc(length + 3U);
    if (!entry->method || !entry->id || !expression) {
        free(entry->method);
        free(entry->id);
        free(expression);
        free(entry);
        return KC_ROUTE_ENOMEM;
    }
    for (i = 0U; entry->method[i]; ++i)
        entry->method[i] = (char)toupper((unsigned char)entry->method[i]);
    expression[0] = '^';
    memcpy(expression + 1U, pattern, length);
    expression[length + 1U] = '$';
    expression[length + 2U] = '\0';
    if (regcomp(&entry->regex, expression, REG_EXTENDED) != 0) {
        free(expression);
        free(entry->method);
        free(entry->id);
        free(entry);
        return KC_ROUTE_EPATTERN;
    }
    free(expression);
    entry->handler = handler;
    entry->userdata = userdata;
    if (route->last) route->last->next = entry;
    else route->first = entry;
    route->last = entry;
    return KC_ROUTE_OK;
}

/**
 * Evaluate ordered registrations and invoke matching callbacks.
 * @param route Borrowed registry.
 * @param method HTTP method.
 * @param target HTTP request target.
 * @param matched Receives accepted-match indicator.
 * @return Route status code.
 */
int kc_route_run(const kc_route_t *route, const char *method,
    const char *target, int *matched)
{
    kc_route_entry_t *entry;
    char *path = NULL, *query = NULL;
    int status;

    if (!route || !method || !*method || !matched) return KC_ROUTE_EINVAL;
    *matched = 0;
    status = kc_route_target(target, &path, &query);
    if (status != KC_ROUTE_OK) return status;

    for (entry = route->first; entry; entry = entry->next) {
        regmatch_t positions[KC_ROUTE_CAPTURES + 1U];
        const char *groups[KC_ROUTE_CAPTURES];
        char *allocated[KC_ROUTE_CAPTURES] = {0};
        kc_route_match_t match;
        size_t i, count = 0U;
        int accepted;

        if (strcmp(entry->method, "ALL") != 0) {
            for (i = 0U; method[i] && entry->method[i]; ++i) {
                if (toupper((unsigned char)method[i]) != entry->method[i])
                    break;
            }
            if (method[i] || entry->method[i]) continue;
        }
        if (regexec(&entry->regex, path, KC_ROUTE_CAPTURES + 1U,
                positions, 0) != 0) continue;
        for (i = 1U; i <= KC_ROUTE_CAPTURES; ++i) {
            size_t width;

            if (positions[i].rm_so < 0) break;
            width = (size_t)(positions[i].rm_eo - positions[i].rm_so);
            allocated[count] = (char *)malloc(width + 1U);
            if (!allocated[count]) break;
            memcpy(allocated[count], path + positions[i].rm_so, width);
            allocated[count][width] = '\0';
            groups[count] = allocated[count];
            ++count;
        }
        if (i <= KC_ROUTE_CAPTURES) {
            while (count) free(allocated[--count]);
            free(path);
            free(query);
            return KC_ROUTE_ENOMEM;
        }
        match.method = method;
        match.path = path;
        match.query = query;
        match.id = entry->id;
        match.captures = groups;
        match.capture_count = count;
        accepted = entry->handler ? entry->handler(&match,
            entry->userdata) : 1;
        for (i = 0U; i < count; ++i) free(allocated[i]);
        if (accepted) {
            *matched = 1;
            break;
        }
    }
    free(path);
    free(query);
    return KC_ROUTE_OK;
}

/**
 * Dispatch a libhttp request without retaining its borrowed fields.
 * @param route Registry.
 * @param request Borrowed parsed HTTP request.
 * @param matched Receives accepted-match indicator.
 * @return Route status code.
 */
int kc_route_http(const kc_route_t *route, const kc_http_request_t *request,
    int *matched)
{
    if (!request) return KC_ROUTE_EINVAL;
    return kc_route_run(route, request->method, request->target, matched);
}

/**
 * Release every compiled route and registration.
 * @param route Registry or NULL.
 * @return None.
 */
void kc_route_close(kc_route_t *route)
{
    kc_route_entry_t *entry, *next;

    if (!route) return;
    for (entry = route->first; entry; entry = next) {
        next = entry->next;
        regfree(&entry->regex);
        free(entry->method);
        free(entry->id);
        free(entry);
    }
    free(route);
}

/**
 * Describe one API status code.
 * @param status Status returned by route operations.
 * @return Static status description.
 */
const char *kc_route_strerror(int status)
{
    switch (status) {
        case KC_ROUTE_OK: return "success";
        case KC_ROUTE_EINVAL: return "invalid argument";
        case KC_ROUTE_ENOMEM: return "out of memory";
        case KC_ROUTE_EPATTERN: return "invalid route pattern";
        case KC_ROUTE_EURI: return "invalid request URI";
        default: return "unknown error";
    }
}

/**
 * Return the generated build timestamp.
 * @return Unix timestamp or zero.
 */
uint64_t kc_route_version(void)
{
    return (uint64_t)KC_ROUTE_BUILD_VERSION;
}
