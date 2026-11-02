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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_ROUTE_BUILD_VERSION
#define KC_ROUTE_BUILD_VERSION 0
#endif


#define KC_ROUTE_PATTERN_MAX 4096U
#define KC_ROUTE_NODES_MAX 512U
#define KC_ROUTE_STEPS_MAX 100000U
#define KC_ROUTE_DEPTH_MAX 256U

typedef enum {
    KC_ROUTE_CHAR, KC_ROUTE_DOT, KC_ROUTE_CLASS, KC_ROUTE_SEQUENCE,
    KC_ROUTE_CHOICE, KC_ROUTE_REPEAT, KC_ROUTE_GROUP
} kc_route_kind_t;

typedef struct kc_route_node {
    kc_route_kind_t kind;
    struct kc_route_node *left;
    struct kc_route_node *right;
    unsigned char bitmap[32];
    unsigned char ch;
    size_t group;
    size_t minimum;
    size_t maximum;
} kc_route_node_t;

typedef struct {
    const char *text;
    size_t pos;
    size_t nodes;
    size_t groups;
    int error;
} kc_route_parser_t;

typedef struct {
    size_t pos;
    size_t start[KC_ROUTE_CAPTURES];
    size_t end[KC_ROUTE_CAPTURES];
} kc_route_state_t;

typedef struct kc_route_cont {
    const struct kc_route_cont *parent;
    const kc_route_node_t *node;
    size_t group;
    size_t repeat_count;
    size_t repeat_pos;
    int type;
} kc_route_cont_t;

/**
 * Release one compiled pattern tree recursively.
 * @param node Owned tree root, or NULL.
 * @return None.
 */
static void kc_route_node_free(kc_route_node_t *node)
{
    if (!node) return;
    kc_route_node_free(node->left);
    kc_route_node_free(node->right);
    free(node);
}

/**
 * Allocate one bounded pattern node.
 * @param parser Mutable parser context.
 * @param kind Node operation.
 * @return New node or NULL on invalid complexity or allocation.
 */
static kc_route_node_t *kc_route_node_new(kc_route_parser_t *parser,
    kc_route_kind_t kind)
{
    kc_route_node_t *node;

    if (++parser->nodes > KC_ROUTE_NODES_MAX) {
        parser->error = 1;
        return NULL;
    }
    node = (kc_route_node_t *)calloc(1U, sizeof(*node));
    if (!node) parser->error = 1;
    else node->kind = kind;
    return node;
}

/**
 * Set one supported character-class byte.
 * @param node Character-class node.
 * @param byte Byte to accept.
 * @return None.
 */
static void kc_route_class_byte(kc_route_node_t *node, unsigned char byte)
{
    node->bitmap[byte / 8U] |= (unsigned char)(1U << (byte % 8U));
}

/**
 * Parse one pattern expression, including alternatives.
 * @param parser Current parser state.
 * @return Owned pattern AST, or NULL for invalid syntax.
 */
static kc_route_node_t *kc_route_parse_alt(kc_route_parser_t *parser);

/**
 * Parse a literal, class, group or wildcard atom.
 * @param parser Current parser state.
 * @return Owned node, or NULL on failure.
 */
