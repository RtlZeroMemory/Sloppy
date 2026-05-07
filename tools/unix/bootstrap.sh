#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
vcpkg_root="$repo_root/.sdeps/vcpkg"
vcpkg_binary_cache="$repo_root/.sdeps/vcpkg-bincache"
vcpkg_commit="$(grep -m1 '"builtin-baseline"' "$repo_root/vcpkg.json" | sed -E 's/.*"builtin-baseline"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/')"
if [[ -z "$vcpkg_commit" ]]; then
  echo "bootstrap: failed to parse builtin-baseline from $repo_root/vcpkg.json" >&2
  exit 1
fi

require_command() {
  local command_name="$1"
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "bootstrap: missing required command: $command_name" >&2
    exit 1
  fi
}

case "${1:-}" in
  -h|--help|help)
    cat <<'USAGE'
Usage: tools/unix/bootstrap.sh

Validates the Unix host tools needed for Sloppy's non-V8 developer loop and bootstraps the
repo-local vcpkg cache under .sdeps/. Linux and macOS remain cross-platform lanes; this
script does not fetch V8 SDK artifacts or claim release readiness.
USAGE
    exit 0
    ;;
  "")
    ;;
  *)
    echo "bootstrap: unknown argument: $1" >&2
    exit 2
    ;;
esac

for command_name in git cmake ninja cargo; do
  require_command "$command_name"
done

mkdir -p "$vcpkg_binary_cache"
if [[ ! -d "$vcpkg_root" ]]; then
  mkdir -p "$(dirname "$vcpkg_root")"
  git clone https://github.com/microsoft/vcpkg.git "$vcpkg_root"
fi

git -C "$vcpkg_root" fetch origin
git -C "$vcpkg_root" checkout "$vcpkg_commit"
if [[ ! -x "$vcpkg_root/vcpkg" ]]; then
  "$vcpkg_root/bootstrap-vcpkg.sh" -disableMetrics
fi

cat <<EOF
bootstrap: found cmake, ninja, git, cargo
bootstrap: vcpkg root: $vcpkg_root
bootstrap: vcpkg binary cache: $vcpkg_binary_cache
bootstrap: V8 SDK fetch is unavailable in Unix bootstrap; use V8 OFF/AUTO reporting until a pinned artifact source lands.
EOF
