#!/bin/bash
# kcbin test tool
# Summary: Tests one or all kcbin projects natively, through Wine, and through WASM when supported.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

set -eo pipefail

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

# Counts test runtimes declared by one kcbin project.
# @param project_dir Project directory.
# @return Runtime count (native, Wine and optionally WASM).
count_test_steps() {
    local project_dir="$1"

    if grep -Eq '^wasm[[:space:]]*:' "$project_dir/Makefile"; then
        printf '3\n'
    else
        printf '2\n'
    fi
}

# Shows complete output, an in-place status, or one summary per runtime.
# @param project_name Project directory basename.
# @param runtime Test runtime name.
# @param step_index One-based runtime position.
# @param step_count Total runtimes in this invocation.
# @param output_mode verbose, tty, or plain.
# @param columns Available terminal columns.
# @return 0 on success.
format_test_output() {
    awk -v project_name="$1" \
        -v runtime="$2" \
        -v step_index="$3" \
        -v step_count="$4" \
        -v output_mode="$5" \
        -v columns="$6" '
            function show_status(detail,    line) {
                gsub(/\r/, "", detail)
                line = sprintf("[%d/%d] %s (%s): %s",
                    step_index, step_count, project_name, runtime, detail)
                if (columns > 1 && length(line) >= columns) {
                    line = substr(line, 1, columns - 1)
                }
                printf "\r\033[2K%s", line
            }
            {
                line = $0
                if (line ~ /^\[[0-9]+\/[0-9]+\] \[(PASS|FAIL)\] /) {
                    sub(/^\[[0-9]+\/[0-9]+\] \[(PASS|FAIL)\] /,
                        "&" project_name ": ", line)
                }
                if (output_mode == "verbose") {
                    print line
                } else if (output_mode == "tty") {
                    if (line != "") show_status(line)
                } else if (line ~ /^[0-9]+ passed, [0-9]+ failed$/) {
                    printf "[%d/%d] %s (%s): %s\n",
                        step_index, step_count, project_name, runtime, line
                }
                fflush()
            }
        '
}

# Runs one test runtime, retaining its full output for failures.
# @param project_name Project directory basename.
# @param runtime Test runtime name.
# @param step_index One-based runtime position.
# @param step_count Total runtimes in this invocation.
# @param output_mode verbose, tty, or plain.
# @param columns Available terminal columns.
# @param command Test command and arguments.
# @return The test command status.
run_test_command() {
    local project_name="$1"
    local runtime="$2"
    local step_index="$3"
    local step_count="$4"
    local output_mode="$5"
    local columns="$6"
    local test_log status

    shift 6

    if [ "$output_mode" = "tty" ]; then
        printf '\r\033[2K[%d/%d] %s (%s): running' \
            "$step_index" "$step_count" "$project_name" "$runtime"
    fi

    if [ "$output_mode" = "verbose" ]; then
        if "$@" 2>&1 | format_test_output "$project_name" "$runtime" \
            "$step_index" "$step_count" "$output_mode" "$columns"; then
            return 0
        else
            return $?
        fi
    fi

    test_log=$(mktemp)
    status=0
    "$@" 2>&1 | tee "$test_log" | format_test_output \
        "$project_name" "$runtime" "$step_index" "$step_count" \
        "$output_mode" "$columns" || status=$?

    if [ "$status" -ne 0 ]; then
        if [ "$output_mode" = "tty" ]; then
            printf '\r\033[2K'
        fi
        printf '[%d/%d] %s (%s): tests failed\n' \
            "$step_index" "$step_count" "$project_name" "$runtime" >&2
        cat "$test_log" >&2
        rm -f "$test_log"
        return "$status"
    fi

    if [ "$output_mode" = "plain" ] &&
        ! grep -Eq '^[0-9]+ passed, [0-9]+ failed$' "$test_log"; then
        printf '[%d/%d] %s (%s): completed\n' \
            "$step_index" "$step_count" "$project_name" "$runtime"
    fi

    rm -f "$test_log"
}

# Tests one kcbin project project on every supported runtime.
# @param project_dir Project directory.
# @param output_mode verbose, tty, or plain.
# @param columns Available terminal columns.
# @param step_index One-based position of the first runtime.
# @param step_count Total runtimes in this invocation.
# @return 0 on success.
test_project() {
    local project_dir="$1"
    local output_mode="$2"
    local columns="$3"
    local step_index="$4"
    local step_count="$5"
    local project_name

    project_name=$(basename "$project_dir")
    if [ "$output_mode" = "verbose" ]; then
        echo "Testing $project_name..."
    fi

    (
        cd "$project_dir"

        run_test_command "$project_name" linux "$step_index" \
            "$step_count" "$output_mode" "$columns" make test || exit $?
        step_index=$((step_index + 1))

        if [ "$output_mode" = "verbose" ]; then
            printf '\n'
        fi
        run_test_command "$project_name" wine "$step_index" \
            "$step_count" "$output_mode" "$columns" make test wine || exit $?
        step_index=$((step_index + 1))

        if grep -Eq '^wasm[[:space:]]*:' Makefile; then
            if [ "$output_mode" = "verbose" ]; then
                printf '\n'
            fi
            run_test_command "$project_name" wasm "$step_index" \
                "$step_count" "$output_mode" "$columns" make test wasm || exit $?
        fi
    )
}

# Dispatches one project test run or all project test runs.
# @param target Logical kcbin package name or all.
# @return 0 on success.
main() {
    local script_dir root_dir proj_dir target project_dir
    local output_mode terminal_cols step_index step_count steps
    local -a project_dirs

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
        project_dirs=()
        step_count=0
        for project_dir in "$proj_dir"/*; do
            [ -f "$project_dir/Makefile" ] || continue
            project_dirs+=("$project_dir")
            steps=$(count_test_steps "$project_dir")
            step_count=$((step_count + steps))
        done

        step_index=1
        for project_dir in "${project_dirs[@]}"; do
            steps=$(count_test_steps "$project_dir")
            test_project "$project_dir" "$output_mode" "$terminal_cols" \
                "$step_index" "$step_count" || return $?
            step_index=$((step_index + steps))
        done
        if [ "$output_mode" = "tty" ]; then
            printf '\r\033[2K[%d/%d] Tests complete\n' \
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

    step_count=$(count_test_steps "$project_dir")
    test_project "$project_dir" "$output_mode" "$terminal_cols" \
        1 "$step_count" || return $?

    if [ "$output_mode" = "tty" ]; then
        printf '\r\033[2K[%d/%d] %s: tests complete\n' \
            "$step_count" "$step_count" "$(basename "$project_dir")"
    fi
}

main "$@"
