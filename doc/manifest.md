# Package manifests

Every kcbin project must own a source `proj/NAME/manifest.json`. It describes the package, the level of composition, and its explicit KaisarCode dependencies.

A hypothetical HTTP-server project would declare:

```json
{
  "name": "http-server",
  "description": "Serves HTTP requests through composed networking and protocol primitives.",
  "author": "KaisarCode",
  "license": "LGPL-3.0-only",
  "website": "https://kaisarcode.com",
  "repository": "https://github.com/kaisarcode/kcbin/tree/master/proj/http-server",
  "level": 1,
  "dependencies": ["libhttp", "libnetl"]
}
```

This example is a proposal, not an existing project. The `license` field is **LGPL-3.0-only** for every kcbin project, just as for kclibs. Only the kcbin repository infrastructure and root tooling use GPLv3. Each project's `LICENSE` contains the LGPLv3 license text, and its `COPYING` contains the GPLv3 text incorporated by the LGPLv3 license, following the kclib convention.

## Dependency contract

The `dependencies` array contains the logical package names of required KaisarCode components, matching the `name` field of their source manifests. For LV1 it must contain at least one LV0 kclib. All entries must resolve to lower-level packages; a dependency on another LV1 tool would raise the effective composition level and would not belong in kcbin.

Use `"level": 1` for kcbin packages. The general rule is that a package's effective level equals one plus the maximum level of its KaisarCode dependencies. External toolchains and operating-system libraries are documented separately; they do not alter this hierarchy.

The manifest declares direct requirements rather than copying dependent binaries into the package. The same dependency names are resolved to platform/architecture-specific precompiled library artifacts by the project's build and eventually by an installer. The source manifest itself does not specify a host library path or make dynamic loader resolution work. Keep version pinning, ABI compatibility, runtime lookup, and cross-platform details explicit when the corresponding package-manager contracts support them.

## Publication and current support

`./scripts/dist.sh NAME` publishes the project's built binaries and public headers into `dist/<platform>-<arch>/`, alongside any existing kclib artifacts for the same target. Each target's `SHA256SUMS` is the published artifact inventory and checksum reference. No `dist/NAME/manifest.json`, generated `libNAME.json`, project README, or central distribution manifest is produced.

The source `manifest.json` is declarative metadata used to describe dependencies. `dist.sh` does not install or resolve the `dependencies` field, nor does the distribution guarantee runtime loader configuration.

## Next step

Continue with [Build and distribution](build.md).
