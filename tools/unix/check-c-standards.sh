#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

violations=()
warnings=()

is_platform_path() {
    [[ "$1" == src/platform/* ]]
}

is_v8_path() {
    [[ "$1" == src/engine/v8/* ]]
}

is_allowed_alloc_path() {
    [[ "$1" == src/core/arena.* || "$1" == src/core/alloc.* || "$1" == src/memory/* ]]
}

is_allowed_memory_boundary() {
    local file="$1"
    local function_name="$2"
    local line="$3"

    [[ "$line" == *"sloppy-allow: c-memory-boundary "* ]] && return 0
    [[ "$file" == "src/core/string.c" && "$function_name" == "strlen" ]] && return 0
    [[ "$file" == "src/main.c" && "$function_name" == "strlen" ]] && return 0
    [[ "$file" == "src/data/postgres.c" && "$function_name" == "snprintf" ]] && return 0
    [[ "$file" == "src/data/sqlserver.c" && "$function_name" == "memcpy" ]] && return 0
    [[ "$file" == "src/engine/v8/intrinsics_sqlite.cc" && "$function_name" == "memcpy" ]] && return 0
    return 1
}

add_finding() {
    local target_array="$1"
    local file="$2"
    local line="$3"
    local rule="$4"
    local pattern="$5"

    if [ "$target_array" = "violations" ]; then
        violations+=("$file:$line: error: $rule (pattern: $pattern)")
    else
        warnings+=("$file:$line: warning: $rule (pattern: $pattern)")
    fi
}

while IFS= read -r file; do
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
                windows.h|winsock2.h|ws2tcpip.h|io.h|unistd.h|pthread.h|sys/epoll.h|sys/event.h)
                    if ! is_platform_path "$file"; then
                        add_finding violations "$file" "$line_number" "OS headers are allowed only under src/platform/*." "#include <$header>"
                    fi
                    ;;
            esac

            if [[ "$header" == v8* ]] && ! is_v8_path "$file"; then
                add_finding violations "$file" "$line_number" "V8 headers are allowed only under src/engine/v8/*." "#include <$header>"
            fi
        fi

        if [[ "$line" == *"v8::"* ]] && ! is_v8_path "$file"; then
            add_finding violations "$file" "$line_number" "V8 types must not leak outside src/engine/v8/*." "v8::"
        fi

        if [[ "$line" =~ (^|[^[:alnum:]_])(gets|strcpy|strcat|sprintf|vsprintf)[[:space:]]*\( ]]; then
            add_finding violations "$file" "$line_number" "Unsafe C string/format functions are forbidden." "${BASH_REMATCH[2]}"
        fi

        if [[ "$line" =~ (^|[^[:alnum:]_])(snprintf|strlen|memcpy|memmove)[[:space:]]*\( ]] && ! is_allowed_memory_boundary "$file" "${BASH_REMATCH[2]}" "$line"; then
            add_finding violations "$file" "$line_number" "Use Slop memory/string/buffer primitives instead of ad hoc low-level operations." "${BASH_REMATCH[2]}"
        fi

        if [[ "$line" =~ (^|[^[:alnum:]_])(malloc|free|realloc|calloc)[[:space:]]*\( ]] && ! is_allowed_alloc_path "$file"; then
            add_finding warnings "$file" "$line_number" "Raw allocation belongs in allocator modules." "${BASH_REMATCH[2]}"
        fi
    done < "$file"
done < <(git ls-files include src)

if [ "${#warnings[@]}" -gt 0 ]; then
    echo "C standards warnings found:"
    printf '  %s\n' "${warnings[@]}"
fi

if [ "${#violations[@]}" -gt 0 ]; then
    echo "C standards violations found:" >&2
    printf '  %s\n' "${violations[@]}" >&2
    exit 1
fi

echo "C standards check passed."
