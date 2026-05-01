#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
scan_root="$repo_root"
self_test_only=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --self-test)
            self_test_only=1
            ;;
        --scan-root)
            shift
            if [ "$#" -eq 0 ]; then
                echo "--scan-root requires a path." >&2
                exit 2
            fi
            scan_root="$1"
            ;;
        *)
            echo "unknown argument: $1" >&2
            exit 2
            ;;
    esac
    shift
done

scan_root="$(cd "$scan_root" && pwd)"

is_allowed_platform_path() {
    local path="$1"
    local header="$2"

    case "$header" in
        windows.h|winsock2.h|io.h)
            [[ "$path" == src/platform/win32/* ]]
            ;;
        unistd.h|pthread.h)
            [[ "$path" == src/platform/posix/* || "$path" == src/platform/linux/* || "$path" == src/platform/macos/* ]]
            ;;
        sys/epoll.h)
            [[ "$path" == src/platform/linux/* ]]
            ;;
        sys/event.h)
            [[ "$path" == src/platform/macos/* ]]
            ;;
        *)
            return 1
            ;;
    esac
}

run_scan() {
    local root="$1"
    violations=()

    cd "$root"

    local file_source=()
    if git -C "$root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        while IFS= read -r discovered_file; do
            file_source+=("$discovered_file")
        done < <(git -C "$root" ls-files include src)
    else
        while IFS= read -r discovered_file; do
            file_source+=("$discovered_file")
        done < <(find include src -type f 2>/dev/null | sed 's#^\./##')
    fi

    local file
    for file in "${file_source[@]}"; do
        case "$file" in
            *.c|*.h|*.cc|*.cpp|*.cxx|*.hpp|*.hh|*.hxx) ;;
            *) continue ;;
        esac

        line_number=0
        while IFS= read -r line || [ -n "$line" ]; do
            ((line_number += 1))
            if [[ "$line" =~ ^[[:space:]]*#[[:space:]]*include[[:space:]]*[\<\"]([^\">]+)[\"\>] ]]; then
                header="${BASH_REMATCH[1]}"
                case "$header" in
                    windows.h|winsock2.h|io.h|unistd.h|pthread.h|sys/epoll.h|sys/event.h)
                        if ! is_allowed_platform_path "$file" "$header"; then
                            violations+=("$file:$line_number: forbidden platform header <$header>")
                        fi
                        ;;
                esac
            fi
        done < "$file"
    done
}

write_fixture() {
    local file="$1"
    local header="$2"
    mkdir -p "$(dirname "$file")"
    printf '#include <%s>\n' "$header" > "$file"
}

run_self_test() {
    local fixture_root
    fixture_root="$(mktemp -d "${TMPDIR:-/tmp}/sloppy-platform-boundary-self-test.XXXXXX")"

    write_fixture "$fixture_root/include/core_windows_forbidden.h" "windows.h"
    write_fixture "$fixture_root/src/core/posix_forbidden.c" "unistd.h"
    write_fixture "$fixture_root/src/platform/win32/allowed_windows.c" "windows.h"
    write_fixture "$fixture_root/src/platform/win32/allowed_winsock.c" "winsock2.h"
    write_fixture "$fixture_root/src/platform/posix/allowed_posix.c" "unistd.h"
    write_fixture "$fixture_root/src/platform/linux/allowed_epoll.c" "sys/epoll.h"
    write_fixture "$fixture_root/src/platform/macos/allowed_event.c" "sys/event.h"

    run_scan "$fixture_root"

    local expected_windows="include/core_windows_forbidden.h:1: forbidden platform header <windows.h>"
    local expected_posix="src/core/posix_forbidden.c:1: forbidden platform header <unistd.h>"
    local found_windows=0
    local found_posix=0
    local violation

    for violation in "${violations[@]}"; do
        [ "$violation" = "$expected_windows" ] && found_windows=1
        [ "$violation" = "$expected_posix" ] && found_posix=1
    done

    if [ "$found_windows" -ne 1 ]; then
        echo "platform boundary self-test did not report expected violation: $expected_windows" >&2
        rm -rf "$fixture_root"
        exit 1
    fi

    if [ "$found_posix" -ne 1 ]; then
        echo "platform boundary self-test did not report expected violation: $expected_posix" >&2
        rm -rf "$fixture_root"
        exit 1
    fi

    if [ "${#violations[@]}" -ne 2 ]; then
        echo "platform boundary self-test expected 2 violations, found ${#violations[@]}:" >&2
        printf '  %s\n' "${violations[@]}" >&2
        rm -rf "$fixture_root"
        exit 1
    fi

    rm -rf "$fixture_root"
    echo "platform boundary self-test passed."
}

run_self_test

if [ "$self_test_only" -eq 1 ]; then
    exit 0
fi

run_scan "$scan_root"

if [ "${#violations[@]}" -gt 0 ]; then
    echo "Platform boundary violations found:" >&2
    printf '  %s\n' "${violations[@]}" >&2
    exit 1
fi

echo "platform boundary check passed."
