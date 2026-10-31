#!/bin/bash
# kcbin dist tool
# Summary: Publishes individual package artifacts with checksums and a manifest.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

set -e

# Resolves the real script directory, including relative and chained symlinks.
# @return Absolute directory containing the script.
resolve_script_dir() {
    local source="${BASH_SOURCE[0]}"
    local directory link hops=0

    while [ -L "$source" ]; do
        directory=$(cd -P "$(dirname "$source")" && pwd) || return 1
        link=$(readlink "$source") || return 1
        case "$link" in
            /*) source="$link" ;;
            *) source="$directory/$link" ;;
        esac
        hops=$((hops + 1))
        if [ "$hops" -gt 40 ]; then
            echo "error: too many script symlinks" >&2
            return 1
        fi
    done

    cd -P "$(dirname "$source")" && pwd
}

SCRIPT_DIR=$(resolve_script_dir)
ROOT_DIR=$(dirname "$SCRIPT_DIR")
PROJ_DIR="$ROOT_DIR/proj"
DIST_DIR="${KCDIST_DIR:-$ROOT_DIR/dist}"
readonly EXCLUDED_PROJECTS=()
declare -A MANIFEST_SHA=()
declare -A MANIFEST_BUILD_SHA=()
declare -A MANIFEST_LICENSE_SHA=()
declare -A MANIFEST_README_SHA=()
declare -A MANIFEST_COUNT=()

# Prints command usage information.
# @return 0 on success.
usage() {
    echo "Usage: $0 [all|NAME]" >&2
}

# Returns the logical capability name for a libNAME.c project.
# @param project_name Project directory basename.
# @return 0 on success; the capability name is written to stdout.
capability_name() {
    local project_name="$1"
    local stem

    if [[ "$project_name" == lib*.c ]]; then
        stem="${project_name%.c}"
        printf '%s\n' "${stem#lib}"
    else
        printf '%s\n' "$project_name"
    fi
}

# Checks whether a project is excluded from distribution.
# @param project_name Project directory basename.
# @return 0 when excluded, 1 otherwise.
is_excluded() {
    local project_name="$1"
    local name

    for name in "${EXCLUDED_PROJECTS[@]}"; do
        if [ "$project_name" = "$name" ]; then
            return 0
        fi
    done
    return 1
}

# Checks whether a directory name is a supported target platform.
# @param platform Directory name beneath bin/ or dist/libNAME.c/.
# @return 0 when recognized, 1 otherwise.
is_platform() {
    case "$1" in
        linux|windows|macos|ios|iossim|android|wasm) return 0 ;;
        *) return 1 ;;
    esac
}

# Checks whether a path is a current kcbin project.
# @param project_path Project directory path.
# @return 0 when current, 1 otherwise.
is_project() {
    local project_path="$1"
    local project_name

    [ -d "$project_path" ] || return 1
    [ -f "$project_path/Makefile" ] || return 1
    [ -f "$project_path/manifest.json" ] || return 1

    project_name=$(basename "$project_path")
    if is_excluded "$project_name"; then
        return 1
    fi

    return 0
}

# Resolves one logical capability name to its project directory.
# @param proj_dir Projects directory.
# @param name Logical capability name.
# @return 0 on success, 1 when the project does not exist.
resolve_project() {
    local proj_dir="$1"
    local name="$2"
    local project_dir

    project_dir="$proj_dir/$name"
    if is_project "$project_dir"; then
        printf '%s\n' "$project_dir"
        return 0
    fi
    project_dir="$proj_dir/lib$name.c"
    if is_project "$project_dir"; then
        printf '%s\n' "$project_dir"
        return 0
    fi

    return 1
}

# Returns the SHA-256 checksum of one file.
# @param file File path.
# @return Checksum on stdout.
compute_sha256() {
    local file="$1"

    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$file" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$file" | awk '{print $1}'
    else
        openssl dgst -sha256 "$file" | awk '{print $NF}'
    fi
}

# Returns a stable SHA-256 digest of the available project license files.
# @param project_dir Project directory.
# @return Digest changes when LICENSE or COPYING changes or is removed.
compute_license_sha256() {
    local project_dir="$1"
    local file

    (
        cd "$project_dir" || exit 1
        for file in LICENSE COPYING; do
            [ -f "$file" ] || continue
            printf '%s  ' "$file"
            compute_sha256 "$file" || exit 1
        done
    ) | compute_sha256 -
}

# Writes the minimal README included with each published target.
# @param name Project capability name without the lib prefix.
# @return 0 on success.
write_dist_readme() {
    local project_dir="$1"
    local name
    name=$(capability_name "$(basename "$project_dir")")
    if [[ "$(basename "$project_dir")" == lib*.c ]]; then
        printf '# lib%s.c\n\nAuthor: Kaisarcode\nWebsite: https://kaisarcode.com/\n' "$name"
    else
        cat "$project_dir/README.md"
    fi
}

# Maps a built target to its published platform/architecture directory.
# @param project_dir Project directory.
# @param target_dir Built target directory.
# @return Distribution directory on stdout.
dist_target_path() {
    local project_dir="$1"
    local target_dir="$2"
    local project_name relative

    project_dir="${project_dir%/}"
    project_name=$(basename "$project_dir")
    relative="${target_dir#"$project_dir/bin/"}"
    printf '%s/%s/%s\n' "$DIST_DIR" "$project_name" "$relative"
}

# Loads published file and source checksums for incremental distribution.
# @return 0 on success.
load_manifest_state() {
    local manifest sha build_sha license_sha readme_sha path prefix

    MANIFEST_SHA=()
    MANIFEST_BUILD_SHA=()
    MANIFEST_LICENSE_SHA=()
    MANIFEST_README_SHA=()
    MANIFEST_COUNT=()
    local -a manifests=()
    for manifest in "$@"; do
        [ -f "$PROJ_DIR/$(basename "$(dirname "$manifest")")/Makefile" ] || continue
        [ -f "$manifest" ] || continue
        manifests+=("$manifest")
    done
    [ "${#manifests[@]}" -gt 0 ] || return 0

    while IFS=$'\t' read -r sha build_sha license_sha readme_sha path; do
        [ -n "$sha" ] || continue
        [ -n "$path" ] || continue
        prefix="${path%/*}"
        MANIFEST_SHA["$path"]="$sha"
        MANIFEST_BUILD_SHA["$prefix"]="$build_sha"
        MANIFEST_LICENSE_SHA["$prefix"]="$license_sha"
        MANIFEST_README_SHA["$prefix"]="$readme_sha"
        MANIFEST_COUNT["$prefix"]=$((${MANIFEST_COUNT["$prefix"]:-0} + 1))
    done < <(
        awk '
            /"sha256":/ {
                sha = $0
                sub(/^.*"sha256": "/, "", sha)
                sub(/".*$/, "", sha)
            }
            /"build_sha256":/ {
                build_sha = $0
                sub(/^.*"build_sha256": "/, "", build_sha)
                sub(/".*$/, "", build_sha)
            }
            /"license_sha256":/ {
                license_sha = $0
                sub(/^.*"license_sha256": "/, "", license_sha)
                sub(/".*$/, "", license_sha)
            }
            /"readme_sha256":/ {
                readme_sha = $0
                sub(/^.*"readme_sha256": "/, "", readme_sha)
                sub(/".*$/, "", readme_sha)
            }
            /"path":/ {
                path = $0
                sub(/^.*"path": "/, "", path)
                sub(/".*$/, "", path)
                if (sha != "" && path != "") {
                    printf "%s\t%s\t%s\t%s\t%s\n", sha, build_sha, license_sha, readme_sha, path
                }
                sha = ""
                build_sha = ""
                license_sha = ""
                readme_sha = ""
            }
        ' "${manifests[@]}"
    )
}

# Removes metadata from the former, unpacked distribution layout.
# @return 0 on success.
remove_legacy_metadata() {
    [ -d "$DIST_DIR" ] || return 0
    rm -f "$DIST_DIR/.build_state"
}

# Checks whether a target's published files and source inputs are current.
# @param project_dir Project directory.
# @param target_dir Built target directory.
# @return 0 when current, 1 when changed, 2 on error.
dist_target_is_current() {
    local project_dir="$1"
    local target_dir="$2"
    local project_name name header dist_target prefix checksum
    local build_sha license_sha readme_sha file path actual_sha count

    project_dir="${project_dir%/}"
    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    header="$project_dir/src/lib$name.h"
    checksum="$target_dir/SHA256SUMS"
    if [ ! -f "$checksum" ]; then
        echo "error: build checksum not found: $checksum" >&2
        echo "run ./scripts/build.sh $name" >&2
        return 2
    fi
    if [[ "$project_name" == lib*.c ]] && [ ! -f "$header" ]; then
        echo "error: public header not found: $header" >&2
        return 2
    fi

    dist_target=$(dist_target_path "$project_dir" "$target_dir")
    prefix="${dist_target#"$DIST_DIR/"}"
    [ -d "$dist_target" ] || return 1

    build_sha=$(compute_sha256 "$checksum") || return 2
    [ "${MANIFEST_BUILD_SHA["$prefix"]:-}" = "$build_sha" ] || return 1

    license_sha=$(compute_license_sha256 "$project_dir") || return 2
    [ "${MANIFEST_LICENSE_SHA["$prefix"]:-}" = "$license_sha" ] || return 1

    readme_sha=$(write_dist_readme "$project_dir" | compute_sha256 -) || return 2
    [ "${MANIFEST_README_SHA["$prefix"]:-}" = "$readme_sha" ] || return 1

    if [ -f "$header" ]; then
        actual_sha=$(compute_sha256 "$header") || return 2
        [ "${MANIFEST_SHA["$prefix/lib$name.h"]:-}" = "$actual_sha" ] || return 1
    fi

    count=0
    for file in "$dist_target"/*; do
        [ -f "$file" ] || continue
        path="$prefix/${file##*/}"
        actual_sha=$(compute_sha256 "$file") || return 2
        [ "${MANIFEST_SHA["$path"]:-}" = "$actual_sha" ] || return 1
        count=$((count + 1))
    done

    [ "$count" -gt 0 ] || return 1
    [ "$count" -eq "${MANIFEST_COUNT["$prefix"]:-0}" ]
}

