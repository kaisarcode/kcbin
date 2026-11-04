# kcbin repository instructions

kcbin is the KaisarCode monorepo for **level-1 (LV1) composable tools**. Each project lives in `proj/<name>/` and combines one or more independent **level-0 (LV0) primitives** from kclib. A kcbin project may expose an executable, a shared C library and public header, or an explicit flow-based tool, according to its actual purpose. These are free-software tools, not an application framework.

## Dependency levels

- LV0 kclibs are autonomous primitives and never depend on other kclibs.
- LV1 kcbin packages may depend on LV0 primitives and external system libraries.
- An LV1 package must not depend on an LV1 package or anything at a higher level.
- Generally, a package at level N may depend only on packages with level lower than N. A project's effective level is one plus its highest KaisarCode dependency level.
- Do not reclassify an LV1 composition as a kclib, and do not add any kcbin dependency to kclib.
- Declare all KaisarCode dependencies explicitly in each project's `manifest.json`; do not silently install them or copy them into the source tree.

## Existing project precedents

Before building, refactoring, or extending a kcbin project, inspect current
comparable projects in kcbin and the relevant kclibs they compose. Read their
source, public headers, CLIs, READMEs, tests, and build configurations. The
repository's actual implementations are the baseline: never invent a command
convention, API model, file structure, or interface contract from memory.

Reuse proven patterns for APIs, arguments and parameter types, option defaults,
errors, ownership and lifecycle, help and version output, positional arguments,
flags, input precedence, stdin and file handling, output formats, diagnostics,
exit statuses, test cases and PASS/FAIL reporting, cross-platform builds,
dependency linking, manifests, and documentation. Inspect the exact underlying
kclib APIs before writing an adapter or composition; project their behavior
faithfully instead of inventing different semantics or implementing features
the dependency already provides.

Select precedents by matching responsibility and interface, not by superficial
file similarity. Follow equivalent concepts consistently and deviate only when
the project's concrete capability requires it. Keep those differences explicit
and minimal. Apply current KCS rules where older examples conflict with them.
Do not copy unrelated mechanisms or introduce extra files and helpers solely
for organization.

## Naming

Use the shortest precise name that identifies an element in its context.
Add a qualifier only to distinguish real alternatives that would otherwise
be ambiguous. Do not invent a distinction just to justify an adjective.
If there is only one set of options, call it `Options`, not
`Common options`. Avoid gratuitous qualifiers such as `general`,
`common`, `available`, `main`, `default`, or `internal` when
they do not distinguish anything.

This applies everywhere: filenames, directories, types, identifiers,
functions, API names, commands, flags, parameters, configuration keys,
test names, help and output labels, documentation headings, navigation,
UI components and visible prose. Prefer an established plain name
over a longer invented one. A qualifier is correct when it expresses
an actual, necessary distinction; otherwise omit it. Do not use
unrelated renames as incidental cleanup.

Do not encode incidental implementation or representation details into names
when there is no meaningful alternative. For example, use `input`, not
`input_base64`, when there is only one input and the encoding does not
distinguish it from another input; use `Options`, not `Common options`,
when there is only one options section. A qualifier describing a format,
storage mechanism, transport, or role belongs in the name only when it is a
necessary part of the public contract or disambiguates actual alternatives.
Never imply that a second variant exists when it does not.

## Mandatory KCS and interface consistency

KCS and established KaisarCode conventions are mandatory for **all**
implementation artifacts, not merely for tests. Read the current authoritative
KCS rules and the relevant current examples in kcbin and kclib before
writing or changing code. Apply the required details while authoring, not
as deferred formatting or a post-implementation cleanup. Normative KCS rules
take priority over historical code that does not conform.

Inspect separate closest precedents as necessary for each concept, and
reproduce their actual conventions:

* **Tests and output:** use the established named case presentation
    `[current/total] [PASS|FAIL] case_name: description` and the
    `N passed, M failed` summary where that is the applicable C-project
    convention. Verify correct accounting, exit codes, real outputs, and
    contract coverage proportionate to complexity. Do not improvise a new
    runner or output style.
* **CLI/help:** follow actual kclib/kcbin help formatting, usage lines,
    spacing, option names, parameter types, defaults, command syntax,
    stdin/file behavior, output formats, error messages, and exit codes.
    Preserve the contract of each composed kclib instead of redefining it.
