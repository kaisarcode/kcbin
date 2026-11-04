#!/bin/bash
# kcbin dist tool
# Summary: Publishes kcbin artifacts alongside kclibs by platform-architecture.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

set -euo pipefail

# Resolve the script directory, including chained symlinks.
# @return Absolute directory.
resolve_script_dir() {
    local source="${BASH_SOURCE[0]}" directory link hops=0
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
CHANGED=0
PROJECT_CHANGED=0

# Show the public command interface.
# @return None.
usage() {
    echo "Usage: $0 [all|NAME]" >&2
}

# Resolve the capability of NAME.c or libNAME.c.
# @param directory Project directory name.
# @return Capability on stdout.
capability_name() {
    local name="$1"
    name="${name%.c}"
    printf '%s\n' "${name#lib}"
}

# Accept only supported platform directories.
# @param platform Platform identifier.
# @return 0 when recognized.
is_platform() {
    case "$1" in
        linux|windows|macos|ios|iossim|android|wasm) return 0 ;;
        *) return 1 ;;
    esac
}

# Recognize a self-contained kcbin project.
# @param directory Project path.
# @return 0 when valid.
is_project() {
    [ -d "$1" ] && [ -f "$1/Makefile" ] && [ -f "$1/manifest.json" ]
}

# Resolve a logical project name.
# @param name Capability or project name.
# @return Directory path on stdout.
resolve_project() {
    local name="$1" directory
    for directory in "$PROJ_DIR/$name" "$PROJ_DIR/$name.c" "$PROJ_DIR/lib$name.c"; do
        if is_project "$directory"; then
            printf '%s\n' "$directory"
            return 0
        fi
    done
    return 1
}

# Calculate a portable SHA-256 file digest.
# @param file File path or - for stdin.
# @return Digest on stdout.
compute_sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        openssl dgst -sha256 "$1" | awk '{print $NF}'
    fi
}

# Check whether the artifact belongs to the selected project.
# @param filename Artifact basename.
# @param name Capability.
# @return 0 when project-owned.
owned_artifact() {
    case "$1" in
        "lib$2."*|"$2"|"$2.exe") return 0 ;;
        *) return 1 ;;
    esac
}

# Validate a file against the project's build-generated checksum.
# @param directory Source target directory.
# @param checksum Expected SHA-256.
# @param filename Artifact basename.
# @param name Capability.
# @param platform Target platform.
# @return 0 when verified.
verify_artifact() {
    local directory="$1" expected="$2" filename="$3" name="$4" platform="$5" actual
    if [[ ! "$expected" =~ ^[[:xdigit:]]{64}$ ]] ||
        [[ ! "$filename" =~ ^[A-Za-z0-9_.-]+$ ]] ||
        [[ "$filename" == '.' || "$filename" == '..' ]] ||
        [ ! -f "$directory/$filename" ]; then
        echo "error: invalid or missing artifact: $directory/$filename" >&2
        return 1
    fi
    if ! owned_artifact "$filename" "$name"; then
        if [ "$filename" != package.json ] || [ "$platform" != wasm ]; then
            echo "error: unexpected artifact: $directory/$filename" >&2
            return 1
        fi
    fi
    actual=$(compute_sha256 "$directory/$filename") || return 1
    if [ "$actual" != "$expected" ]; then
        echo "error: build checksum mismatch: $directory/$filename" >&2
        echo "run ./scripts/build.sh $name to refresh SHA256SUMS" >&2
        return 1
    fi
}

# Copy only when bytes differ; shared metadata may never conflict.
# @param source Source file.
# @param destination Published file.
# @param shared 1 for LICENSE, COPYING and package.json.
# @return 0 when unchanged or successfully published.
publish_file() {
    local source="$1" destination="$2" shared="$3" temporary
    if [ -f "$destination" ]; then
        cmp -s "$source" "$destination" && return 0
        if [ "$shared" -eq 1 ]; then
            echo "error: conflicting shared artifact: $destination" >&2
            return 1
        fi
    fi
    mkdir -p "$(dirname "$destination")"
    temporary=$(mktemp "$(dirname "$destination")/.dist.XXXXXXXX") || return 1
    if ! cp "$source" "$temporary" || ! mv -f "$temporary" "$destination"; then
        rm -f "$temporary"
        return 1
    fi
    CHANGED=1
    PROJECT_CHANGED=1
}

# Return whether a basename is listed in a source build checksum.
# @param checksum Checksum file.
# @param filename Exact basename.
# @return 0 if listed.
checksum_has_file() {
    [ -f "$1" ] && awk '{print $2}' "$1" | grep -Fxq "$2"
}