# Publishes one target's binaries and metadata as separate files.
# @param project_dir Project directory.
# @param target_dir Built target directory.
# @return 0 on success, 1 on failure.
sync_target() {
    local project_dir="$1"
    local target_dir="$2"
    local project_name name header dist_target staging
    local file sha actual_sha count

    project_dir="${project_dir%/}"
    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    header="$project_dir/src/lib$name.h"
    dist_target=$(dist_target_path "$project_dir" "$target_dir")
    if [[ "$project_name" == lib*.c ]] && [ ! -f "$header" ]; then
        echo "error: public header not found: $header" >&2
        return 1
    fi

    mkdir -p "$(dirname "$dist_target")" || return 1
    staging=$(mktemp -d "$(dirname "$dist_target")/.dist.XXXXXXXX") || return 1
    count=0
    : > "$staging/SHA256SUMS"
    while read -r sha file; do
        [ -n "$sha" ] || continue
        [ -n "$file" ] || continue
        case "$file" in
            *.h) continue ;;
            "lib$name."*|"$name"|"$name.exe") ;;
            *)
                echo "error: unexpected build artifact: $file" >&2
                rm -rf "$staging"
                return 1
                ;;
        esac
        if [ ! -f "$target_dir/$file" ]; then
            echo "error: build artifact not found: $target_dir/$file" >&2
            rm -rf "$staging"
            return 1
        fi
        if ! cp "$target_dir/$file" "$staging/$file"; then
            rm -rf "$staging"
            return 1
        fi
        actual_sha=$(compute_sha256 "$staging/$file") || {
            rm -rf "$staging"
            return 1
        }
        if [ "$actual_sha" != "$sha" ]; then
            echo "error: build checksum mismatch: $target_dir/$file" >&2
            echo "run ./scripts/build.sh $name to refresh SHA256SUMS" >&2
            rm -rf "$staging"
            return 1
        fi
        printf '%s  %s\n' "$sha" "$file" >> "$staging/SHA256SUMS"
        count=$((count + 1))
    done < "$target_dir/SHA256SUMS"

    if [ "$count" -eq 0 ]; then
        echo "error: no build artifacts to publish: $target_dir" >&2
        rm -rf "$staging"
        return 1
    fi

    if [ -f "$header" ]; then
        if ! cp "$header" "$staging/lib$name.h"; then
            rm -rf "$staging"
            return 1
        fi
        actual_sha=$(compute_sha256 "$staging/lib$name.h") || {
            rm -rf "$staging"
            return 1
        }
        printf '%s  lib%s.h\n' "$actual_sha" "$name" >> "$staging/SHA256SUMS"
    fi

    for file in LICENSE COPYING; do
        [ -f "$project_dir/$file" ] || continue
        if ! cp "$project_dir/$file" "$staging/$file"; then
            rm -rf "$staging"
            return 1
        fi
        actual_sha=$(compute_sha256 "$staging/$file") || {
            rm -rf "$staging"
            return 1
        }
        printf '%s  %s\n' "$actual_sha" "$file" >> "$staging/SHA256SUMS"
    done

    if ! write_dist_readme "$project_dir" > "$staging/README.md"; then
        rm -rf "$staging"
        return 1
    fi
    actual_sha=$(compute_sha256 "$staging/README.md") || {
        rm -rf "$staging"
        return 1
    }
    printf '%s  README.md\n' "$actual_sha" >> "$staging/SHA256SUMS"

    rm -rf "$dist_target"
    if ! mv "$staging" "$dist_target"; then
        rm -rf "$staging"
        return 1
    fi
    echo "    [+] ${dist_target#"$DIST_DIR/"}"
}

