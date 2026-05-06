#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SELF_TEST=0

if [ "${1:-}" = "--self-test" ]; then
    SELF_TEST=1
fi

check_file() {
    path=$1
    file=$2
    case "$path" in
        stdlib/sloppy/*)
            if [ "$path" != "stdlib/sloppy/codec.js" ] &&
                [ "$path" != "stdlib/sloppy/internal/runtime-classic.js" ] &&
                grep -Eq 'new[[:space:]]+Text(Encoder|Decoder)[[:space:]]*\(' "$file"; then
                printf '%s\n' "$path uses TextEncoder/TextDecoder directly; route text conversions through sloppy/codec Text."
                return 1
            fi
            if [ "$path" != "stdlib/sloppy/internal/runtime-classic.js" ] &&
                grep -Eq '^[[:space:]]*(export[[:space:]]+)?(function|const|let|var)[[:space:]]+(utf8ToBytes|bytesToHex|bytesToBase64)\b' "$file"; then
                printf '%s\n' "$path defines local UTF-8/hex/base64 helpers; use sloppy/codec instead."
                return 1
            fi
            if [ "$path" != "stdlib/sloppy/internal/runtime-classic.js" ] &&
                grep -Eq '\bMath\.random[[:space:]]*\(' "$file"; then
                printf '%s\n' "$path uses Math.random for runtime API behavior; use sloppy/crypto Random or document a non-security exception."
                return 1
            fi
            ;;
    esac
    if { case "$path" in
            src/*.c|src/*.h|src/*.cc|src/*.cpp|src/*/*.c|src/*/*.h|src/*/*.cc|src/*/*.cpp|src/*/*/*.c|src/*/*/*.h|src/*/*/*.cc|src/*/*/*.cpp|include/*.h|include/*/*.h|include/*/*/*.h)
                [ "$path" != "src/core/string.c" ] &&
                    grep -Eq 'static[[:space:]]+bool[[:space:]]+sl_[A-Za-z0-9_]*ascii[A-Za-z0-9_]*(equal|starts|ends)[A-Za-z0-9_]*ci' "$file"
                ;;
            *)
                false
                ;;
          esac; } ||
        { case "$path" in
            src/core/http_*.c|src/platform/libuv/http_transport_libuv.c)
                grep -Eq 'sl_http_(dispatch|backend|response|transport)_(ascii_lower|str_i(equal|starts_with|ends_with))\b' "$file"
                ;;
            *)
                false
                ;;
          esac; }; then
        printf '%s\n' "$path defines a local ASCII comparison helper; use sloppy/string case-insensitive helpers."
        return 1
    fi
    return 0
}

