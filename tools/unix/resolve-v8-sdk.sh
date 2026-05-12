#!/usr/bin/env bash
set -euo pipefail

mode="REQUIRED"
v8_root="${SLOPPY_V8_ROOT:-}"
quiet=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode)
      mode="${2:?missing value for --mode}"
      shift 2
      ;;
    --v8-root)
      v8_root="${2:?missing value for --v8-root}"
      shift 2
      ;;
    --quiet)
      quiet=1
      shift
      ;;
    -h|--help)
      cat <<'USAGE'
Usage: tools/unix/resolve-v8-sdk.sh [--mode OFF|AUTO|REQUIRED] [--v8-root DIR] [--quiet]

Resolves a Sloppy-owned Unix V8 SDK. The happy path is .sdeps/v8/linux-x64, produced by
tools/unix/build-v8.sh or extracted from the pinned Sloppy SDK artifact. Distro V8/Node
development packages are not treated as compatible SDKs.
USAGE
      exit 0
      ;;
    *)
      echo "resolve-v8-sdk: unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

case "$mode" in
  OFF|AUTO|REQUIRED) ;;
  *) echo "resolve-v8-sdk: --mode must be OFF, AUTO, or REQUIRED" >&2; exit 2 ;;
esac

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
expected_v8_revision="7221f49fdb6c89cce6be08005732ebcab3c45b38"
expected_cr_libcxx_revision="af4386908c3762433d412689038de6e6333f5921"
platform=""
case "$(uname -s):$(uname -m)" in
  Linux:x86_64|Linux:amd64) platform="linux-x64" ;;
  Darwin:arm64|Darwin:aarch64) platform="macos-arm64" ;;
  Darwin:x86_64|Darwin:amd64) platform="macos-x64" ;;
  *) platform="$(uname -s)-$(uname -m)" ;;
esac

log() {
  if [[ "$quiet" -eq 0 ]]; then
    printf '%s\n' "$*"
  fi
}

fail_or_empty() {
  local message="$1"
  if [[ "$mode" == "REQUIRED" ]]; then
    echo "$message" >&2
    exit 1
  fi
  log "$message"
  exit 0
}

if [[ "$mode" == "OFF" ]]; then
  log "V8 SDK resolution is disabled because --mode OFF was selected."
  exit 0
fi