* **Public APIs:** mirror established names, data types, argument
    conventions, option structures, return values, error handling,
    lifecycle, ownership, and JS/Lua projections where concepts match.
* **Source conventions:** use standard KaisarCode headers, required
    meaningful DocBlocks and parameter/return metadata, naming, indentation,
    whitespace, and KCS comment rules. No invented internal explanatory
    comments in place of required DocBlocks; non-DocBlock comments are
    prohibited wherever the KCS rules prohibit them.
* **Files and complexity:** follow standard file naming, including **no
    underscores in filenames**. Keep the number of files proportional to
    project responsibility and to comparable projects; use one `src/test.c`
    for ordinary C projects, and do not create extra helper, tokenizer,
    fixture, or private header files solely for code organization.
* **Documentation/build/distribution:** match comparable README, `doc/`
    structure, page naming and navigation, Markdown, manifests, licensing,
    cross-compilation, output layout, and dependency contracts. Do not
    invent alternative conventions where an existing one applies.

A matching concept must use the existing convention. Deviate only for a
concrete technical difference, and never because a different shape would
be easier to write. Review the changed artifacts for these requirements
before committing; do not claim compliance from memory or from a passing
test suite alone.

## Latest-only development contract

Follow the same latest-only policy as the kclibs being composed. During active
development, adjust API and serialized representations until behavior and
contract tests are correct; once consolidated, keep that design stable. Never
introduce legacy formats, old/new compatibility branches, migration readers,
compatibility wrappers, or parallel versioned APIs unless explicitly requested.
The current representation is the only supported one; do not add compatibility
complexity for unreleased or unconsolidated implementations. CLI build-version
reporting is independent from file-format compatibility.

## Project structure

Each independently distributable project owns its `README.md`, `Makefile`, `manifest.json`, `src/`, tests, and LGPLv3 license files (`LICENSE` with the LGPLv3 text and `COPYING` with the accompanying GPLv3 text, matching kclib project conventions). Use `doc/` only when the project requires more documentation. A project may provide a native CLI, shared library, or both, without an obligatory common API.

Source metadata fields are `name`, `description`, `author`, `license`, `website`, `repository`, `level`, and `dependencies`. For LV1, use `"level": 1` and a nonempty `"dependencies"` array of names identifying LV0 kclibs. See `doc/manifest.md`. The distribution script retains source metadata and adds built artifact hashes. The current project-manifest consumer does not yet install declared dependencies automatically; never claim it does.

Use existing `kclib` public APIs according to their README, public headers, and documentation on the selected branch. Do not assume that a kclib catalog is available at a historical path. Any connection between APIs, CLI processes, and .flow files belongs at the composition boundary.

## Build, test, distribution

Follow the kclib monorepo conventions:

- `scripts/build.sh [all|NAME]` invokes each project's `make all`.
- `scripts/test.sh [all|NAME]` runs project-local tests; standard output includes named `[PASS]` cases where applicable.
- `scripts/dist.sh [all|NAME]` publishes project-scoped distribution manifests and verified artifacts into the ignored `dist/`.
- Project build outputs belong in `proj/<name>/bin/<platform>/<arch>/`; source metadata belongs in `proj/<name>/manifest.json`.
- `KCDIST_DIR` may select a shared distribution root; never prune packages belonging to other repositories.

The scripts discover projects by `Makefile`; do not hardcode an inventory. Each project defines its own supported target matrix and link flags.

## WebAssembly JavaScript contract

For a kcbin project supporting `wasm/wasm32`, the public JavaScript
projection must be implemented and shipped together with the WASM binary.
The distribution includes `libNAME.js` (the projected API),
`libNAME.mjs` (the Emscripten runtime), `libNAME.wasm` and the
available link artifacts. `package.json` marks the public JS as an ES
module. `make test wasm` must exercise the JavaScript public API as
well as the native C tests. A raw list of exported C symbols is not a
JavaScript API. Build and test the binding inside the consuming kcbin
project without modifying or rebuilding dependent kclibs.

## Test quality contract

