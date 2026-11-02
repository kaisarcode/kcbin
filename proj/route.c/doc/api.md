# C API

`route` maintains an ordered registry of HTTP methods and path expressions. It is independent of sockets, response writing, template rendering and the process that receives a request.

## Registry

`kc_route_open()` allocates an empty registry. `kc_route_add()` records an HTTP method, a portable route expression, a destination identifier and an optional callback. Registrations are checked in insertion order. The special method `ALL` matches any request method. Every pattern matches the complete canonical path, not a substring.

`kc_route_run()` takes a method and origin-form target, normalizes repeated and trailing slashes, separates the query, and visits matching registrations. Callback data, including captured groups, is borrowed only during callback execution. Returning zero continues to the next registration; returning nonzero accepts it and stops. A null callback accepts immediately. `matched` is set to zero when no handler accepts the request.

`kc_route_http()` adapts a borrowed `kc_http_request_t` from `libhttp.c` to the same routing operation. `kc_route_close()` releases the registry and compiled expressions. The caller owns callback context for as long as registered callbacks can execute.

## Patterns and paths

The first capture is the first parenthesized group in the expression; up to 16 captures are returned. Matching uses POSIX extended regex syntax, which differs from PHP PCRE in some constructs. Patterns must not include delimiters. The portable matcher supports literals, escaped characters, byte classes and ranges, `.`, capturing groups, alternation (`|`) and the `*`, `+`, `?` quantifiers. It matches the entire path implicitly. Explicit `^` and `# C API

`route` maintains an ordered registry of HTTP methods and path expressions. It is independent of sockets, response writing, template rendering and the process that receives a request.

## Registry

`kc_route_open()` allocates an empty registry. `kc_route_add()` records an HTTP method, a portable route expression, a destination identifier and an optional callback. Registrations are checked in insertion order. The special method `ALL` matches any request method. Every pattern matches the complete canonical path, not a substring.

`kc_route_run()` takes a method and origin-form target, normalizes repeated and trailing slashes, separates the query, and visits matching registrations. Callback data, including captured groups, is borrowed only during callback execution. Returning zero continues to the next registration; returning nonzero accepts it and stops. A null callback accepts immediately. `matched` is set to zero when no handler accepts the request.

`kc_route_http()` adapts a borrowed `kc_http_request_t` from `libhttp.c` to the same routing operation. `kc_route_close()` releases the registry and compiled expressions. The caller owns callback context for as long as registered callbacks can execute.

## Patterns and paths

The first capture is the first parenthesized group in the expression; up to 16 captures are returned. Matching uses POSIX extended regex syntax, which differs from PHP PCRE in some constructs.  anchors, counted repeats and PCRE extensions are not supported; this is not full PHP/PCRE compatibility. Patterns and work are bounded to avoid uncontrolled matching.

The path is normalized before matching, while the query remains separate and unchanged. This tool does not issue the canonical HTTP 301 redirect in `Route.php`; redirect policy belongs to the HTTP response composition. It does not decode percent-encoded segments or authorize filesystem access. Applications serving files must independently validate and confine paths.

## JavaScript API projection

The intended JavaScript projection maps `Route.add()`, `Route.get()`, `Route.post()`, `Route.put()`, `Route.delete()` and `Route.all()` to the same native registry operations and callback continuation semantics. This document describes the intended projection; it does not claim a JavaScript binding has already been implemented.

## Next step

Return to the [project README](../README.md).
