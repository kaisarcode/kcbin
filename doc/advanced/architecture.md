# Architecture

kcbin is the level-1 (LV1) composition layer built from autonomous level-0 (LV0) KaisarCode primitives. Unlike kclib, a kcbin project may depend on several independent kclibs to offer a reusable higher-level tool.

## Levels

- **LV0 — kclib:** independent primitive capabilities; no dependencies on other kclibs.
- **LV1 — kcbin:** explicit compositions of LV0 primitives; no dependencies on other LV1 tools.
- **Higher levels:** consumers and applications may combine tools from any lower level without creating cycles.

For a project P with KaisarCode dependencies D, its effective level is `1 + max(level(D))`. Dependencies always point downward, and using an LV0 capability directly is allowed at every higher level. The levels describe software composition, not commercial status, UI presence, or whether an artifact is a binary or a shared library.

## Project boundaries

Each project under `proj/` has one reusable responsibility, its own README and license, and a project-local source manifest with dependencies. A higher-level tool may expose a CLI, a library API, a long-lived service, or a flow recipe. No universal server runtime or framework is required.

Keep network transport, message protocol, TLS, rendering, persistence, and application policy in their existing owner components. Compose only the operations the LV1 contract needs. Do not hide or duplicate primitive responsibilities inside the composition merely for convenience.

A future HTTP server, for example, can compose `libnetl` with `libhttp` without changing either primitive. Routing and request dispatch are composition concerns unless a truly independent primitive is discovered.

## Distribution

Build and test one project independently. Publish its artifacts and per-project metadata without depending on the rest of the kcbin repository. The dependency manifest records requirements; the package-manager implementation determines how and when those are installed.

The cross-compilation targets follow kclib; the LV1 build reuses precompiled, target-matched LV0 shared libraries rather than rebuilding them. Refer to [Cross-compilation and dynamic linking](cross-compilation.md) for platform-specific link artifacts and deployment considerations.

The `kc` command is not part of kcbin's required runtime or build contract. If it is used later, its scope is generic KaisarCode packages rather than kclibs alone.

## Next step

Return to the [Project catalog](../catalog.md).
