#!/usr/bin/env bash
set -euo pipefail

depot_tools_root=".sdeps/depot_tools"
work_root=".sdeps/v8-work"
sdk_root=""
archive_dir="artifacts/v8-sdk"
v8_revision="7221f49fdb6c89cce6be08005732ebcab3c45b38"
cr_libcxx_revision="af4386908c3762433d412689038de6e6333f5921"
skip_fetch=0
skip_build=0
package_only=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --depot-tools-root)
      depot_tools_root="${2:?missing value for --depot-tools-root}"
      shift 2
      ;;
    --work-root)
      work_root="${2:?missing value for --work-root}"
      shift 2
      ;;
    --sdk-root)
      sdk_root="${2:?missing value for --sdk-root}"
      shift 2
      ;;
    --archive-dir)
      archive_dir="${2:?missing value for --archive-dir}"
      shift 2
      ;;
    --v8-revision)
      v8_revision="${2:?missing value for --v8-revision}"
      shift 2
      ;;
    --skip-fetch)
      skip_fetch=1
      shift
      ;;
    --skip-build)
      skip_build=1
      shift
      ;;
    --package-only)
      package_only=1
      shift
      ;;
    -h|--help)
      cat <<'USAGE'
Usage: tools/unix/build-v8.sh [--depot-tools-root DIR] [--work-root DIR] [--sdk-root DIR] [--archive-dir DIR]
                              [--v8-revision SHA] [--skip-fetch] [--skip-build] [--package-only]

Builds and packages Sloppy's pinned Unix V8 SDK. The default SDK root is
.sdeps/v8/<platform-arch>, which tools/unix/resolve-v8-sdk.sh and tools/unix/dev.sh --enable-v8
consume. This script intentionally does not adapt distro Node/V8 development packages.
USAGE
      exit 0
      ;;
    *)
      echo "build-v8: unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
depot_tools_git="https://chromium.googlesource.com/chromium/tools/depot_tools.git"