Test quality is a mandatory part of implementing or changing any kcbin project.
Inspect rigorous, comparable test suites in the current kcbin and kclib projects
before authoring tests. Match their depth and conventions, scaled to the actual
capability, complexity, and risks; do not use a fixed test count or pad tests
with superficial assertions.

Test the shipped public interface and real composition, not only individual
helpers or successful startup. Check exact output data, document identities,
ordering, ranking scores where applicable, observable side effects, failure
propagation, and error codes. A passing exit status or correct result count
alone does not establish correctness. For numerical behavior, use independently
determined expected values and explicit tolerances.

Cover applicable happy paths, options and defaults, empty and boundary inputs,
invalid arguments, malformed input, duplicates, lifecycle and cleanup. Verify
persistence, corrupt or truncated files, concurrency, and asynchronous behavior
when the project exposes those capabilities. For CLIs, verify help, stdin/file
input when supported, output format, exit codes, and invalid commands. For
compositions, exercise actual dependent kclib contracts, identifier alignment
and data exchange, missing or incompatible dependency artifacts, and failures
at component boundaries where relevant. Do not rebuild or vendor kclibs to
make tests pass.

Keep suites deterministic, isolated, portable, and self-contained, using the
existing project test layout, normally one `src/test.c` for C projects. Avoid
new fixture frameworks, extra test files, public network dependencies, and
untestable demonstrations. Report cases in the established indexed PASS/FAIL format described above,
accurate aggregate totals, and nonzero failure exit codes. Strengthen any test that
could pass while the contracted behavior is wrong.

Author and update tests in the same implementation increment. Run the
applicable project-local tests when execution is available, fix confirmed
failures, and report precisely what was executed. Never claim unexecuted tests
passed. Detailed platform matrices are proportional to the task, but genuine
contract coverage is never deferrable polish.

## Cross-compilation and linking

Use the same platform/architecture names, cross-compilers, toolchains, SDKs, sysroots and output layout as the relevant kclib projects. An LV1 project declares and builds **only its own supported targets**, and should follow the corresponding kclib Makefiles for their platform configuration.

**Never build, rebuild or vendor LV0 kclib sources as a side effect of a kcbin build.** Consume target-specific precompiled kclib artifacts and matching public headers, using the link mode explicitly selected by the project. Resolve LV0 artifacts from the flat `<platform>-<arch>` directory under `KCDIST_DIR`, not from per-library subdirectories. Build-time dependencies and output publication use the shared distribution root selected by `KCDIST_DIR`. This root may contain both kclib and kcbin packages. Require the exact `platform/arch`; missing or incompatible dependencies must fail with a clear error rather than searching host libraries, silently changing target, or switching link modes.

For dynamic linking, Linux and Android use `.so`, macOS uses `.dylib`, and Windows uses `.dll.a` with the corresponding `.dll` at runtime. For static linking, use the matching precompiled `.a` files for each target. Libraries containing C++ code, such as GGML inside `libemb`, require an appropriate C++ linker even when the consumer sources and public API are C. Other targets, especially iOS and WASM, require confirmation that the selected artifacts support the intended link/runtime model. Do not claim cross-compilation support solely because a kclib has a matching target directory.

The package manifest declares dependency names, while the build selects actual target-specific artifacts. Publish only the LV1 project's own outputs; do not bundle separate kclib artifacts. Distinguish build-time library search paths from deployment-time dynamic loader resolution when dynamic linking is used. A runner/package manager could arrange runtime discovery but is **not currently required or guaranteed**; see `doc/advanced/cross-compilation.md`.

## Source control and documentation

Use the branch selected by the user; do not create branches, tags, or refs without a request. Keep root ignore patterns at the monorepo root. The repository root infrastructure and root tooling are GPLv3; **all** projects under `proj/` are LGPLv3, use `"license": "LGPL-3.0-only"` in their source manifests, and document LGPLv3 in their project READMEs. Do not inherit the repository's GPLv3 license for a nested project.

Keep `README.md` focused on first use; put longer architecture and build details under `doc/`. The canonical project inventory is `doc/catalog.md`, linking to self-contained project README files. Do not create placeholder packages solely to fill the catalog.

The `kc` command and package-manager design are independent, still evolving concerns. Do not make kcbin builds, packages, or manifests require `kc`, and do not describe `kc` as specific to kclibs.
