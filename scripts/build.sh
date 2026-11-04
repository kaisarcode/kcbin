#!/bin/bash
# kcbin build tool
# Summary: Builds one or all kcbin projects with make all.
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

# Prints command usage information.
# @return 0 on success.
usage() {
    echo "Usage: $0 [-l] [all|NAME]" >&2
}

# Resolves one logical kcbin package name to its project directory.
# @param proj_dir Projects directory.
# @param name Logical kcbin package name.
# @return 0 on success, 1 when the project does not exist.
resolve_project() {
    local proj_dir="$1"
    local name="$2"

    if [ -f "$proj_dir/$name/Makefile" ]; then
        printf '%s\n' "$proj_dir/$name"
        return 0
    fi
    if [ -f "$proj_dir/$name.c/Makefile" ]; then
        printf '%s\n' "$proj_dir/$name.c"
        return 0
    fi
    if [ -f "$proj_dir/lib$name.c/Makefile" ]; then
        printf '%s\n' "$proj_dir/lib$name.c"
        return 0
    fi

    return 1
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
    elif [[ "$project_name" == *.c ]]; then
        printf '%s\n' "${project_name%.c}"
    else
        printf '%s\n' "$project_name"
    fi
}

# Computes the SHA-256 digest of one file.
# @param file File path.
# @return 0 on success; the digest is written to stdout.
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

# Writes the artifact file list for one compiled target.
# @param project_dir Project directory.
# @param target_dir Compiled target directory.
# @param output_file Destination list file.
# @return 0 on success.
write_target_file_list() {
    local project_dir="$1"
    local target_dir="$2"
    local output_file="$3"
    local project_name name

    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")

    find "$target_dir" -maxdepth 1 -type f \
        \( -name "lib$name.*" -o -name "$name" -o -name "$name.exe" -o -name "package.json" \) \
        ! -name "*.sync-conflict-*" \
        -printf '%f\n' | LC_ALL=C sort -u > "$output_file"
}

# Checks whether one target checksum is still current.
# @param project_dir Project directory.
# @param target_dir Compiled target directory.
# @return 0 when current, 1 otherwise.
target_checksum_is_current() {
    local project_dir="$1"
    local target_dir="$2"
    local project_name name checksum current_list stored_list

    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    checksum="$target_dir/SHA256SUMS"

    [ -f "$checksum" ] || return 1

    if find "$target_dir" -maxdepth 1 -type f \
        \( -name "lib$name.*" -o -name "$name" -o -name "$name.exe" -o -name "package.json" \) \
        ! -name "*.sync-conflict-*" \
        -newer "$checksum" -print -quit | grep -q .; then
        return 1
    fi

    current_list=$(mktemp)
    stored_list=$(mktemp)
    write_target_file_list "$project_dir" "$target_dir" "$current_list"
    awk '{print $2}' "$checksum" | LC_ALL=C sort -u > "$stored_list"

    if cmp -s "$current_list" "$stored_list"; then
        rm -f "$current_list" "$stored_list"
        return 0
    fi

    rm -f "$current_list" "$stored_list"
    return 1
}

# Updates one target checksum only when its artifacts changed.
# @param project_dir Project directory.
# @param target_dir Compiled target directory.
# @return 0 on success.
update_target_checksum() {
    local project_dir="$1"
    local target_dir="$2"
    local project_name name checksum temporary artifact
    local filename sha256

    if target_checksum_is_current "$project_dir" "$target_dir"; then
        return 0
    fi

    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    checksum="$target_dir/SHA256SUMS"
    temporary=$(mktemp)

    while IFS= read -r -d '' artifact; do
        filename=$(basename "$artifact")
        sha256=$(compute_sha256 "$artifact")
        printf '%s  %s\n' "$sha256" "$filename" >> "$temporary"
    done < <(
        find "$target_dir" -maxdepth 1 -type f \
            \( -name "lib$name.*" -o -name "$name" -o -name "$name.exe" -o -name "package.json" \) \
            ! -name "*.sync-conflict-*" \
            -print0 | sort -z
    )

    LC_ALL=C sort -u -k2,2 "$temporary" > "$checksum"
    rm -f "$temporary"
}