if [ "$SELF_TEST" -eq 1 ]; then
    tmp=${TMPDIR:-/tmp}/sloppy-core-integration-$$
    mkdir -p "$tmp/stdlib/sloppy" "$tmp/src/core"
    printf "const bytes = new TextEncoder().encode('x');\n" > "$tmp/stdlib/sloppy/fs.js"
    printf "const owner = new TextEncoder();\n" > "$tmp/stdlib/sloppy/codec.js"
    mkdir -p "$tmp/stdlib/sloppy/internal"
    printf "function bytesToHex(bytes) { return bytes; } const text = new TextDecoder();\n" > "$tmp/stdlib/sloppy/internal/runtime-classic.js"
    printf "export const bytesToHex = (bytes) => bytes;\n" > "$tmp/stdlib/sloppy/net.js"
    printf "const id = Math.random();\n" > "$tmp/stdlib/sloppy/workers.js"
    printf "const id = Math.random();\n" > "$tmp/stdlib/sloppy/crypto.js"
    printf "static bool sl_str_ascii_equal_ci(char a, char b) { return a == b; }\n" > "$tmp/src/core/string.c"
    printf "static bool sl_http_backend_str_iequal(SlStr left, SlStr right) { return true; }\n" > "$tmp/src/core/http_backend.c"
    printf "static bool sl_diag_ascii_equal_ci(char actual, char expected) { return actual == expected; }\n" > "$tmp/src/core/diagnostics.c"
    actual="$tmp/actual.txt"
    expected="$tmp/expected.txt"
    : > "$actual"
    check_file "stdlib/sloppy/fs.js" "$tmp/stdlib/sloppy/fs.js" >> "$actual" 2>/dev/null || true
    check_file "stdlib/sloppy/codec.js" "$tmp/stdlib/sloppy/codec.js" >> "$actual" 2>/dev/null || true
    check_file "stdlib/sloppy/internal/runtime-classic.js" "$tmp/stdlib/sloppy/internal/runtime-classic.js" >> "$actual" 2>/dev/null || true
    check_file "stdlib/sloppy/net.js" "$tmp/stdlib/sloppy/net.js" >> "$actual" 2>/dev/null || true
    check_file "stdlib/sloppy/workers.js" "$tmp/stdlib/sloppy/workers.js" >> "$actual" 2>/dev/null || true
    check_file "stdlib/sloppy/crypto.js" "$tmp/stdlib/sloppy/crypto.js" >> "$actual" 2>/dev/null || true
    check_file "src/core/string.c" "$tmp/src/core/string.c" >> "$actual" 2>/dev/null || true
    check_file "src/core/http_backend.c" "$tmp/src/core/http_backend.c" >> "$actual" 2>/dev/null || true
    check_file "src/core/diagnostics.c" "$tmp/src/core/diagnostics.c" >> "$actual" 2>/dev/null || true
    {
        printf '%s\n' "stdlib/sloppy/fs.js uses TextEncoder/TextDecoder directly; route text conversions through sloppy/codec Text."
        printf '%s\n' "stdlib/sloppy/net.js defines local UTF-8/hex/base64 helpers; use sloppy/codec instead."
        printf '%s\n' "stdlib/sloppy/workers.js uses Math.random for runtime API behavior; use sloppy/crypto Random or document a non-security exception."
        printf '%s\n' "stdlib/sloppy/crypto.js uses Math.random for runtime API behavior; use sloppy/crypto Random or document a non-security exception."
        printf '%s\n' "src/core/http_backend.c defines a local ASCII comparison helper; use sloppy/string case-insensitive helpers."
        printf '%s\n' "src/core/diagnostics.c defines a local ASCII comparison helper; use sloppy/string case-insensitive helpers."
    } | sort > "$expected"
    sort "$actual" > "$tmp/actual.sorted"
    if ! cmp -s "$expected" "$tmp/actual.sorted"; then
        printf 'core API integration scanner self-test mismatch.\nExpected:\n' >&2
        cat "$expected" >&2
        printf 'Actual:\n' >&2
        cat "$tmp/actual.sorted" >&2
        rm -rf "$tmp"
        exit 1
    fi
    rm -rf "$tmp"
    printf 'core API integration scanner self-test passed.\n'
    exit 0
fi

collect_files() {
    if command -v git >/dev/null 2>&1 &&
        git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git -C "$ROOT" ls-files -z --cached --others --exclude-standard
        return
    fi

    while IFS= read -r -d '' file; do
        path=${file#"$ROOT"/}
        printf '%s\0' "$path"
    done < <(find "$ROOT" -type f -print0)
}

violations=""
while IFS= read -r -d '' path; do
    case "$path" in
        *.c|*.h|*.cc|*.cpp|*.js|*.mjs|*.ts|*.md|*.json|*.cmake|*.ps1|*.sh|*.rs)
            ;;
        *)
            continue
            ;;
    esac
    if [ -f "$ROOT/$path" ]; then
        if ! message=$(check_file "$path" "$ROOT/$path"); then
            violations="${violations}${message}
"
        fi
    fi
done < <(collect_files)

if [ -n "$violations" ]; then
    printf 'Core API integration check failed:\n%s' "$violations" >&2
    exit 1
fi

printf 'core API integration check passed.\n'