static kc_route_node_t *kc_route_parse_atom(kc_route_parser_t *parser)
{
    kc_route_node_t *node;
    unsigned char ch = (unsigned char)parser->text[parser->pos++];

    if (ch == '(') {
        if (++parser->groups > KC_ROUTE_CAPTURES) {
            parser->error = 1;
            return NULL;
        }
        node = kc_route_node_new(parser, KC_ROUTE_GROUP);
        if (!node) return NULL;
        node->group = parser->groups - 1U;
        node->left = kc_route_parse_alt(parser);
        if (parser->text[parser->pos] != ')') parser->error = 1;
        else parser->pos++;
        if (parser->error) {
            kc_route_node_free(node);
            return NULL;
        }
        return node;
    }
    if (ch == '[') {
        int negate = 0, first = 1, closed = 0;
        unsigned char prev = 0;

        node = kc_route_node_new(parser, KC_ROUTE_CLASS);
        if (!node) return NULL;
        if (parser->text[parser->pos] == '^') {
            negate = 1;
            parser->pos++;
        }
        while (parser->text[parser->pos]) {
            unsigned char current =
                (unsigned char)parser->text[parser->pos++];
            if (current == ']' && !first) {
                closed = 1;
                break;
            }
            if (current == '\\' && parser->text[parser->pos])
                current = (unsigned char)parser->text[parser->pos++];
            if (current == '-' && !first &&
                parser->text[parser->pos] &&
                parser->text[parser->pos] != ']') {
                unsigned char last =
                    (unsigned char)parser->text[parser->pos++];
                unsigned int i;

                if (last == '\\' && parser->text[parser->pos])
                    last = (unsigned char)parser->text[parser->pos++];
                if (last < prev) {
                    parser->error = 1;
                    break;
                }
                for (i = prev; i <= last; ++i)
                    kc_route_class_byte(node, (unsigned char)i);
                prev = last;
            } else {
                kc_route_class_byte(node, current);
                prev = current;
            }
            first = 0;
        }
        if (!closed || first) parser->error = 1;
        if (negate) {
            size_t i;
            for (i = 0U; i < sizeof(node->bitmap); ++i)
                node->bitmap[i] = (unsigned char)~node->bitmap[i];
        }
        if (parser->error) {
            kc_route_node_free(node);
            return NULL;
        }
        return node;
    }
    if (ch == '.' ) return kc_route_node_new(parser, KC_ROUTE_DOT);
    if (ch == '\\' && parser->text[parser->pos])
        ch = (unsigned char)parser->text[parser->pos++];
    else if (ch == '\\' || ch == '*' || ch == '+' || ch == '?' ||
             ch == ')' || ch == '|' || ch == '{' || ch == '}' ||
             ch == '^' || ch == '
    char *method;
    char *id;
    kc_route_node_t *pattern;
    size_t groups;
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
    size_t i;

    if (!route || !method || !*method || !pattern || !id || !*id)
        return KC_ROUTE_EINVAL;
    entry = (kc_route_entry_t *)calloc(1U, sizeof(*entry));
    if (!entry) return KC_ROUTE_ENOMEM;
    entry->method = kc_route_copy(method);
    entry->id = kc_route_copy(id);
    if (!entry->method || !entry->id) {
        free(entry->method);
        free(entry->id);
        free(entry);
        return KC_ROUTE_ENOMEM;
    }
    for (i = 0U; entry->method[i]; ++i)
        entry->method[i] = (char)toupper((unsigned char)entry->method[i]);
    entry->pattern = kc_route_compile(pattern, &entry->groups);
    if (!entry->pattern) {
        free(entry->method);
        free(entry->id);
        free(entry);
        return KC_ROUTE_EPATTERN;
    }
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
        kc_route_state_t captures;
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
        if (!kc_route_match(entry->pattern, entry->groups, path, &captures))
            continue;
        for (i = 0U; i < entry->groups; ++i) {
            size_t width = captures.start[i] == SIZE_MAX ? 0U :
                captures.end[i] - captures.start[i];

            allocated[count] = (char *)malloc(width + 1U);
            if (!allocated[count]) break;
            if (width) memcpy(allocated[count],
                path + captures.start[i], width);
            allocated[count][width] = '\0';
            groups[count] = allocated[count];
            ++count;
        }
        if (i < entry->groups) {
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
        kc_route_node_free(entry->pattern);
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
) {
        parser->error = 1;
        return NULL;
    }
    node = kc_route_node_new(parser, KC_ROUTE_CHAR);
    if (node) node->ch = ch;
    return node;
}

/**
 * Parse concatenated atoms with optional repetition.
 * @param parser Current parser state.
 * @return Owned sequence tree, or NULL for an empty sequence.
 */
static kc_route_node_t *kc_route_parse_seq(kc_route_parser_t *parser)
{
    kc_route_node_t *tree = NULL;

    while (!parser->error && parser->text[parser->pos] &&
        parser->text[parser->pos] != ')' &&
        parser->text[parser->pos] != '|') {
        kc_route_node_t *atom = kc_route_parse_atom(parser);
        unsigned char next = (unsigned char)parser->text[parser->pos];

        if (!atom) break;
        if (next == '*' || next == '+' || next == '?') {
            kc_route_node_t *repeat = kc_route_node_new(parser,
                KC_ROUTE_REPEAT);
            if (!repeat) {
                kc_route_node_free(atom);
                break;
            }
            repeat->left = atom;
            repeat->minimum = next == '+' ? 1U : 0U;
            repeat->maximum = next == '?' ? 1U : SIZE_MAX;
            atom = repeat;
            parser->pos++;
        }
        if (tree) {
            kc_route_node_t *sequence = kc_route_node_new(parser,
                KC_ROUTE_SEQUENCE);
            if (!sequence) {
                kc_route_node_free(atom);
                break;
            }
            sequence->left = tree;
            sequence->right = atom;
            tree = sequence;
        } else tree = atom;
    }
    if (parser->error) {
        kc_route_node_free(tree);
        return NULL;
    }
    return tree;
}

/**
 * Parse alternations of full sequences.
 * @param parser Parser context.
 * @return Owned AST, or NULL on invalid input.
 */
static kc_route_node_t *kc_route_parse_alt(kc_route_parser_t *parser)
{
    kc_route_node_t *tree = kc_route_parse_seq(parser);

    while (!parser->error && parser->text[parser->pos] == '|') {
        kc_route_node_t *choice = kc_route_node_new(parser,
            KC_ROUTE_CHOICE);
        if (!choice) break;
        parser->pos++;
        choice->left = tree;
        choice->right = kc_route_parse_seq(parser);
        tree = choice;
    }
    if (parser->error) {
        kc_route_node_free(tree);
        return NULL;
    }
    return tree;
}

/**
 * Compile the supported portable ERE subset.
 * @param pattern Borrowed pattern text.
 * @param groups Receives number of capture groups.
 * @return Owned tree or NULL for invalid syntax.
 */
static kc_route_node_t *kc_route_compile(const char *pattern, size_t *groups)
{
    kc_route_parser_t parser = {0};
    kc_route_node_t *node;
    size_t len;

    if (!pattern || !groups) return NULL;
    len = strlen(pattern);
    if (!len || len > KC_ROUTE_PATTERN_MAX) return NULL;
    parser.text = pattern;
    node = kc_route_parse_alt(&parser);
    if (parser.error || parser.text[parser.pos]) {
        kc_route_node_free(node);
        return NULL;
    }
    *groups = parser.groups;
    return node;
}

/**
 * Advance a match into its next continuation.
 * @param cont Pending sequence, group or repetition.
 * @param path Canonical request path.
 * @param state Current capture state.
 * @param result Receives completed match state.
 * @param steps Shared bounded work counter.
 * @param depth Current recursion depth.
 * @return Nonzero when the full path matched.
 */
static int kc_route_advance(const kc_route_cont *cont, const char *path,
    kc_route_state_t state, kc_route_state_t *result,
    size_t *steps, size_t depth);

/**
 * Evaluate one compiled node with bounded backtracking.
 * @param node Current expression node.
 * @param cont Remaining pattern continuation.
 * @param path Canonical request path.
 * @param state Current position and captures.
 * @param result Receives complete match.
 * @param steps Shared work counter.
 * @param depth Recursion depth.
 * @return Nonzero for a complete path match.
 */
static int kc_route_match_node(const kc_route_node_t *node,
    const kc_route_cont *cont, const char *path,
    kc_route_state_t state, kc_route_state_t *result,
    size_t *steps, size_t depth)
{
    kc_route_cont next;

    if (++*steps > KC_ROUTE_STEPS_MAX || depth > KC_ROUTE_DEPTH_MAX)
        return 0;
    if (!node) return kc_route_advance(cont, path, state, result,
        steps, depth + 1U);
    switch (node->kind) {
        case KC_ROUTE_SEQUENCE:
            next.parent = cont;
            next.node = node->right;
            next.type = 0;
            return kc_route_match_node(node->left, &next, path, state,
                result, steps, depth + 1U);
        case KC_ROUTE_CHOICE:
            if (kc_route_match_node(node->left, cont, path, state,
                    result, steps, depth + 1U)) return 1;
            return kc_route_match_node(node->right, cont, path, state,
                result, steps, depth + 1U);
        case KC_ROUTE_REPEAT:
            next.parent = cont;
            next.node = node;
            next.type = 2;
            next.repeat_count = 0U;
            next.repeat_pos = state.pos;
            return kc_route_advance(&next, path, state, result,
                steps, depth + 1U);
        case KC_ROUTE_GROUP:
            next.parent = cont;
            next.node = NULL;
            next.type = 1;
            next.group = node->group;
            state.start[node->group] = state.pos;
            return kc_route_match_node(node->left, &next, path, state,
                result, steps, depth + 1U);
        case KC_ROUTE_CHAR:
            if ((unsigned char)path[state.pos] != node->ch ||
                path[state.pos] == '\0') return 0;
            break;
        case KC_ROUTE_DOT:
            if (path[state.pos] == '\0') return 0;
            break;
        case KC_ROUTE_CLASS:
            if (path[state.pos] == '\0' ||
                !(node->bitmap[(unsigned char)path[state.pos] / 8U] &
                (1U << ((unsigned char)path[state.pos] % 8U)))) return 0;
            break;
    }
    state.pos++;
    return kc_route_advance(cont, path, state, result, steps, depth + 1U);
}

/**
 * Continue a pending sequence, group closure or greedy repeat.
 * @param cont Continuation frame.
 * @param path Canonical path.
 * @param state Current match state.
 * @param result Receives a successful complete match.
 * @param steps Bounded work counter.
 * @param depth Recursion depth.
 * @return Nonzero when the entire pattern matches.
 */
static int kc_route_advance(const kc_route_cont *cont, const char *path,
    kc_route_state_t state, kc_route_state_t *result,
    size_t *steps, size_t depth)
{
    kc_route_cont next;

    if (++*steps > KC_ROUTE_STEPS_MAX || depth > KC_ROUTE_DEPTH_MAX)
        return 0;
    if (!cont) {
        if (path[state.pos]) return 0;
        *result = state;
        return 1;
    }
    if (cont->type == 0)
        return kc_route_match_node(cont->node, cont->parent, path,
            state, result, steps, depth + 1U);
    if (cont->type == 1) {
        state.end[cont->group] = state.pos;
        return kc_route_advance(cont->parent, path, state, result,
            steps, depth + 1U);
    }
    if (cont->type == 2) {
        const kc_route_node_t *repeat = cont->node;

        if (cont->repeat_count < repeat->maximum &&
            (cont->repeat_count == 0U || state.pos > cont->repeat_pos)) {
            next = *cont;
            next.repeat_count++;
            next.repeat_pos = state.pos;
            if (kc_route_match_node(repeat->left, &next, path, state,
                    result, steps, depth + 1U)) return 1;
        }
        if (cont->repeat_count >= repeat->minimum)
            return kc_route_advance(cont->parent, path, state, result,
                steps, depth + 1U);
    }
    return 0;
}

/**
 * Match one expression against the complete path.
 * @param node Compiled pattern.
 * @param groups Number of capture groups.
 * @param path Canonical URI path.
 * @param result Receives captured byte ranges.
 * @return Nonzero when matched.
 */
static int kc_route_match(const kc_route_node_t *node, size_t groups,
    const char *path, kc_route_state_t *result)
{
    kc_route_state_t state = {0};
    size_t steps = 0U, i;

    for (i = 0U; i < groups; ++i) {
        state.start[i] = SIZE_MAX;
        state.end[i] = SIZE_MAX;
    }
    return kc_route_match_node(node, NULL, path, state, result,
        &steps, 0U);
}

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
    size_t i;

    if (!route || !method || !*method || !pattern || !id || !*id)
        return KC_ROUTE_EINVAL;
    entry = (kc_route_entry_t *)calloc(1U, sizeof(*entry));
    if (!entry) return KC_ROUTE_ENOMEM;
    entry->method = kc_route_copy(method);
    entry->id = kc_route_copy(id);
    if (!entry->method || !entry->id) {
        free(entry->method);
        free(entry->id);
        free(entry);
        return KC_ROUTE_ENOMEM;
    }
    for (i = 0U; entry->method[i]; ++i)
        entry->method[i] = (char)toupper((unsigned char)entry->method[i]);
    if (regcomp(&entry->regex, pattern, REG_EXTENDED) != 0) {
        free(entry->method);
        free(entry->id);
        free(entry);
        return KC_ROUTE_EPATTERN;
    }
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
        size_t slots;
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
        slots = entry->regex.re_nsub < KC_ROUTE_CAPTURES ?
            entry->regex.re_nsub : KC_ROUTE_CAPTURES;
        if (regexec(&entry->regex, path, slots + 1U,
                positions, 0) != 0 || positions[0].rm_so != 0 ||
                (size_t)positions[0].rm_eo != strlen(path)) continue;
        for (i = 1U; i <= slots; ++i) {
            size_t width;

            if (positions[i].rm_so < 0) {
                allocated[count] = kc_route_copy("");
                if (!allocated[count]) break;
            } else {
                width = (size_t)(positions[i].rm_eo - positions[i].rm_so);
                allocated[count] = (char *)malloc(width + 1U);
                if (!allocated[count]) break;
                memcpy(allocated[count], path + positions[i].rm_so, width);
                allocated[count][width] = '\0';
            }
            groups[count] = allocated[count];
            ++count;
        }
        if (i <= slots) {
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