# Publish verified artifacts and one public header into a shared target.
# @param project_dir Source project.
# @param target_dir Built platform/architecture directory.
# @return 0 on success.
publish_target() {
    local project_dir="$1" target_dir="$2" name relative platform arch checksum
    local destination sha filename count=0 header
    name=$(capability_name "$(basename "$project_dir")")
    relative="${target_dir#"$project_dir/bin/"}"
    platform="${relative%%/*}"
    arch="${relative#*/}"
    destination="$DIST_DIR/$platform-$arch"
    checksum="$target_dir/SHA256SUMS"
    header="$project_dir/src/lib$name.h"
    if [ ! -f "$checksum" ] || [ ! -f "$header" ]; then
        echo "error: missing SHA256SUMS or public header for $project_dir/$relative" >&2
        return 1
    fi
    for filename in LICENSE COPYING; do
        if [ ! -f "$project_dir/$filename" ]; then
            echo "error: missing $filename in $project_dir" >&2
            return 1
        fi
    done

    # Validate the entire input list before copying files for this target.
    while read -r sha filename; do
        [ -n "$sha" ] || continue
        case "$filename" in *.h) continue ;; esac
        verify_artifact "$target_dir" "$sha" "$filename" "$name" "$platform"
        [ "$filename" = package.json ] || count=$((count + 1))
    done < "$checksum"
    if [ "$count" -eq 0 ]; then
        echo "error: no build artifacts in $checksum" >&2
        return 1
    fi

    publish_file "$project_dir/LICENSE" "$destination/LICENSE" 1
    publish_file "$project_dir/COPYING" "$destination/COPYING" 1
    while read -r sha filename; do
        [ -n "$sha" ] || continue
        case "$filename" in *.h) continue ;; esac
        if [ "$filename" = package.json ]; then
            publish_file "$target_dir/$filename" "$destination/$filename" 1
        else
            publish_file "$target_dir/$filename" "$destination/$filename" 0
        fi
    done < "$checksum"
    publish_file "$header" "$destination/lib$name.h" 0
}

# Remove only obsolete files owned by one kcbin project from target dirs.
# Foreign kclibs and other kcbin projects are always preserved.
# @param project_dir Source project.
# @return None.
prune_project_files() {
    local project_dir="$1" name destination target platform arch source checksum file filename
    name=$(capability_name "$(basename "$project_dir")")
    for destination in "$DIST_DIR"/*-*/; do
        [ -d "$destination" ] || continue
        target="${destination%/}"
        target="${target##*/}"
        platform="${target%%-*}"
        arch="${target#*-}"
        is_platform "$platform" || continue
        [[ "$arch" =~ ^[A-Za-z0-9_]+$ ]] || continue
        source="$project_dir/bin/$platform/$arch"
        checksum="$source/SHA256SUMS"
        for file in "$destination"*; do
            [ -f "$file" ] || continue
            filename="${file##*/}"
            owned_artifact "$filename" "$name" || continue
            if [ -f "$checksum" ]; then
                if [ "$filename" = "lib$name.h" ] &&
                    [ -f "$project_dir/src/lib$name.h" ]; then
                    continue
                fi
                if checksum_has_file "$checksum" "$filename"; then
                    continue
                fi
            fi
            rm -f "$file"
            CHANGED=1
            PROJECT_CHANGED=1
        done
    done
}

# Remove only old layouts/JSON metadata owned by this project.
# @param project_dir Source project.
# @return None.
prune_legacy_project() {
    local project_dir="$1" project_name name path
    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    for path in "$DIST_DIR/$project_name/manifest.json"; do
        if [ -f "$path" ]; then
            rm -rf "${DIST_DIR:?}/${project_name:?}"
            CHANGED=1
            PROJECT_CHANGED=1
        fi
    done
    for path in "$DIST_DIR/$name.json" "$DIST_DIR/lib$name.json"; do
        if [ -f "$path" ]; then
            rm -f "$path"
            CHANGED=1
            PROJECT_CHANGED=1
        fi
    done
}

