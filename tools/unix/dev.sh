#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
command_name="${1:-help}"
if [[ $# -gt 0 ]]; then
  shift
fi

preset=""
package_path=""
enable_v8=0
v8_root=""
require_v8_runtime=0

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
    --enable-v8)
      enable_v8=1
      shift
      ;;
    --v8-root)
      v8_root="${2:?missing value for --v8-root}"
      shift 2
      ;;
    --require-v8-runtime)
      require_v8_runtime=1
      shift
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
Usage: tools/unix/dev.sh <command> [--preset PRESET] [--package-path PATH] [--enable-v8] [--v8-root DIR]

Commands:
  doctor        Validate required Unix host tools and optional dependency status.
  configure     Configure the selected CMake preset.
  build         Build the selected CMake preset.
  test          Run CTest and compiler tests.
  lint          Run POSIX standards scanners.
  format-check  Run Rust format check when rustfmt is available.
  package       Build an experimental local tar.gz package.
  test-package  Extract a package outside the checkout and run smoke checks.
  test-install  Verify create/build/package/run from an extracted archive.
  build-v8      Build/package the pinned Sloppy-owned Linux x64 V8 SDK.
  npm-dry-run   Generate npm launcher/platform tarballs from a package archive.
  dogfood      Run or report dogfood/example evidence.
  clean         Remove the selected build directory.
  help          Print this help.

Use --enable-v8 for the Linux V8 build/package lane after tools/unix/dev.sh build-v8
or an extracted Sloppy-owned SDK has populated .sdeps/v8/linux-x64.
USAGE
}

doctor() {
  local missing=0
  for tool in git python3 cmake ninja cargo clang clang++ curl zip unzip tar pkg-config autoconf aclocal automake bison flex gawk; do
    if command -v "$tool" >/dev/null 2>&1; then
      echo "doctor: found: $tool"
    else
      echo "doctor: missing: $tool" >&2
      missing=1
    fi
  done
  if command -v libtoolize >/dev/null 2>&1 || command -v glibtoolize >/dev/null 2>&1; then
    echo "doctor: found: libtoolize or glibtoolize"
  else
    echo "doctor: missing: libtoolize or glibtoolize" >&2
    missing=1
  fi
  local aclocal_dir
  aclocal_dir="$(aclocal --print-ac-dir 2>/dev/null || true)"
  if [[ -n "$aclocal_dir" && -f "$aclocal_dir/ax_check_compile_flag.m4" ]] ||
    [[ -f /usr/share/aclocal/ax_check_compile_flag.m4 || -f /usr/local/share/aclocal/ax_check_compile_flag.m4 ]]; then
    echo "doctor: found: autoconf-archive"
  else
    echo "doctor: missing: autoconf-archive" >&2
    missing=1
  fi
  if command -v docker >/dev/null 2>&1; then
    echo "doctor: optional found: docker"
  else
    echo "doctor: optional unavailable: docker"
  fi
  if command -v ld.lld >/dev/null 2>&1; then
    echo "doctor: optional found: ld.lld for Linux V8 package linking"
  else
    echo "doctor: optional unavailable: ld.lld; required for tools/unix/dev.sh package --enable-v8 on linux-x64"
  fi
  if "$repo_root/tools/unix/resolve-v8-sdk.sh" --mode AUTO --quiet >/dev/null 2>&1; then
    echo "doctor: optional found: V8 SDK"
  else
    echo "doctor: optional unavailable: V8 SDK; run tools/unix/dev.sh build-v8 or set SLOPPY_V8_ROOT to a Sloppy-owned SDK"
  fi
  return "$missing"
}

resolve_v8_root_arg() {
  local resolved
  local args=(--mode REQUIRED --quiet)
  if [[ -n "$v8_root" ]]; then
    args+=(--v8-root "$v8_root")
  fi
  resolved="$("$repo_root/tools/unix/resolve-v8-sdk.sh" "${args[@]}")"
  printf '%s\n' "-DSLOPPY_V8_ROOT=$resolved"
}

configure() {
  local selected_preset
  selected_preset="$(host_preset)"
  local cmake_args=(
    --preset "$selected_preset"
    "$(resolve_vcpkg_toolchain_arg)" \
    -DCMAKE_MAKE_PROGRAM="$(command -v ninja)" \
  )
  if [[ "$enable_v8" -eq 1 ]]; then
    cmake_args+=(
      -DSLOPPY_ENABLE_V8=ON
      -DSLOPPY_ENGINE=v8
      "$(resolve_v8_root_arg)"
    )
  else
    cmake_args+=(
      -DSLOPPY_ENABLE_V8=OFF
      -DSLOPPY_ENGINE=none
    )
  fi
  cmake "${cmake_args[@]}"
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
  local args=(--configuration "$(package_configuration)")
  if [[ "$enable_v8" -eq 1 ]]; then
    args+=(--enable-v8)
    if [[ -n "$v8_root" ]]; then
      args+=(--v8-root "$v8_root")
    fi
  fi
  bash "$repo_root/tools/unix/package.sh" "${args[@]}"
}

build_v8() {
  "$repo_root/tools/unix/build-v8.sh"
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
  local args=(--package-path "$resolved")
  if [[ "$require_v8_runtime" -eq 1 ]]; then
    args+=(--require-v8-runtime)
  fi
  "$repo_root/tools/unix/test-package.sh" "${args[@]}"
}

test_install() {
  local resolved="$package_path"
  if [[ -z "$resolved" ]]; then
    resolved="$(ls -t "$repo_root"/artifacts/packages/sloppy-*.tar.gz 2>/dev/null | head -n 1 || true)"
  fi
  if [[ -z "$resolved" ]]; then
    echo "sloppy dev: no Unix package archive found; run tools/unix/dev.sh package or pass --package-path." >&2
    exit 1
  fi
  local args=(--package-path "$resolved")
  if [[ "$require_v8_runtime" -eq 1 ]]; then
    args+=(--require-v8-runtime)
  fi
  "$repo_root/tools/unix/test-install.sh" "${args[@]}"
}

npm_dry_run() {
  local resolved="$package_path"
  if [[ -z "$resolved" ]]; then
    resolved="$(ls -t "$repo_root"/artifacts/packages/sloppy-*.tar.gz 2>/dev/null | head -n 1 || true)"
  fi
  if [[ -z "$resolved" ]]; then
    echo "sloppy dev: no Unix package archive found; run tools/unix/dev.sh package or pass --package-path." >&2
    exit 1
  fi
  "$repo_root/tools/unix/npm-dry-run.sh" --package-path "$resolved"
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
  test-install) test_install ;;
  build-v8) build_v8 ;;
  npm-dry-run) npm_dry_run ;;
  dogfood) dogfood_repo ;;
  *)
    echo "sloppy dev: unknown command '$command_name'. Run tools/unix/dev.sh help for the command contract." >&2
    exit 2
    ;;
esac
