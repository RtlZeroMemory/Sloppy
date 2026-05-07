#!/usr/bin/env bash
set -euo pipefail

repo_root="${SLOPPY_C_STANDARDS_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
cd "$repo_root"

if [ "${1:-}" = "--self-test" ]; then
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/sloppy-c-standards.XXXXXX")
    trap 'rm -rf "$tmp"' EXIT

    valid="$tmp/valid"
    invalid="$tmp/invalid"
    mkdir -p "$valid/src/core" "$valid/tests/unit/core" "$invalid/src/core" "$invalid/tests/unit/core"

    cat > "$valid/src/core/string.c" <<'EOF'
#include <string.h>
size_t ok_strlen(const char* text) { return strlen(text); }
int ok_memcmp(const char* a, const char* b, size_t n) { return memcmp(a, b, n); }
void ok_boundary(char* dst, const char* src, size_t n) { memcpy(dst, src, n); } /* sloppy-allow: c-memory-boundary #760 fixture boundary copy; remove when fixture is replaced */
EOF
    cat > "$valid/tests/unit/core/test_ok.c" <<'EOF'
/* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): sloppy-analysis-suppress: #805 fixture suppression; remove when fixture is replaced */
int ok_suppression(void) { return 0; }
/* NOLINTBEGIN(clang-analyzer-deadcode.DeadStores): sloppy-analysis-suppress: #805 fixture block suppression; remove when fixture is replaced */
int ok_block_suppression(void) { int value = 1; return value; }
/* NOLINTEND(clang-analyzer-deadcode.DeadStores) */
EOF

    cat > "$invalid/src/core/bad.c" <<'EOF'
#include <string.h>
#include <stdlib.h>
void bad_copy(char* dst, const char* src) { strcpy(dst, src); }
void bad_memset(char* dst) { memset(dst, 0, 8); }
void* bad_alloc(void) { return malloc(16); }
/* NOLINTNEXTLINE(clang-analyzer-core.NullDereference) */
int bad_suppression(void) { return 0; }
EOF
    cat > "$invalid/tests/unit/core/test_bad.c" <<'EOF'
#include <string.h>
void bad_test_copy(char* dst, const char* src) { strncpy(dst, src, 4); }
EOF

    SLOPPY_C_STANDARDS_ROOT="$valid" "$0" >/dev/null
    if SLOPPY_C_STANDARDS_ROOT="$invalid" "$0" >/dev/null 2>&1; then
        echo "C standards scanner self-test invalid fixture unexpectedly passed." >&2
        exit 1
    fi
    echo "C standards scanner self-test passed."
    exit 0
