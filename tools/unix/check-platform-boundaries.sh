#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

violations=()

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
                windows.h|winsock2.h|io.h|unistd.h|pthread.h|sys/epoll.h|sys/event.h)
                    if ! is_allowed_platform_path "$file" "$header"; then
                        violations+=("$file:$line_number: forbidden platform header <$header>")
                    fi
                    ;;
            esac
        fi
    done < "$file"
done < <(git ls-files include src)

if [ "${#violations[@]}" -gt 0 ]; then
    echo "Platform boundary violations found:" >&2
    printf '  %s\n' "${violations[@]}" >&2
    exit 1
fi

echo "platform boundary check passed."