# Removes old ZIPs and target directories without source builds.
# @param project_dir Project directory.
# @return 0 when unchanged, 1 when stale targets were removed.
prune_stale_targets() {
    local project_dir="$1"
    local project_name name project_dist archive platform_dir arch_dir
    local platform arch changed

    project_dir="${project_dir%/}"
    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    project_dist="$DIST_DIR/$project_name"
    [ -d "$project_dist" ] || return 0

    changed=0
    for archive in "$project_dist"/lib"$name"-*.zip; do
        [ -f "$archive" ] || continue
        rm -f "$archive"
        changed=1
    done

    for platform_dir in "$project_dist"/*/; do
        [ -d "$platform_dir" ] || continue
        platform=$(basename "$platform_dir")
        if ! is_platform "$platform"; then
            rm -rf "$platform_dir"
            changed=1
            continue
        fi
        for arch_dir in "$platform_dir"*/; do
            [ -d "$arch_dir" ] || continue
            arch=$(basename "$arch_dir")
            if [ ! -d "$project_dir/bin/$platform/$arch" ]; then
                rm -rf "$arch_dir"
                changed=1
            fi
        done
        rmdir "$platform_dir" 2>/dev/null || true
    done

    return "$changed"
}

# Synchronizes one project using build-generated artifact checksums.
# @param project_dir Project directory.
# @return 0 when changed, 1 when no work was needed.
package_project() {
    local project_dir="$1"
    local project_name target_dir relative platform status changed stale_changed

    project_dir="${project_dir%/}"
    project_name=$(basename "$project_dir")

    echo "Processing $project_name..."
    changed=0

    if [ -d "$project_dir/bin" ]; then
        for target_dir in "$project_dir"/bin/*/*; do
            [ -d "$target_dir" ] || continue
            relative="${target_dir#"$project_dir/bin/"}"
            platform="${relative%%/*}"
            is_platform "$platform" || continue

            status=0
            dist_target_is_current "$project_dir" "$target_dir" || status=$?
            case "$status" in
                0) continue ;;
                1)
                    sync_target "$project_dir" "$target_dir" || return $?
                    changed=1
                    ;;
                *) return "$status" ;;
            esac
        done
    else
        echo "    [=] No build artifacts."
    fi

    stale_changed=0
    if ! prune_stale_targets "$project_dir"; then
        stale_changed=1
    fi

    if [ "$changed" -eq 1 ] || [ "$stale_changed" -eq 1 ]; then
        echo "    [+] Distribution updated."
        return 0
    fi

    echo "    [=] Distribution current."
    return 1
}

# Removes distributions for projects that no longer exist.
# @return 0 when unchanged, 1 when stale distributions were removed.
prune_stale_projects() {
    local project_dist project_name source_project changed

    # A shared distribution may contain projects owned by other repositories.
    [ -z "${KCDIST_DIR:-}" ] || return 0
    changed=0
    for project_dist in "$DIST_DIR"/*/; do
        [ -d "$project_dist" ] || continue
        project_name=$(basename "$project_dist")
        source_project="$PROJ_DIR/$project_name"

        if ! is_project "$source_project"; then
            echo "Removing stale $project_name..."
            rm -rf "$project_dist"
            changed=1
        fi
    done

    return "$changed"
}

