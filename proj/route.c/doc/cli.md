# CLI

```text
Usage: route -r <method> <pattern> <command> [-r ...] < request.http

Options:
  -r, --route <method> <pattern> <command>  Register a handler command
  -h, --help                               Show this help
  -v, --version                            Show version
```

The CLI reads HTTP/1.x wire messages from stdin. `libhttp.c` parses each complete request and `route.c` selects the first matching method and canonical path, with regular expressions supplied by `libregex.c`. A matching command is launched through the platform shell. It receives a rebuilt HTTP request on stdin and its stdout passes through unmodified as the HTTP response. The handler must write a valid HTTP response, including its status line and framing. Route never opens a socket, never prints a match JSON object and never constructs a fallback response.

```sh
printf 'POST /users/42 HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\ntest' |
    route -r POST '/users/([0-9]+)' './handlers/update' \
          -r ALL '/.*' './handlers/notfound'
```

Multiple requests can arrive sequentially over stdin; each matched request invokes one command. Missing matches, HTTP parsing failures, handler failures and invalid route syntax produce a nonzero exit status and diagnostics on stderr. The CLI does not synthesize HTTP 404 or 500 responses. Register explicit error or fallback handlers when required.

Commands come from the CLI configuration and must be trusted: the CLI runs them through the system shell. Route captures are available to native C callbacks. CLI handler commands receive the complete HTTP request and can extract path parameters themselves; captures are not currently passed separately through command arguments or environment variables.

The native API is the preferred integration for an already parsed request within a server. It forwards the borrowed `kc_http_request_t` through `kc_route_match_t.request` to registered callbacks without parsing the request again.

## Next step

Return to the [project README](../README.md).