resolve_repo_path() {
  local path="$1"
  if [[ -z "$path" ]]; then
    echo "build-v8: path value must not be empty" >&2
    exit 2
  fi
  case "$path" in
    /*) printf '%s\n' "$path" ;;
    *) printf '%s\n' "$repo_root/$path" ;;
  esac
}

assert_safe_local_path() {
  local name="$1"
  local path="$2"
  local full
  local parent
  parent="$(dirname "$path")"
  mkdir -p "$parent"
  full="$(cd "$parent" && pwd)/$(basename "$path")"
  case "$full" in
    ""|"/"|"$repo_root"|"$repo_root/."|"$repo_root/..")
      echo "build-v8: refusing to use unsafe $name: $path" >&2
      exit 1
      ;;
  esac
  printf '%s\n' "$full"
}

assert_tool() {
  local tool="$1"
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "build-v8: missing required tool: $tool" >&2
    exit 1
  fi
}

read_command_lines() {
  local output_array_name="$1"
  shift
  local line
  eval "$output_array_name=()"
  while IFS= read -r line; do
    eval "$output_array_name+=(\"\$line\")"
  done < <("$@")
}

resolve_archive_tool() {
  local v8_llvm_ar="$v8_checkout/third_party/llvm-build/Release+Asserts/bin/llvm-ar"
  if [[ -x "$v8_llvm_ar" ]]; then
    printf '%s\n' "$v8_llvm_ar"
    return 0
  fi
  if command -v llvm-ar >/dev/null 2>&1; then
    command -v llvm-ar
    return 0
  fi
  if command -v xcrun >/dev/null 2>&1; then
    local xcrun_llvm_ar
    xcrun_llvm_ar="$(xcrun -f llvm-ar 2>/dev/null || true)"
    if [[ -n "$xcrun_llvm_ar" && -x "$xcrun_llvm_ar" ]]; then
      printf '%s\n' "$xcrun_llvm_ar"
      return 0
    fi
  fi
  command -v ar
}

copy_required_file() {
  local source="$1"
  local destination="$2"
  if [[ ! -f "$source" ]]; then
    echo "build-v8: expected build output is missing: $source" >&2
    exit 1
  fi
  cp -f "$source" "$destination"
}

copy_static_archive_as_full() {
  local source="$1"
  local destination="$2"
  local member_root="$3"
  local archive_tool
  local members=()
  local object_paths=()
  local member

  if [[ ! -f "$source" ]]; then
    echo "build-v8: expected build output is missing: $source" >&2
    exit 1
  fi
  archive_tool="$(resolve_archive_tool)"
  if ! file "$source" | grep -q "thin archive"; then
    cp -f "$source" "$destination"
    return
  fi
  read_command_lines members "$archive_tool" t "$source"
  if [[ "${#members[@]}" -eq 0 ]]; then
    echo "build-v8: archive has no members: $source" >&2
    exit 1
  fi
  for member in "${members[@]}"; do
    [[ -n "$member" ]] || continue
    if [[ "$member" = /* ]]; then
      [[ -f "$member" ]] || { echo "build-v8: archive member is missing: $member" >&2; exit 1; }
      object_paths+=("$member")
    elif [[ -f "$member_root/$member" ]]; then
      object_paths+=("$member")
    else
      local matches=()
      read_command_lines matches find "$member_root" -name "$member" -type f
      if [[ "${#matches[@]}" -eq 1 ]]; then
        object_paths+=("${matches[0]}")
      else
        echo "build-v8: archive member is missing or ambiguous under $member_root: $member" >&2
        exit 1
      fi
    fi
  done
  rm -f "$destination"
  (cd "$member_root" && "$archive_tool" crs "$destination" "${object_paths[@]}")
}

archive_objects() {
  local object_root="$1"
  local destination="$2"
  local description="$3"
  local objects=()

  if [[ ! -d "$object_root" ]] || ! find "$object_root" -name '*.o' -type f -print -quit | grep -q .; then
    echo "build-v8: no $description object files found under $object_root." >&2
    exit 1
  fi

  read_command_lines objects find "$object_root" -name '*.o' -type f
  rm -f "$destination"
  "$(resolve_archive_tool)" crs "$destination" "${objects[@]}"
}

kernel_name="$(uname -s)"
machine_name="$(uname -m)"
case "$kernel_name:$machine_name" in
  Linux:x86_64|Linux:amd64) platform="linux-x64"; v8_cpu="x64" ;;
  Darwin:arm64|Darwin:aarch64) platform="macos-arm64"; v8_cpu="arm64" ;;
  Darwin:x86_64|Darwin:amd64) platform="macos-x64"; v8_cpu="x64" ;;
  *) echo "build-v8: unsupported V8 SDK platform: $kernel_name $machine_name" >&2; exit 1 ;;
esac

write_gn_args() {
  local args_path="$1"
  local custom_libcxx="false"
  local enable_sandbox="false"
  if [[ "$platform" == "linux-x64" ]]; then
    custom_libcxx="true"
    enable_sandbox="true"
  fi
  cat > "$args_path" <<GNARGS
is_debug = false
target_cpu = "$v8_cpu"
v8_target_cpu = "$v8_cpu"
is_component_build = false
v8_monolithic = true
v8_use_external_startup_data = false
v8_enable_temporal_support = false
v8_enable_webassembly = false
v8_enable_sandbox = $enable_sandbox
use_custom_libcxx = $custom_libcxx
use_thin_archives = false
use_allocator_shim = false
treat_warnings_as_errors = false
symbol_level = 1
GNARGS
}

for tool in git python3 cmake ninja tar file; do
  assert_tool "$tool"
done
if [[ "$platform" == "linux-x64" ]]; then
  assert_tool pkg-config
  if ! pkg-config --exists glib-2.0 gmodule-2.0 gobject-2.0 gthread-2.0; then
    echo "build-v8: missing GLib development pkg-config entries; install libglib2.0-dev." >&2
    exit 1
  fi
fi

if [[ -z "$sdk_root" ]]; then
  sdk_root=".sdeps/v8/$platform"
fi
depot_tools_root="$(assert_safe_local_path "DepotToolsRoot" "$(resolve_repo_path "$depot_tools_root")")"
work_root="$(assert_safe_local_path "WorkRoot" "$(resolve_repo_path "$work_root")")"
sdk_root="$(assert_safe_local_path "SdkRoot" "$(resolve_repo_path "$sdk_root")")"
archive_dir="$(assert_safe_local_path "ArchiveDir" "$(resolve_repo_path "$archive_dir")")"
v8_checkout="$work_root/v8"
v8_build_dir="$v8_checkout/out.gn/$platform.release"
needs_depot_tools=0
if [[ "$package_only" -eq 0 && ( "$skip_fetch" -eq 0 || "$skip_build" -eq 0 ) ]]; then
  needs_depot_tools=1
fi

if [[ "$needs_depot_tools" -eq 1 ]]; then
  if [[ ! -d "$depot_tools_root" ]]; then
    echo "Cloning depot_tools into $depot_tools_root"
    mkdir -p "$(dirname "$depot_tools_root")"
    git clone "$depot_tools_git" "$depot_tools_root"
  fi
  export PATH="$depot_tools_root:$PATH"
  gclient --version >/dev/null
fi

if [[ "$package_only" -eq 1 ]]; then
  [[ -d "$v8_checkout" ]] || { echo "build-v8: --package-only requires $v8_checkout" >&2; exit 1; }
  [[ -d "$v8_build_dir" ]] || { echo "build-v8: --package-only requires $v8_build_dir" >&2; exit 1; }
else
  mkdir -p "$work_root"
fi

if [[ "$skip_fetch" -eq 0 && "$package_only" -eq 0 ]]; then
  if [[ ! -d "$v8_checkout" ]]; then
    echo "Fetching V8 source into $work_root"
    (cd "$work_root" && fetch v8)
  else
    echo "Updating existing V8 checkout at $v8_checkout"
    git -C "$v8_checkout" fetch origin
  fi
  git -C "$v8_checkout" checkout "$v8_revision"
  (cd "$v8_checkout" && gclient sync)
fi

if [[ "$package_only" -eq 0 ]]; then
  mkdir -p "$v8_build_dir"
  write_gn_args "$v8_build_dir/args.gn"
fi

if [[ "$skip_build" -eq 0 && "$package_only" -eq 0 ]]; then
  (cd "$v8_checkout" && gn gen "out.gn/$platform.release")
  (cd "$v8_checkout" && ninja -C "out.gn/$platform.release" v8_monolith v8_libplatform v8_libbase)
fi

revision="$(git -C "$v8_checkout" rev-parse HEAD)"
if [[ "$revision" != "$v8_revision" ]]; then
  echo "build-v8: V8 checkout revision $revision does not match pinned revision $v8_revision." >&2
  exit 1
fi

rm -rf "$sdk_root"
mkdir -p "$sdk_root/include" "$sdk_root/lib" "$sdk_root/share"
cp -R "$v8_checkout/include/." "$sdk_root/include/"
copy_static_archive_as_full "$v8_build_dir/obj/libv8_monolith.a" "$sdk_root/lib/libv8_monolith.a" "$v8_build_dir/obj"
copy_static_archive_as_full "$v8_build_dir/obj/libv8_libplatform.a" "$sdk_root/lib/libv8_libplatform.a" "$v8_build_dir/obj"
copy_static_archive_as_full "$v8_build_dir/obj/libv8_libbase.a" "$sdk_root/lib/libv8_libbase.a" "$v8_build_dir/obj"
if [[ -f "$v8_build_dir/obj/libv8_libsampler.a" ]]; then
  copy_static_archive_as_full "$v8_build_dir/obj/libv8_libsampler.a" "$sdk_root/lib/libv8_libsampler.a" "$v8_build_dir/obj"
fi
if [[ "$platform" == "linux-x64" ]]; then
  mkdir -p "$sdk_root/support/libcxx/include" "$sdk_root/support/libcxx/buildtools"
  archive_objects "$v8_build_dir/obj/buildtools/third_party/libc++/libc++" "$sdk_root/lib/libc++.a" "libc++"
  archive_objects "$v8_build_dir/obj/buildtools/third_party/libc++abi/libc++abi" "$sdk_root/lib/libc++abi.a" "libc++abi"
  cp -R "$v8_checkout/third_party/libc++/src/include/." "$sdk_root/support/libcxx/include/"
  copy_required_file "$v8_checkout/buildtools/third_party/libc++/__config_site" "$sdk_root/support/libcxx/buildtools/__config_site"
  copy_required_file "$v8_checkout/buildtools/third_party/libc++/__assertion_handler" "$sdk_root/support/libcxx/buildtools/__assertion_handler"
fi
copy_required_file "$v8_build_dir/v8_features.json" "$sdk_root/share/v8_features.json"
copy_required_file "$v8_build_dir/v8_build_config.json" "$sdk_root/share/v8_build_config.json"

python3 - "$sdk_root/share/v8_features.json" "$sdk_root/share/sloppy-v8-sdk.json" "$revision" "$cr_libcxx_revision" "$platform" "$v8_cpu" <<'PY'
import json
import sys

features_path, manifest_path, revision, cr_libcxx_revision, platform, v8_cpu = sys.argv[1:7]
with open(features_path, encoding="utf-8") as handle:
    features = json.load(handle)

crt = "glibc clang-libc++ static-v8" if platform == "linux-x64" else "darwin system-libc++ static-v8"
manifest = {
    "name": "sloppy-v8-sdk",
    "platform": platform,
    "source": "sloppy-built-v8",
    "v8Revision": revision,
    "buildType": "release",
    "crtCompatibility": crt,
    "abi": {
        "crLibcxxRevision": cr_libcxx_revision,
        "v8TargetArch": v8_cpu,
        "v8CompressPointers": bool(features["v8_enable_pointer_compression"]),
        "v8CompressPointersInSharedCage": bool(features["v8_enable_pointer_compression_shared_cage"]),
        "v8_31BitSmisOn64BitArch": bool(features["v8_enable_31bit_smis_on_64bit_arch"]),
        "v8EnableSandbox": bool(features["v8_enable_sandbox"]),
        "v8EnableI18nSupport": bool(features["v8_enable_i18n_support"]),
        "v8EnableWebAssembly": bool(features["v8_enable_webassembly"]),
    },
    "publicFeatureMetadata": "share/v8_features.json",
    "buildConfigMetadata": "share/v8_build_config.json",
    "gnArgs": [
        "is_debug=false",
        f"target_cpu={v8_cpu}",
        f"v8_target_cpu={v8_cpu}",
        "is_component_build=false",
        "v8_monolithic=true",
        "v8_use_external_startup_data=false",
        "v8_enable_temporal_support=false",
        "v8_enable_webassembly=false",
        f"v8_enable_sandbox={'true' if platform == 'linux-x64' else 'false'}",
        f"use_custom_libcxx={'true' if platform == 'linux-x64' else 'false'}",
        "use_thin_archives=false",
        "use_allocator_shim=false",
        "treat_warnings_as_errors=false",
        "symbol_level=1",
    ],
}

with open(manifest_path, "w", encoding="utf-8") as handle:
    json.dump(manifest, handle, indent=2)
    handle.write("\n")
PY

"$repo_root/tools/unix/resolve-v8-sdk.sh" --mode REQUIRED --v8-root "$sdk_root" --quiet >/dev/null

mkdir -p "$archive_dir"
archive_name="sloppy-v8-sdk-$platform-v8-$revision.tar.gz"
archive_path="$archive_dir/$archive_name"
tar -C "$(dirname "$sdk_root")" -czf "$archive_path" "$(basename "$sdk_root")"
if command -v sha256sum >/dev/null 2>&1; then
  (cd "$archive_dir" && sha256sum "$archive_name" > "$archive_name.sha256")
else
  (cd "$archive_dir" && shasum -a 256 "$archive_name" > "$archive_name.sha256")
fi

echo ""
echo "Packaged Sloppy V8 SDK: $sdk_root"
echo "Archive: $archive_path"
echo "Configure/package with:"
echo "  tools/unix/dev.sh configure --enable-v8"
echo "  tools/unix/dev.sh package --enable-v8"
