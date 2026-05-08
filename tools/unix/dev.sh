#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
command_name="${1:-help}"
if [[ $# -gt 0 ]]; then
  shift
fi

preset=""
package_path=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      preset="${2:?missing value for --preset}"
      shift 2
      ;;
    --package-path)
      package_path="${2:?missing value for --package-path}"
      shift 2
      ;;
    -h|--help)
      command_name="help"
      shift
      ;;
    *)
      echo "sloppy dev: unknown option '$1'" >&2
      exit 2
      ;;
  esac
done

host_preset() {
  if [[ -n "$preset" ]]; then
    printf '%s\n' "$preset"
    return
  fi
  case "$(uname -s)" in
    Darwin) printf 'macos-clang\n' ;;
    Linux) printf 'linux-clang\n' ;;
    *)
      echo "sloppy dev: unsupported Unix platform: $(uname -s)" >&2
      exit 1
      ;;
  esac
}

package_configuration() {
  case "$(host_preset)" in
    *debug*|*Debug*) printf 'Debug\n' ;;
    *) printf 'Release\n' ;;
  esac
}

resolve_vcpkg_toolchain_arg() {
  local vcpkg_root="${VCPKG_ROOT:-$repo_root/.sdeps/vcpkg}"
  local toolchain="$vcpkg_root/scripts/buildsystems/vcpkg.cmake"
  if [[ ! -f "$toolchain" ]]; then
    echo "sloppy dev: vcpkg toolchain missing at $toolchain; run tools/unix/bootstrap.sh or set VCPKG_ROOT." >&2
    exit 1
  fi
  printf '%s\n' "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
}

usage() {
  cat <<'USAGE'
Usage: tools/unix/dev.sh <command> [--preset PRESET] [--package-path PATH]

Commands:
  doctor        Validate required Unix host tools and optional dependency status.
  configure     Configure the selected CMake preset.
  build         Build the selected CMake preset.
  test          Run CTest and compiler tests.
  lint          Run POSIX standards scanners.
  format-check  Run Rust format check when rustfmt is available.
  package       Build an experimental local tar.gz package.
  test-package  Extract a package outside the checkout and run smoke checks.
  npm-dry-run   Unavailable on Unix in this PR; use tools/windows/npm-dry-run.ps1 or hosted follow-up.
  dogfood      Run or report ALPHA-INFRA dogfood/example evidence.
  clean         Remove the selected build directory.
  help          Print this help.

Unsupported optional lanes are reported as unavailable/skipped, never as pass evidence.
USAGE
}

doctor() {
  local missing=0
  for tool in git cmake ninja cargo; do
    if command -v "$tool" >/dev/null 2>&1; then
      echo "doctor: found: $tool"
    else
      echo "doctor: missing: $tool" >&2
      missing=1
    fi
  done
  if command -v docker >/dev/null 2>&1; then
    echo "doctor: optional found: docker"
  else
    echo "doctor: optional unavailable: docker"
  fi
  echo "doctor: optional unavailable: V8 SDK artifact fetch for Unix is not implemented"
  return "$missing"
}

configure() {
  local selected_preset
  selected_preset="$(host_preset)"
  cmake --preset "$selected_preset" \
    "$(resolve_vcpkg_toolchain_arg)" \
    -DCMAKE_MAKE_PROGRAM="$(command -v ninja)" \
    -DSLOPPY_ENABLE_V8=OFF \
    -DSLOPPY_ENGINE=none
}

build() {
  cmake --build --preset "$(host_preset)"
}

test_repo() {
  ctest --preset "$(host_preset)" --output-on-failure
  cargo test --manifest-path "$repo_root/compiler/Cargo.toml"
}

lint_repo() {
  "$repo_root/tools/unix/check-platform-boundaries.sh"
  "$repo_root/tools/unix/check-c-standards.sh" --self-test
  "$repo_root/tools/unix/check-c-standards.sh"
}

format_check() {
  if command -v cargo >/dev/null 2>&1 && command -v rustfmt >/dev/null 2>&1; then
    cargo fmt --manifest-path "$repo_root/compiler/Cargo.toml" -- --check
  else
    echo "format-check: rustfmt unavailable; skipped local Rust format check"
  fi
}

clean() {
  local selected_preset build_dir
  selected_preset="$(host_preset)"
  build_dir="$repo_root/build/$selected_preset"
  case "$build_dir" in
    "$repo_root"/build/*) rm -rf "$build_dir" ;;
    *) echo "sloppy dev: refusing to clean outside build root: $build_dir" >&2; exit 1 ;;
  esac
  echo "clean: removed $build_dir"
}

package_repo() {
  "$repo_root/tools/unix/package.sh" --configuration "$(package_configuration)"
}

test_package() {
  local resolved="$package_path"
  if [[ -z "$resolved" ]]; then
    resolved="$(ls -t "$repo_root"/artifacts/packages/sloppy-*.tar.gz 2>/dev/null | head -n 1 || true)"
  fi
  if [[ -z "$resolved" ]]; then
    echo "sloppy dev: no Unix package archive found; run tools/unix/dev.sh package or pass --package-path." >&2
    exit 1
  fi
  "$repo_root/tools/unix/test-package.sh" --package-path "$resolved"
}

dogfood_repo() {
  local args=()
  if [[ -n "$package_path" ]]; then
    args+=(--package-path "$package_path")
  fi
  "$repo_root/tools/unix/dogfood.sh" "${args[@]}"
}

case "$command_name" in
  help) usage ;;
  doctor) doctor ;;
  configure) configure ;;
  build) build ;;
  test) test_repo ;;
  lint) lint_repo ;;
  format-check) format_check ;;
  clean) clean ;;
  package) package_repo ;;
  test-package) test_package ;;
  npm-dry-run)
    echo "sloppy dev: npm-dry-run is unavailable in tools/unix/dev.sh in this dry-run PR. Generate npm tarballs from tested archives with tools/windows/npm-dry-run.ps1, or add a Unix implementation in a follow-up." >&2
    exit 2
    ;;
  dogfood) dogfood_repo ;;
  *)
    echo "sloppy dev: unknown command '$command_name'. Run tools/unix/dev.sh help for the command contract." >&2
    exit 2
    ;;
esac