# Remove obsolete metadata from a repository-local distribution.
# Shared distribution roots must not remove other repositories' files.
# @return None.
prune_legacy_root() {
    local path project_name
    [ -z "${KCDIST_DIR:-}" ] || return 0
    for path in "$DIST_DIR"/*.json; do
        [ -f "$path" ] || continue
        rm -f "$path"
        CHANGED=1
    done
    for path in "$DIST_DIR"/*.c/; do
        [ -d "$path" ] || continue
        [ -f "$path/manifest.json" ] || continue
        project_name="${path%/}"
        project_name="${project_name##*/}"
        [[ "$project_name" =~ ^[A-Za-z0-9_-]+\.c$ ]] || continue
        rm -rf "${DIST_DIR:?}/${project_name:?}"
        CHANGED=1
    done
    rm -f "$DIST_DIR/.build_state"
}

# Create one complete checksum inventory for each platform-arch directory.
# @return None.
refresh_target_checksums() {
    local destination target platform temporary file filename sha count
    for destination in "$DIST_DIR"/*-*/; do
        [ -d "$destination" ] || continue
        target="${destination%/}"
        target="${target##*/}"
        platform="${target%%-*}"
        is_platform "$platform" || continue
        count=0
        for file in "$destination"*; do
            [ -f "$file" ] || continue
            case "${file##*/}" in
                LICENSE|COPYING|package.json|SHA256SUMS) ;;
                *) count=$((count + 1)) ;;
            esac
        done
        if [ "$count" -eq 0 ]; then
            rm -f "$destination/LICENSE" "$destination/COPYING" \
                "$destination/package.json" "$destination/SHA256SUMS"
            rmdir "$destination" 2>/dev/null || true
            CHANGED=1
            continue
        fi
        temporary=$(mktemp "$destination/.checksum.XXXXXXXX") || return 1
        while IFS= read -r -d '' file; do
            filename="${file##*/}"
            case "$filename" in SHA256SUMS|.checksum.*|.dist.*) continue ;; esac
            sha=$(compute_sha256 "$file") || return 1
            printf '%s  %s\n' "$sha" "$filename"
        done < <(find "$destination" -maxdepth 1 -type f -print0 | LC_ALL=C sort -z) > "$temporary"
        if [ -f "$destination/SHA256SUMS" ] &&
            cmp -s "$temporary" "$destination/SHA256SUMS"; then
            rm -f "$temporary"
        else
            mv -f "$temporary" "$destination/SHA256SUMS"
            CHANGED=1
        fi
    done
}

# Publish one project, then remove only its obsolete artifacts.
# @param project_dir Source project.
# @return None.
package_project() {
    local project_dir="$1" project_name target_dir relative platform arch targets=0
    project_name=$(basename "$project_dir")
    PROJECT_CHANGED=0
    echo "Processing $project_name..."
    for target_dir in "$project_dir"/bin/*/*; do
        [ -d "$target_dir" ] || continue
        relative="${target_dir#"$project_dir/bin/"}"
        platform="${relative%%/*}"
        arch="${relative#*/}"
        is_platform "$platform" || continue
        [[ "$arch" =~ ^[A-Za-z0-9_]+$ ]] || continue
        publish_target "$project_dir" "$target_dir"
        targets=$((targets + 1))
    done
    prune_project_files "$project_dir"
    prune_legacy_project "$project_dir"
    if [ "$targets" -eq 0 ]; then
        echo "    [=] No build artifacts."
    elif [ "$PROJECT_CHANGED" -eq 1 ]; then
        echo "    [+] Distribution updated."
    else
        echo "    [=] Distribution current."
    fi
}

# Distribute one project or every discovered project.
# @param target all or capability name.
# @return 0 on success.
main() {
    local target="${1:-all}" project_dir
    [ "$#" -le 1 ] || { usage; return 1; }
    [ -d "$PROJ_DIR" ] || {
        echo "error: projects directory not found: $PROJ_DIR" >&2
        return 1
    }
    mkdir -p "$DIST_DIR"
    case "$target" in
        all)
            prune_legacy_root
            for project_dir in "$PROJ_DIR"/*/; do
                is_project "$project_dir" || continue
                package_project "${project_dir%/}"
            done
            ;;
        *)
            [[ "$target" =~ ^[A-Za-z0-9_-]+$ ]] || {
                echo "error: invalid project name: $target" >&2
                return 1
            }
            project_dir=$(resolve_project "$target") || {
                echo "error: project not found: $target" >&2
                return 1
            }
            package_project "$project_dir"
            ;;
    esac
    # Recheck the published checksum inventory even on an unchanged run.
    # Identical SHA256SUMS files are retained without rewriting their mtimes.
    refresh_target_checksums
    if [ "$CHANGED" -eq 1 ]; then
        echo "Done. Distribution updated in $DIST_DIR/."
    else
        echo "Done. No distribution changes."
    fi
}

main "$@"