# Returns success for supported output platform directory names.
# @param platform First component beneath bin/.
# @return 0 for a known platform, 1 otherwise.
is_platform() {
    case "$1" in
        linux|windows|macos|ios|iossim|android|wasm) return 0 ;;
        *) return 1 ;;
    esac
}

# Updates checksums for all compiled targets of one project.
# @param project_dir Project directory.
# @return 0 on success.
update_project_checksums() {
    local project_dir="$1"
    local target_dir relative platform

    project_dir="${project_dir%/}"
    [ -d "$project_dir/bin" ] || return 0

    for target_dir in "$project_dir"/bin/*/*; do
        [ -d "$target_dir" ] || continue
        relative="${target_dir#"$project_dir/bin/"}"
        platform="${relative%%/*}"
        is_platform "$platform" || continue
        update_target_checksum "$project_dir" "$target_dir"
    done
}

# Counts the targets declared by make all without executing their recipes.
# @param project_dir Project directory.
# @return 0 on success; the target count is written to stdout.
count_build_steps() {
    local project_dir="$1"
    local count

    count=$(
        cd "$project_dir"
        make -qp all 2>/dev/null | awk '
            /^all:([[:space:]]|$)/ {
                for (i = 2; i <= NF; i++) {
                    if ($i != "|" && $i != ".WAIT") {
                        count++
                    }
                }
                print count
                exit
            }
        '
    )

    if [[ "$count" =~ ^[1-9][0-9]*$ ]]; then
        printf '%s\n' "$count"
    else
        printf '1\n'
    fi
}

# Formats build output as logs, an in-place status, or CI-friendly lines.
# @param project_name Project directory basename.
# @param step_index First target position for the project.
# @param step_count Total number of targets.
# @param last_step Last target position for the project.
# @param output_mode verbose, tty, or plain.
# @param columns Available terminal columns.
# @return 0 on success.
format_build_output() {
    awk -v project_name="$1" \
        -v step_index="$2" \
        -v step_count="$3" \
        -v last_step="$4" \
        -v output_mode="$5" \
        -v columns="$6" '
            function show_status(detail,    line) {
                sub(/\/[^[:space:]]*\/bin\//, "bin/", detail)
                gsub(/\r/, "", detail)
                line = sprintf("[%d/%d] %s: %s", step_index,
                    step_count, project_name, detail)
                if (columns > 1 && length(line) >= columns) {
                    line = substr(line, 1, columns - 1)
                }
                printf "\r\033[2K%s", line
            }
            {
                if (output_mode == "verbose") {
                    printf "[%d/%d] %s: %s\n", step_index,
                        step_count, project_name, $0
                } else if (output_mode == "tty") {
                    show_status($0)
                } else if ($0 ~ /^OK [^[:space:]]+\/[^[:space:]]+$/) {
                    printf "[%d/%d] %s: %s\n", step_index,
                        step_count, project_name, $0
                }

                if ($0 ~ /^OK [^[:space:]]+\/[^[:space:]]+$/ &&
                    step_index < last_step) {
                    step_index++
                }
                fflush()
            }
        '
}

# Builds one kclib project for every configured target.
# @param project_dir Project directory.
# @param step_index One-based index of the first target for this project.
# @param step_count Total targets in the build run.
# @param project_steps Number of targets for this project.
# @param output_mode verbose, tty, or plain.
# @param columns Available terminal columns.
# @return 0 on success.
build_project() {
    local project_dir="$1"
    local step_index="$2"
    local step_count="$3"
    local project_steps="$4"
    local output_mode="$5"
    local columns="$6"
    local project_name status build_log

    project_dir="${project_dir%/}"
    project_name=$(basename "$project_dir")

    if [ "$output_mode" = "tty" ]; then
        printf '\r\033[2K[%d/%d] %s: building' \
            "$step_index" "$step_count" "$project_name"
    else
        echo "[$step_index/$step_count] $project_name: building"
    fi

    build_log=""
    if [ "$output_mode" != "verbose" ]; then
        build_log=$(mktemp)
    fi

    status=0
    (
        cd "$project_dir"
        set -o pipefail
        if [ "$output_mode" = "verbose" ]; then
            make all 2>&1 | format_build_output \
                "$project_name" "$step_index" "$step_count" \
                "$((step_index + project_steps - 1))" "$output_mode" "$columns"
        else
            make all 2>&1 | tee "$build_log" | format_build_output \
                "$project_name" "$step_index" "$step_count" \
                "$((step_index + project_steps - 1))" "$output_mode" "$columns"
        fi
    ) || status=$?

    if [ "$status" -ne 0 ]; then
        if [ "$output_mode" = "tty" ]; then
            printf '\r\033[2K'
        fi
        if [ -n "$build_log" ]; then
            printf '[%d/%d] %s: build failed\n' \
                "$step_index" "$step_count" "$project_name" >&2
            cat "$build_log" >&2
            rm -f "$build_log"
        fi
        return "$status"
    fi

    if [ -n "$build_log" ]; then
        rm -f "$build_log"
    fi
    update_project_checksums "$project_dir"

    if [ "$output_mode" = "verbose" ]; then
        printf '\n'
    fi
}

# Dispatches one project build or all project builds.
# @param target Logical kcbin package name or all.
# @return 0 on success.
main() {
    local script_dir root_dir proj_dir target project_dir project_name
    local failed_projects step_count step_index steps i output_mode terminal_cols
    local -a project_dirs project_steps

    target=""
    output_mode="compact"
    while [ "$#" -gt 0 ]; do
        case "$1" in
            -l)
                [ "$output_mode" = "compact" ] || { usage; return 1; }
                output_mode="verbose"
                ;;
            *)
                [ -z "$target" ] || { usage; return 1; }
                target="$1"
                ;;
        esac
        shift
    done

    target="${target:-all}"

    terminal_cols=80
    if [ "$output_mode" = "compact" ]; then
        if [ -t 1 ] && [ "${TERM:-dumb}" != "dumb" ]; then
            output_mode="tty"
            terminal_cols=$(tput cols 2>/dev/null || true)
            case "$terminal_cols" in
                ''|*[!0-9]*) terminal_cols=80 ;;
            esac
            if [ "$terminal_cols" -lt 12 ]; then
                terminal_cols=80
            fi
        else
            output_mode="plain"
        fi
    fi

    script_dir=$(resolve_script_dir)
    root_dir=$(dirname "$script_dir")
    proj_dir="$root_dir/proj"

    [ -d "$proj_dir" ] || {
        echo "error: projects directory not found: $proj_dir" >&2
        return 1
    }

    if [ "$target" = "all" ]; then
        failed_projects=""
        project_dirs=()
        project_steps=()
        step_count=0
        for project_dir in "$proj_dir"/*; do
            [ -f "$project_dir/Makefile" ] || continue
            steps=$(count_build_steps "$project_dir")
            project_dirs+=("$project_dir")
            project_steps+=("$steps")
            step_count=$((step_count + steps))
        done

        step_index=1
        for i in "${!project_dirs[@]}"; do
            project_dir="${project_dirs[$i]}"
            steps="${project_steps[$i]}"
            if ! build_project "$project_dir" "$step_index" "$step_count" \
                "$steps" "$output_mode" "$terminal_cols"; then
                project_name=$(basename "$project_dir")
                failed_projects="$failed_projects $project_name"
            fi
            step_index=$((step_index + steps))
        done

        if [ -n "$failed_projects" ]; then
            if [ "$output_mode" = "tty" ]; then
                printf '\r\033[2K'
            fi
            echo "error: failed projects:$failed_projects" >&2
            return 1
        fi
        if [ "$output_mode" = "tty" ]; then
            printf '\r\033[2K[%d/%d] Build complete\n' \
                "$step_count" "$step_count"
        fi
        return 0
    fi

    case "$target" in
        *[!A-Za-z0-9_-]*|'')
            echo "error: invalid project name: $target" >&2
            usage
            return 1
            ;;
    esac

    project_dir=$(resolve_project "$proj_dir" "$target") || {
        echo "error: project not found: $target" >&2
        return 1
    }

    steps=$(count_build_steps "$project_dir")
    build_project "$project_dir" 1 "$steps" "$steps" \
        "$output_mode" "$terminal_cols" || return $?

    if [ "$output_mode" = "tty" ]; then
        printf '\r\033[2K[%d/%d] %s: build complete\n' \
            "$steps" "$steps" "$(basename "$project_dir")"
    fi
}

main "$@"
