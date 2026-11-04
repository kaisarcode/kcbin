# C API

`route` maintains an ordered registry of HTTP methods and path expressions. It selects destinations without implementing sockets, HTTP responses or application commands. Its compiled-pattern engine comes from the independent kclib `libregex.c`.

## Registry

`kc_route_open()` allocates an empty registry. `kc_route_add()` records an HTTP method, a `libregex` pattern, an ID and an optional callback. Pattern compilation happens at registration time, and each entry owns its compiled regex until `kc_route_close()`.

Registrations are checked in insertion order. The special method `ALL` accepts any request method, and method matching is case-insensitive. Each pattern must match the **entire canonical path**; the query string is not part of the match.

`kc_route_run()` takes a method and origin-form target, normalizes repeated and trailing slashes, separates the query and checks matching registrations. A callback that returns zero declines a match and allows subsequent routes; a nonzero result accepts the match and stops routing. The callback match contains a borrowed `request` pointer when dispatch originated from `kc_route_http()`, and `NULL` for `kc_route_run()`. An omitted callback accepts immediately.

`kc_route_http()` takes a borrowed `kc_http_request_t` from `libhttp.c` and dispatches with the complete request available to handlers. It does not own, copy or reparse the request. The `matched` output is set to zero when no registered route accepts the request.

## Pattern matching and captures

Matching uses `kc_regex_open()` and `kc_regex_match()` from `libregex.c`; `route` contains no regex parser, interpreter or system-`regex.h` fallback.

Up to 16 capturing groups are delivered as borrowed strings through the callback. Group zero (the entire match) is not included in the callback's `captures` array. Optional groups that did not participate are represented by empty strings. The canonical path, query string, ID and captures are valid only during the callback.

For syntax, operators, limits and unsupported PCRE features, consult `kclib/proj/libregex.c/doc/syntax.md`. No regular-expression behavior is reimplemented in `route`.

## Errors and ownership

`KC_ROUTE_OK` means the routing operation was evaluated successfully, including when no route was accepted. `KC_ROUTE_EPATTERN` reports a rejected pattern during registration. Other failures include `KC_ROUTE_EINVAL` (invalid arguments), `KC_ROUTE_ENOMEM` (allocation failure), `KC_ROUTE_EURI` (invalid request target) and `KC_ROUTE_ELIMIT` (the regex matching budget was exceeded).

A regex error other than `KC_REGEX_NOMATCH` stops dispatch and propagates through the corresponding route error; it does not silently fall back to another route. On failure, the registry remains owned by the caller and is released with `kc_route_close()`.

The route registry owns copied method strings, IDs and compiled expressions. The caller retains ownership of callback userdata for the lifetime of registrations.

## Paths and HTTP

The input path is normalized before matching, while the query is preserved. `route` does not issue the canonical HTTP 301 redirect of the original PHP `Route.php`; that policy belongs to HTTP response composition. No percent-decoding, filesystem authorization or path confinement is performed here. A file-serving consumer must validate access independently.

## JavaScript API projection

The intended public JavaScript projection maps `Route.add()`, `Route.get()`, `Route.post()`, `Route.put()`, `Route.delete()` and `Route.all()` to the same native registration and callback semantics. On WASM, the distributed `libroute.js` implements this projection as an ES module. Native callbacks are marshaled to JavaScript handlers; the WASM build includes a Node.js API contract test.

## Next step

Return to the [project README](../README.md).