validate_sdk_root() {
  local root="$1"
  [[ -d "$root" ]] || return 1
  [[ -f "$root/include/v8.h" ]] || return 1
  [[ -f "$root/include/libplatform/libplatform.h" ]] || return 1
  [[ -d "$root/lib" ]] || return 1
  [[ -f "$root/share/sloppy-v8-sdk.json" ]] || return 1

  case "$platform" in
    linux-x64)
      [[ -f "$root/lib/libv8_monolith.a" || -f "$root/lib/libv8_monolith.so" ]] || return 1
      [[ -f "$root/lib/libv8_libplatform.a" || -f "$root/lib/libv8_libplatform.so" ]] || return 1
      [[ -f "$root/lib/libv8_libbase.a" || -f "$root/lib/libv8_libbase.so" ]] || return 1
      [[ -f "$root/lib/libc++.a" || -f "$root/lib/libc++.so" ]] || return 1
      [[ -f "$root/lib/libc++abi.a" || -f "$root/lib/libc++abi.so" ]] || return 1
      [[ -f "$root/support/libcxx/include/memory" ]] || return 1
      [[ -f "$root/support/libcxx/buildtools/__config_site" ]] || return 1
      grep -Eq '"platform"[[:space:]]*:[[:space:]]*"linux-x64"' "$root/share/sloppy-v8-sdk.json" || return 1
      grep -Eq '"source"[[:space:]]*:[[:space:]]*"sloppy-built-v8"' "$root/share/sloppy-v8-sdk.json" || return 1
      grep -Eq "\"v8Revision\"[[:space:]]*:[[:space:]]*\"$expected_v8_revision\"" "$root/share/sloppy-v8-sdk.json" || return 1
      grep -Eq '"crtCompatibility"[[:space:]]*:[[:space:]]*"glibc clang-libc\+\+ static-v8"' "$root/share/sloppy-v8-sdk.json" || return 1
      grep -Eq '"glibcBaseline"[[:space:]]*:[[:space:]]*"2\.31"' "$root/share/sloppy-v8-sdk.json" || return 1
      grep -Eq "\"crLibcxxRevision\"[[:space:]]*:[[:space:]]*\"$expected_cr_libcxx_revision\"" "$root/share/sloppy-v8-sdk.json" || return 1
      grep -Eq '"v8EnableSandbox"[[:space:]]*:[[:space:]]*true' "$root/share/sloppy-v8-sdk.json" || return 1
      ;;
    macos-arm64|macos-x64)
      [[ -f "$root/lib/libv8_monolith.a" || -f "$root/lib/libv8_monolith.dylib" ]] || return 1
      [[ -f "$root/lib/libv8_libplatform.a" || -f "$root/lib/libv8_libplatform.dylib" ]] || return 1
      [[ -f "$root/lib/libv8_libbase.a" || -f "$root/lib/libv8_libbase.dylib" ]] || return 1
      [[ -f "$root/lib/libc++.a" || -f "$root/lib/libc++.dylib" ]] || return 1
      [[ -f "$root/lib/libc++abi.a" || -f "$root/lib/libc++abi.dylib" ]] || return 1
      [[ -f "$root/support/libcxx/include/memory" ]] || return 1
      [[ -f "$root/support/libcxx/buildtools/__config_site" ]] || return 1
      [[ -f "$root/support/libcxx/buildtools/__assertion_handler" ]] || return 1
      grep -Eq "\"platform\"[[:space:]]*:[[:space:]]*\"$platform\"" "$root/share/sloppy-v8-sdk.json" || return 1
      grep -Eq '"source"[[:space:]]*:[[:space:]]*"sloppy-built-v8"' "$root/share/sloppy-v8-sdk.json" || return 1
      grep -Eq "\"v8Revision\"[[:space:]]*:[[:space:]]*\"$expected_v8_revision\"" "$root/share/sloppy-v8-sdk.json" || return 1
      grep -Eq '"crtCompatibility"[[:space:]]*:[[:space:]]*"macos clang-libc\+\+ static-v8"' "$root/share/sloppy-v8-sdk.json" || return 1
      ;;
    *)
      return 1
      ;;
  esac

  return 0
}

if [[ -n "$v8_root" ]]; then
  if validate_sdk_root "$v8_root"; then
    [[ "$quiet" -eq 1 ]] && printf '%s\n' "$v8_root" || log "Resolved Sloppy V8 SDK root: $v8_root"
    exit 0
  fi
  fail_or_empty "No compatible Sloppy V8 SDK was found at explicit --v8-root path: $v8_root"
fi

candidate_roots=()
if [[ -n "${SLOPPY_V8_SDK_HINTS:-}" ]]; then
  old_ifs="$IFS"
  IFS=":"
  for hint in $SLOPPY_V8_SDK_HINTS; do
    candidate_roots+=("$hint" "$hint/$platform" "$hint/v8/$platform" "$hint/.sdeps/v8/$platform")
  done
  IFS="$old_ifs"
fi
candidate_roots+=("$repo_root/.sdeps/v8/$platform")

for candidate in "${candidate_roots[@]}"; do
  if validate_sdk_root "$candidate"; then
    [[ "$quiet" -eq 1 ]] && printf '%s\n' "$candidate" || log "Resolved Sloppy V8 SDK root: $candidate"
    exit 0
  fi
done

fail_or_empty "No compatible Sloppy V8 SDK was resolved for $platform. Build one with tools/unix/build-v8.sh or set SLOPPY_V8_ROOT to a Sloppy-owned SDK root."