fi

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

    [[ "$line" =~ sloppy-allow:[[:space:]]c-memory-boundary[[:space:]]#[0-9]+[[:space:]].*\;[[:space:]]remove[[:space:]]when[[:space:]].+ ]] && return 0
    [[ "$file" == "src/core/string.c" && "$function_name" == "strlen" ]] && return 0
    [[ "$function_name" == "memcmp" && ( "$file" == "src/core/string.c" || "$file" == "src/core/bytes.c" ) ]] && return 0
    return 1
}

is_allowed_cstring_terminator_boundary() {
    [[ "$1" == "src/core/string.c" || "$1" == "include/sloppy/string.h" ]]
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

is_implementation_path() {
    [[ "$1" == include/* || "$1" == src/* ]]
}

collect_files() {
    if command -v git >/dev/null 2>&1 &&
        git -C "$repo_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git -C "$repo_root" ls-files include src tests benchmarks
        return
    fi

    find "$repo_root/include" "$repo_root/src" "$repo_root/tests" "$repo_root/benchmarks" \
        -type f 2>/dev/null | sed "s#^$repo_root/##"
}

while IFS= read -r file; do
    case "$file" in
        *.c|*.h|*.cc|*.cpp|*.cxx|*.hpp|*.hh|*.hxx) ;;
        *) continue ;;
    esac

    line_number=0
    previous_line=""
    inside_nolint_block=0
    while IFS= read -r line || [ -n "$line" ]; do
        ((line_number += 1))
        allowance_context="$previous_line $line"

        if is_implementation_path "$file" &&
            [[ "$line" =~ ^[[:space:]]*#[[:space:]]*include[[:space:]]*[\<\"]([^\">]+)[\"\>] ]]; then
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

        if is_implementation_path "$file" && [[ "$line" == *"v8::"* ]] && ! is_v8_path "$file"; then
            add_finding violations "$file" "$line_number" "V8 types must not leak outside src/engine/v8/*." "v8::"
        fi

        if [[ "$line" =~ (^|[^[:alnum:]_])(gets|strcpy|strncpy|strcat|sprintf|vsprintf|strdup)[[:space:]]*\( ]]; then
            add_finding violations "$file" "$line_number" "Unsafe C string/format functions are forbidden." "${BASH_REMATCH[2]}"
        fi

        has_nolint=0
        is_nolint_begin=0
        is_nolint_end=0
        has_valid_suppression=0
        skip_nolint_violation=0
        [[ "$line" =~ (^|[^[:alnum:]_])NOLINT(NEXTLINE|BEGIN|END)?($|[^[:alnum:]_]) ]] && has_nolint=1
        [[ "$line" =~ (^|[^[:alnum:]_])NOLINTBEGIN($|[^[:alnum:]_]) ]] && is_nolint_begin=1
        [[ "$line" =~ (^|[^[:alnum:]_])NOLINTEND($|[^[:alnum:]_]) ]] && is_nolint_end=1
        [[ "$allowance_context" =~ sloppy-analysis-suppress:[[:space:]]#[0-9]+[[:space:]].*\;[[:space:]]remove[[:space:]]when[[:space:]].+ ]] && has_valid_suppression=1
        if [[ "$is_nolint_end" -eq 1 && "$inside_nolint_block" -eq 1 ]]; then
            skip_nolint_violation=1
        fi
        if [[ "$has_nolint" -eq 1 && "$skip_nolint_violation" -eq 0 && "$has_valid_suppression" -eq 0 ]]; then
            add_finding violations "$file" "$line_number" "Static-analysis suppressions need an issue, reason, and removal condition." "NOLINT"
        fi
        if [[ "$has_nolint" -eq 1 && "$is_nolint_begin" -eq 1 && "$has_valid_suppression" -eq 1 ]]; then
            inside_nolint_block=1
        fi
        if [[ "$has_nolint" -eq 1 && "$is_nolint_end" -eq 1 ]]; then
            inside_nolint_block=0
        fi

        if is_implementation_path "$file" &&
            [[ "$line" =~ (^|[^[:alnum:]_])sl_str_copy_to_arena_nul[[:space:]]*\( ]] &&
            ! is_allowed_cstring_terminator_boundary "$file"; then
            add_finding violations "$file" "$line_number" "Raw NUL-append copies are not C-string boundary validation." "sl_str_copy_to_arena_nul"
        fi

        if is_implementation_path "$file" &&
            [[ "$line" =~ (^|[^[:alnum:]_])(snprintf|strlen|memcpy|memmove|memcmp|memset)[[:space:]]*\( ]] &&
            ! is_allowed_memory_boundary "$file" "${BASH_REMATCH[2]}" "$allowance_context"; then
            add_finding violations "$file" "$line_number" "Use Slop memory/string/buffer primitives instead of ad hoc low-level operations." "${BASH_REMATCH[2]}"
        fi

        if is_implementation_path "$file" &&
            [[ "$line" =~ (^|[^[:alnum:]_])(malloc|free|realloc|calloc)[[:space:]]*\( ]] &&
            ! is_allowed_alloc_path "$file"; then
            add_finding violations "$file" "$line_number" "Raw allocation belongs in allocator modules." "${BASH_REMATCH[2]}"
        fi
        previous_line="$line"
    done < "$repo_root/$file"
done < <(collect_files)

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
