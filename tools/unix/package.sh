#!/usr/bin/env bash
set -euo pipefail

configuration="Release"
output_dir="artifacts/packages"
skip_build=0
enable_v8=0
v8_root=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --configuration)
      configuration="${2:?missing value for --configuration}"
      shift 2
      ;;
    --output-dir)
      output_dir="${2:?missing value for --output-dir}"
      shift 2
      ;;
    --skip-build)
      skip_build=1
      shift
      ;;
    --enable-v8)
      enable_v8=1
      shift
      ;;
    --v8-root)
      v8_root="${2:?missing value for --v8-root}"
      shift 2
      ;;
    -h|--help)
      cat <<'USAGE'
Usage: tools/unix/package.sh [--configuration Release|Debug] [--output-dir DIR] [--skip-build] [--enable-v8] [--v8-root DIR]

Creates an experimental local tar.gz package from already supported Unix build outputs.
When --enable-v8 is used on Linux x64, the package is built from a Sloppy-owned V8 SDK.
Static SDKs link V8 into the runtime; shared-library SDKs bundle the V8 runtime libraries
and a wrapper that runs the packaged runtime with the bundled library path.
Use tools/unix/test-package.sh --require-v8-runtime for the extracted-package JS smoke.
USAGE
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

case "$configuration" in
  Release)
    build_type="Release"
    cargo_profile="release"
    cargo_args=(build --manifest-path compiler/Cargo.toml --release)
    ;;
  Debug)
    build_type="Debug"
    cargo_profile="debug"
    cargo_args=(build --manifest-path compiler/Cargo.toml)
    ;;
  *)
    echo "--configuration must be Release or Debug" >&2
    exit 2
    ;;
esac

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
configuration_lower="$(printf '%s' "$configuration" | tr '[:upper:]' '[:lower:]')"
build_suffix="$configuration_lower"
if [[ "$enable_v8" -eq 1 ]]; then
  build_suffix="$build_suffix-v8"
fi
build_dir="$repo_root/build/unix-$build_suffix"
package_version="$(node -e 'process.stdout.write(require(process.argv[1]).version)' "$repo_root/packages/npm/sloppy/package.json")"
kernel_name="$(uname -s)"
machine_name="$(uname -m)"

case "$kernel_name" in
  Linux) platform="linux" ;;
  Darwin) platform="macos" ;;
  *) platform="$(printf '%s' "$kernel_name" | tr '[:upper:]' '[:lower:]')" ;;
esac

case "$machine_name" in
  x86_64|amd64) arch="x64" ;;
  arm64|aarch64) arch="arm64" ;;
  *) arch="$machine_name" ;;
esac
platform_triplet="$platform-$arch"
resolved_v8_root=""
resolved_v8_llvm_root=""

if [[ "$enable_v8" -eq 1 && "$platform_triplet" != "linux-x64" && "$platform_triplet" != "macos-arm64" && "$platform_triplet" != "macos-x64" ]]; then
  echo "package: --enable-v8 is currently supported only on linux-x64, macos-arm64, and macos-x64." >&2
  exit 1
fi

resolve_v8_root() {
  local args=(--mode REQUIRED --quiet)
  if [[ -n "$v8_root" ]]; then
    args+=(--v8-root "$v8_root")
  fi
  "$repo_root/tools/unix/resolve-v8-sdk.sh" "${args[@]}"
}

