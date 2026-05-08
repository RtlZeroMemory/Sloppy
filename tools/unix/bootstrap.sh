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

Validates the Unix host tools needed for Sloppy's developer loop and bootstraps the
repo-local vcpkg cache under .sdeps/. Linux and macOS remain cross-platform lanes; this
script does not build V8 SDK artifacts or claim release readiness.

On Debian/Ubuntu-style systems, the current Linux clang lane expects packages roughly
equivalent to:

  build-essential clang cmake ninja-build pkg-config curl zip unzip tar file \
  autoconf autoconf-archive automake libtool bison flex gawk python3 lld \
  libglib2.0-dev cargo
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

for command_name in git python3 cmake ninja cargo; do
  require_command "$command_name"
done
for command_name in clang clang++ curl zip unzip tar pkg-config autoconf aclocal automake libtoolize bison flex gawk; do
  require_command "$command_name"
done

require_autoconf_archive() {
  local aclocal_dir
  aclocal_dir="$(aclocal --print-ac-dir 2>/dev/null || true)"
  if [[ -n "$aclocal_dir" && -f "$aclocal_dir/ax_check_compile_flag.m4" ]]; then
    return
  fi
  if [[ -f /usr/share/aclocal/ax_check_compile_flag.m4 || -f /usr/local/share/aclocal/ax_check_compile_flag.m4 ]]; then
    return
  fi
  echo "bootstrap: missing required autoconf-archive macros. Install autoconf-archive." >&2
  exit 1
}

require_autoconf_archive

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
bootstrap: found cmake, ninja, git, python3, cargo, clang, clang++, and vcpkg host tools
bootstrap: vcpkg root: $vcpkg_root
bootstrap: vcpkg binary cache: $vcpkg_binary_cache
bootstrap: V8 SDK build is separate; use tools/unix/build-v8.sh for the pinned Linux x64 SDK.
EOF