# Builds one published manifest for each independent kcbin package.
# The source manifest supplies stable GUI-facing metadata; artifact entries
# are generated from the actual published files and their checksums.
# @return 0 when all per-project manifests have been written.
generate_manifest() {
    local project_dir project_name project_dist metadata_file manifest_file
    local name target_dir dist_target relative platform arch checksum
    local build_sha license_sha readme_sha file filename file_sha filesize
    local updated_at first_file metadata_sha

    updated_at=$(date -u +'%Y-%m-%dT%H:%M:%SZ')
    for project_dir in "$@"; do
        is_project "$project_dir" || continue
        project_dir="${project_dir%/}"
        project_name=$(basename "$project_dir")
        name=$(capability_name "$project_name")
        project_dist="$DIST_DIR/$project_name"
        [ -d "$project_dist" ] || continue

        metadata_file="$project_dir/manifest.json"
        if [ ! -f "$metadata_file" ]; then
            echo "error: project metadata not found: $metadata_file" >&2
            return 1
        fi
        metadata_sha=$(compute_sha256 "$metadata_file") || return 1

        manifest_file=$(mktemp "$project_dist/.manifest.XXXXXXXX") || return 1
        {
            printf '{\n  "metadata": '
            cat "$metadata_file"
            printf ',\n'
            printf '  "metadata_sha256": "%s",\n' "$metadata_sha"
            printf '  "updated_at": "%s",\n' "$updated_at"
            printf '  "timestamp": %s,\n' "$(date -u +%s)"
            printf '  "artifacts": [\n'
        } > "$manifest_file"

        first_file=true
        for target_dir in "$project_dir"/bin/*/*; do
            [ -d "$target_dir" ] || continue
            relative="${target_dir#"$project_dir/bin/"}"
            IFS="/" read -r platform arch <<< "$relative"
            is_platform "$platform" || continue
            dist_target="$project_dist/$relative"
            [ -d "$dist_target" ] || continue
            checksum="$target_dir/SHA256SUMS"
            if [ ! -f "$checksum" ]; then
                echo "error: build checksum not found: $checksum" >&2
                rm -f "$manifest_file"
                return 1
            fi
            build_sha=$(compute_sha256 "$checksum") || {
                rm -f "$manifest_file"
                return 1
            }
            license_sha=$(compute_license_sha256 "$project_dir") || {
                rm -f "$manifest_file"
                return 1
            }
            readme_sha=$(write_dist_readme "$project_dir" | compute_sha256 -) || {
                rm -f "$manifest_file"
                return 1
            }

            for file in "$dist_target"/*; do
                [ -f "$file" ] || continue
                filename=$(basename "$file")
                file_sha=$(compute_sha256 "$file") || {
                    rm -f "$manifest_file"
                    return 1
                }
                filesize=$(stat -c%s "$file" 2>/dev/null ||
                    stat -f%z "$file" 2>/dev/null) || {
                    rm -f "$manifest_file"
                    return 1
                }
                if [ "$first_file" = true ]; then
                    first_file=false
                else
                    echo "," >> "$manifest_file"
                fi
                cat <<MANIFEST_ENTRY >> "$manifest_file"
    {
      "arch": "$arch",
      "platform": "$platform",
      "file": "$filename",
      "size_bytes": $filesize,
      "sha256": "$file_sha",
      "build_sha256": "$build_sha",
      "license_sha256": "$license_sha",
      "readme_sha256": "$readme_sha",
      "path": "$project_name/$relative/$filename"
    }
MANIFEST_ENTRY
            done
        done
        printf '\n  ]\n}\n' >> "$manifest_file"
        mv -f "$manifest_file" "$project_dist/manifest.json" || return 1
    done

}

# Detects projects with files published but no per-project manifest.
# @return 0 when every published kclib has its own manifest.
project_manifests_current() {
    local project_dir project_name metadata_sha manifest
    for project_dir in "$@"; do
        is_project "$project_dir" || continue
        project_dir="${project_dir%/}"
        project_name=$(basename "$project_dir")
        [ -d "$DIST_DIR/$project_name" ] || continue
        manifest="$DIST_DIR/$project_name/manifest.json"
        [ -f "$manifest" ] || return 1
        metadata_sha=$(compute_sha256 "$project_dir/manifest.json") || return 1
        grep -Fq "\"metadata_sha256\": \"$metadata_sha\"" "$manifest" ||
            return 1
    done
    return 0
}

# Dispatches one project distribution or all project distributions.
# @param target Logical capability name or all.
# @return 0 on success.
main() {
    local target project_dir status changed stale_changed

    [ "$#" -le 1 ] || {
        usage
        exit 1
    }

    target="${1:-all}"
    [ -d "$PROJ_DIR" ] || {
        echo "error: projects directory not found: $PROJ_DIR" >&2
        exit 1
    }

    mkdir -p "$DIST_DIR"

    if [ "$target" = "all" ]; then
        remove_legacy_metadata
        if [ -z "${KCDIST_DIR:-}" ]; then
            rm -f "$DIST_DIR/manifest.json"
        fi
        load_manifest_state "$DIST_DIR"/*/manifest.json
        stale_changed=0
        if ! prune_stale_projects; then
            stale_changed=1
        fi
        changed="$stale_changed"

        for project_dir in "$PROJ_DIR"/*/; do
            is_project "$project_dir" || continue

            status=0
            package_project "$project_dir" || status=$?
            case "$status" in
                0) changed=1 ;;
                1) ;;
                *) exit "$status" ;;
            esac
        done

        if [ "$changed" -eq 1 ] || ! project_manifests_current "$PROJ_DIR"/*/; then
            echo "Generating project manifests..."
            generate_manifest "$PROJ_DIR"/*/
            echo "Done. Project distributions updated in $DIST_DIR/."
        else
            echo "Done. No distribution changes."
        fi
        return 0
    fi

    case "$target" in
        *[!A-Za-z0-9_-]*|'')
            echo "error: invalid project name: $target" >&2
            usage
            exit 1
            ;;
    esac

    project_dir=$(resolve_project "$PROJ_DIR" "$target") || {
        echo "error: project not found: $target" >&2
        exit 1
    }

    load_manifest_state "$DIST_DIR/$(basename "$project_dir")/manifest.json"

    status=0
    package_project "$project_dir" || status=$?
    case "$status" in
        0) changed=1 ;;
        1) changed=0 ;;
        *) exit "$status" ;;
    esac

    if [ "$changed" -eq 1 ] || ! project_manifests_current "$project_dir"; then
        echo "Generating project manifest for $(basename "$project_dir")..."
        generate_manifest "$project_dir"
        echo "Done. Distribution updated in" \
            "$DIST_DIR/$(basename "$project_dir")/."
    else
        echo "Done. No distribution changes."
    fi
}

main "$@"