resolve_v8_llvm_root() {
  local candidate
  local candidates=()
  if [[ -n "${SLOPPY_V8_LLVM_ROOT:-}" ]]; then
    candidates+=("$SLOPPY_V8_LLVM_ROOT")
  fi
  candidates+=("$repo_root/.sdeps/v8-work/v8/third_party/llvm-build/Release+Asserts")

  for candidate in "${candidates[@]}"; do
    [[ -n "$candidate" ]] || continue
    if [[ -x "$candidate/bin/clang" && -x "$candidate/bin/clang++" && -x "$candidate/bin/ld.lld" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  echo "package: --enable-v8 on linux-x64 requires the V8 depot_tools LLVM toolchain." >&2
  echo "package: run tools/unix/build-v8.sh with the default work root, or set SLOPPY_V8_LLVM_ROOT to V8's third_party/llvm-build/Release+Asserts directory." >&2
  return 1
}

copy_runtime_library_with_symlink() {
  local source_path="$1"
  local destination_dir="$2"
  local link_name
  local real_path
  local real_name

  [[ -e "$source_path" ]] || return 0
  link_name="$(basename "$source_path")"
  real_path="$(readlink -f "$source_path")"
  real_name="$(basename "$real_path")"
  cp -f "$real_path" "$destination_dir/$real_name"
  if [[ "$link_name" != "$real_name" ]]; then
    ln -sfn "$real_name" "$destination_dir/$link_name"
  fi
}

cd "$repo_root"

if [[ "$skip_build" -eq 0 ]]; then
  if [[ "$enable_v8" -eq 1 ]]; then
    if [[ "$platform_triplet" == "linux-x64" ]]; then
      resolved_v8_llvm_root="$(resolve_v8_llvm_root)"
      export PATH="$resolved_v8_llvm_root/bin:$PATH"
    fi
    resolved_v8_root="$(resolve_v8_root)"
  fi
  cmake_args=(
    -S "$repo_root"
    -B "$build_dir"
    -DCMAKE_BUILD_TYPE="$build_type"
  )
  if [[ "$enable_v8" -eq 1 ]]; then
    cmake_args+=(
      -DSLOPPY_ENABLE_V8=ON
      -DSLOPPY_ENGINE=v8
      "-DSLOPPY_V8_ROOT=$resolved_v8_root"
    )
    if [[ "$platform_triplet" == "linux-x64" ]]; then
      cmake_args+=(
        "-DCMAKE_C_COMPILER=$resolved_v8_llvm_root/bin/clang"
        "-DCMAKE_CXX_COMPILER=$resolved_v8_llvm_root/bin/clang++"
        "-DCMAKE_CXX_FLAGS=-nostdinc++ -isystem$resolved_v8_root/support/libcxx/buildtools -isystem$resolved_v8_root/support/libcxx/include -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE"
      )
    fi
  else
    cmake_args+=(
      -DSLOPPY_ENABLE_V8=OFF
      -DSLOPPY_ENGINE=none
    )
  fi
  vcpkg_root="${VCPKG_ROOT:-$repo_root/.sdeps/vcpkg}"
  vcpkg_toolchain="$vcpkg_root/scripts/buildsystems/vcpkg.cmake"
  if [[ -f "$vcpkg_toolchain" ]]; then
    cmake_args+=("-DCMAKE_TOOLCHAIN_FILE=$vcpkg_toolchain")
  fi
  if command -v ninja >/dev/null 2>&1; then
    cmake_args+=(
      -G Ninja
      "-DCMAKE_MAKE_PROGRAM=$(command -v ninja)"
    )
  fi
  if command -v ccache >/dev/null 2>&1; then
    cmake_args+=(
      -DCMAKE_C_COMPILER_LAUNCHER=ccache
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    )
  fi
  cmake "${cmake_args[@]}"
  cmake --build "$build_dir"
  cargo "${cargo_args[@]}"
fi

sloppy_bin="$build_dir/sloppy"
sloppyc_bin="$repo_root/compiler/target/$cargo_profile/sloppyc"
[[ -x "$sloppy_bin" ]] || { echo "missing built sloppy executable: $sloppy_bin" >&2; exit 1; }
[[ -x "$sloppyc_bin" ]] || { echo "missing built sloppyc executable: $sloppyc_bin" >&2; exit 1; }

package_name="sloppy-$platform-$arch"
case "$output_dir" in
  /*) output_root="$output_dir" ;;
  *) output_root="$repo_root/$output_dir" ;;
esac
stage_root="$output_root/stage/$package_name"
rm -rf "$stage_root"
mkdir -p "$stage_root/bin" "$stage_root/stdlib" "$stage_root/templates" "$stage_root/examples" "$stage_root/docs"

cp "$sloppy_bin" "$stage_root/bin/sloppy"
cp "$sloppyc_bin" "$stage_root/bin/sloppyc"
cp -R "$repo_root/stdlib/sloppy" "$stage_root/stdlib/sloppy"
cp -R "$repo_root/templates/." "$stage_root/templates/"
cp -R "$repo_root/examples/." "$stage_root/examples/"
cp "$repo_root/LICENSE" "$stage_root/LICENSE"
cat > "$stage_root/docs/KNOWN_LIMITATIONS.md" <<'LIMITATIONS'
# Known Limitations

This package is an experimental pre-alpha development artifact.

- Publishing uses a separate release step.
- Production readiness is tracked separately.
- Full Node, Bun, and Deno compatibility, package-manager installs, and runtime
  node_modules discovery are separate tracks.
- V8 execution, live provider readiness, TLS hardening, and release readiness use their own
  lanes.
- V8 SDK headers, import libraries, and source/build trees are intentionally excluded.
- V8 runtime support is included only in packages built with a matching Sloppy-owned SDK
  and validated by the V8 package smoke lane.
- PostgreSQL and SQL Server live-provider behavior requires separate opt-in evidence.
- Signing, notarization, installers, auto-update, and package-manager distribution are not
  included.
LIMITATIONS
cat > "$stage_root/docs/LICENSES.md" <<'LICENSES'
# Licenses

This experimental package includes Sloppy source-license text in the repository root
LICENSE file and may include runtime dependencies provided by the host build environment.

Complete third-party license review remains required before publishing.
LICENSES
cat > "$stage_root/docs/NOTICE.md" <<'NOTICES'
# Notice

This experimental local package may include runtime dependencies provided by the
host build environment. It does not include V8 SDK headers/import libraries,
database drivers, package manager metadata, installers, or signed release metadata.

Dependency license review and a complete release notice file are still required
before publishing.
NOTICES
cat > "$stage_root/README.md" <<'README'
# Sloppy Local Package

This is an experimental development artifact for local validation. It does not install
anything, fetch dependencies, bundle a V8 SDK, provide package-manager behavior, or change
production readiness status.
README

contains_v8_runtime=false
contains_native_runtime_dependencies=false
v8_status="not bundled"
v8_runtime_status="not bundled"
v8_notes="no V8 runtime"
runtime_dependency_status="host-linked or system-provided"
enabled_features='"native-runtime", "stdlib", "compiler"'
runtime_dependency_dir=""
if [[ "$enable_v8" -eq 1 ]]; then
  if [[ -z "$resolved_v8_root" ]]; then
    resolved_v8_root="$(resolve_v8_root)"
  fi
  contains_v8_runtime=true
  v8_status="linked runtime"
  v8_runtime_status="linked into packaged runtime"
  v8_notes="$platform_triplet V8 runtime is provided by the Sloppy-owned SDK and linked into the packaged runtime"
  runtime_dependency_status="V8 linked into packaged runtime; platform system libraries remain host-provided"
  enabled_features='"native-runtime", "stdlib", "compiler", "v8"'
  dynamic_v8_runtime_count=0
  for lib in "$resolved_v8_root"/lib/*.so "$resolved_v8_root"/lib/*.so.* "$resolved_v8_root"/bin/*.so "$resolved_v8_root"/bin/*.so.*; do
    [[ -e "$lib" ]] || continue
    if [[ "$dynamic_v8_runtime_count" -eq 0 ]]; then
      runtime_dependency_dir="engines/v8"
      mkdir -p "$stage_root/engines/v8"
    fi
    copy_runtime_library_with_symlink "$lib" "$stage_root/engines/v8"
    dynamic_v8_runtime_count=$((dynamic_v8_runtime_count + 1))
  done
  if [[ "$dynamic_v8_runtime_count" -gt 0 ]]; then
    contains_native_runtime_dependencies=true
    v8_status="bundled shared runtime"
    v8_runtime_status="bundled shared libraries"
    v8_notes="Linux V8 runtime shared libraries are bundled for extracted-package smoke"
    runtime_dependency_status="bundled V8 shared libraries plus platform system libraries"
    rm -f "$stage_root/bin/sloppy"
    cp "$sloppy_bin" "$stage_root/bin/sloppy.bin"
    cat > "$stage_root/bin/sloppy" <<'WRAPPER'
#!/usr/bin/env bash
set -euo pipefail
bin_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
package_root="$(cd "$bin_dir/.." && pwd)"
export PATH="$bin_dir:${PATH:-}"
export LD_LIBRARY_PATH="$package_root/engines/v8:$bin_dir:${LD_LIBRARY_PATH:-}"
exec "$bin_dir/sloppy.bin" "$@"
WRAPPER
    chmod +x "$stage_root/bin/sloppy"
  fi
fi

commit="$(git rev-parse --short HEAD 2>/dev/null || printf 'unknown')"
cat > "$stage_root/manifest.json" <<JSON
{
  "manifestSchema": "sloppy.release-artifact.v1",
  "manifestVersion": 1,
  "name": "sloppy",
  "version": "$package_version",
  "archiveName": "$package_name.tar.gz",
  "packageRoot": "$package_name",
  "platform": "$platform",
  "arch": "$arch",
  "platformTriplet": "$platform_triplet",
  "configuration": "$configuration",
  "commit": "$commit",
  "releaseKind": "dry-run",
  "publicReleaseCreated": false,
  "canonicalDistribution": "github-release-archive",
  "npmPackageSource": "platform packages must be generated from this tested archive content",
  "platformStatus": "experimental",
  "runtimeUserStatus": "$([[ "$contains_v8_runtime" == true ]] && printf 'experimental V8 package smoke required' || ([[ "$platform" == "linux" && "$arch" == "x64" ]] && printf 'experimental package smoke; V8 runtime app execution remains a separate lane' || printf 'experimental pending hosted package smoke evidence'))",
  "compiler": {
    "name": "sloppyc",
    "profile": "$cargo_profile",
    "included": true
  },
  "v8": {
    "sdkIncluded": false,
    "runtimeIncluded": $contains_v8_runtime,
    "status": "$v8_status",
    "version": "pinned by tools/deps/sloppy-deps.json",
    "runtimeDirectory": "$runtime_dependency_dir",
    "notes": "$v8_notes"
  },
  "enabledFeatures": [$enabled_features],
  "dependencyStatuses": {
    "nativeRuntimeDependencies": "$runtime_dependency_status",
    "v8Sdk": "excluded",
    "v8Runtime": "$v8_runtime_status",
    "liveProviders": "not configured",
    "runtimeDependencyAudit": "docs/release/runtime-dependency-audit.json"
  },
  "providers": {
    "sqlite": "packaged runtime dependency status only; provider behavior evidence is separate",
    "postgresql": "live-provider evidence is separate",
    "sqlserver": "driver/runtime availability evidence is separate"
  },
  "containsV8Runtime": $contains_v8_runtime,
  "containsV8Sdk": false,
  "containsStdlib": true,
  "containsTemplates": true,
  "containsExamples": true,
  "containsNativeRuntimeDependencies": $contains_native_runtime_dependencies,
  "knownLimitations": "docs/KNOWN_LIMITATIONS.md",
  "checksums": {
    "file": "SHA256SUMS.txt",
    "algorithm": "SHA-256"
  },
  "tools": ["sloppy", "sloppyc"],
  "layoutVersion": 1,
  "notes": ["experimental", "dry-run artifact", "production readiness tracked separately", "dry-run only", "no installer", "no package manager", "npm launcher packages may reuse this archive but do not install app dependencies or add runtime node_modules discovery"]
}
JSON

mkdir -p "$output_root"
archive_path="$output_root/$package_name.tar.gz"
tar -C "$output_root/stage" -czf "$archive_path" "$package_name"

if command -v sha256sum >/dev/null 2>&1; then
  (cd "$output_root" && sha256sum "$(basename "$archive_path")" > SHA256SUMS.txt)
else
  (cd "$output_root" && shasum -a 256 "$(basename "$archive_path")" > SHA256SUMS.txt)
fi

echo "Created package: $archive_path"
cat "$output_root/SHA256SUMS.txt"
