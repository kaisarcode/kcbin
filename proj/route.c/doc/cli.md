# CLI

```text
Usage: route match <method> <target> -r <method> <pattern> <id> [-r ...]

Commands:
  match  Select the first accepted route and print its JSON result

Options:
  -r, --route <method> <pattern> <id>  Register an ordered route
  -h, --help                          Show this help
  -v, --version                       Show version
```

A matching route prints a JSON object with `id`, `method`, `path`, `query`, and `captures`. Unmatched requests print JSON `null`. Syntax, invalid URIs and invalid expressions produce a nonzero exit status and diagnostic on stderr.

This program is a route selector intended for composition through a shell or `libflow.c`. The selected ID does not automatically execute commands. Route handlers that decline a match are available through the native C callback API.

## Next step

Return to the [project README](../README.md).
