# Package Management Reference

Sloppy package management is modeled after deterministic restore systems such
as NuGet restore. It is not an npm or `node_modules` compatibility layer.

The package manager restores immutable `.slpkg` artifacts from explicit local
sources into a global cache, writes `sloppy.lock.json`, and writes
`.sloppy/obj/project.assets.json` for compiler and runtime consumers.

## Philosophy

Sloppy package restore is intentionally boring:

- no `node_modules`
- no package install scripts
- no arbitrary package-executed code
- immutable package artifacts
- deterministic local restore
- lockfile-backed dependency selection
- package contents in a global cache instead of copied into every project
- generated assets graph for compiler/runtime lookup

Future npm compatibility belongs to a separate foreign-source adapter. It is
not part of the `.slpkg` restore contract.

## Commands

### `sloppy pack`

Creates a `.slpkg` package artifact from the current directory's
`sloppy.json`.

Output path:

```text
artifacts/packages/<normalized-id>.<version>.slpkg
```

The archive is a zip file with deterministic file ordering. It contains
`manifest.json` at the package root and only the files declared by package
metadata.

### `sloppy restore`

Restores dependencies from local package sources declared in `sloppy.json`.

Restore writes:

```text
sloppy.lock.json
.sloppy/obj/project.assets.json
```

Restore never downloads from the network and never executes package code.

### `sloppy restore --locked`

Runs restore against the existing `sloppy.lock.json`.

The command fails if:

- `sloppy.lock.json` is missing
- current dependency constraints would change the lockfile
- package artifact hashes no longer match the locked result
- `sloppy.json` and the lockfile disagree

When locked restore succeeds, it may refresh
`.sloppy/obj/project.assets.json` from the locked graph.

## Project Manifest Fields

Project package restore uses `sloppy.json`.

```json
{
  "name": "my-app",
  "entry": "src/main.ts",
  "target": "sloppy1.0",
  "runtimeIdentifier": "win-x64",
  "packageSources": [
    "./packages"
  ],
  "dependencies": {
    "Sloppy.Example": "[0.1.0]"
  }
}
```

Supported restore fields:

| Field | Required | Meaning |
| --- | --- | --- |
| `target` | Optional | Restore target. Defaults to `sloppy1.0`. |
| `runtimeIdentifier` | Optional when host RID can be detected | Runtime identifier used for native asset selection. |
| `packageSources` | Yes for restore | Local folders containing `.slpkg` artifacts. |
| `dependencies` | Optional | Direct package dependency map from package id to version range. |

Supported target:

- `sloppy1.0`

Supported runtime identifiers:

- `win-x64`
- `win-arm64`
- `linux-x64`
- `linux-arm64`
- `macos-x64`
- `macos-arm64`

If `runtimeIdentifier` is omitted and Sloppy cannot map the current host to
one of these values, restore fails and asks for an explicit RID.

## Package Manifest

`sloppy pack` reads package metadata from `sloppy.json` and writes it as
package-root `manifest.json` inside the `.slpkg` artifact.

Supported package fields:

| Field | Required | Meaning |
| --- | --- | --- |
| `id` | Yes | Package id. Matching is case-insensitive through normalized ids. |
| `version` | Yes | Package version in `major.minor.patch` form. |
| `description` | No | Human-readable package description. |
| `authors` | No | Array of author names. |
| `dependencies` | No | Package dependency map. |
| `targets` | No | Compile/runtime assets by target. |
| `native` | No | Native library metadata by logical name and RID. |
| `capabilities` | No | Package capability declarations. |

Example:

```json
{
  "id": "Sloppy.Example",
  "version": "0.1.0",
  "description": "Example Sloppy package",
  "authors": ["RtlZeroMemory"],
  "dependencies": {
    "Sloppy.Core": "[0.1.0]"
  },
  "targets": {
    "sloppy1.0": {
      "compile": ["lib/sloppy1.0/index.ts"],
      "runtime": []
    }
  },
  "native": {
    "libraries": {
      "example": {
        "win-x64": "native/win-x64/example.dll",
        "linux-x64": "native/linux-x64/libexample.so",
        "macos-arm64": "native/macos-arm64/libexample.dylib"
      }
    }
  },
  "capabilities": [
    "ffi/native"
  ]
}
```

## Version Ranges

This foundation supports a small NuGet-style subset:

| Range | Meaning |
| --- | --- |
| `[1.2.3]` | Exact version. |
| `[1.0.0,2.0.0)` | Inclusive lower bound, exclusive upper bound. |
| `[1.0.0,)` | Inclusive lower bound with no upper bound. |

Unsupported range forms fail with `SLOPPY_E_PACKAGE_RANGE_INVALID`.

Restore prefers the locked version when a lockfile exists and the current
constraints still allow that version. Sloppy does not install multiple
versions of the same package in this foundation. Direct or transitive conflicts
fail with `SLOPPY_E_PACKAGE_CONFLICT`.

## Package Sources

Only local folder sources are supported in this PR.

```json
{
  "packageSources": [
    "./packages",
    "C:/local/sloppy-packages"
  ]
}
```

Restore scans declared source folders for `.slpkg` files. It does not contact a
remote registry.

## Global Cache

Restored package contents live in the user's global Sloppy package cache.

Windows:

```text
%USERPROFILE%/.sloppy/packages/<normalized-id>/<version>/
```

Unix and macOS:

```text
~/.sloppy/packages/<normalized-id>/<version>/
```

Existing cached packages are reused only when the selected artifact hash
matches the cache marker. A mismatch fails with
`SLOPPY_E_PACKAGE_HASH_MISMATCH`. Restore does not copy package contents into
the project root.

## Lockfile

`sloppy.lock.json` is versioned. Version 1 records the deterministic restore
result.

It includes:

- lockfile version
- target
- runtime identifier
- selected packages by normalized id/version
- source path
- package artifact hash
- dependency ranges
- selected compile, runtime, and native assets
- selected asset hashes
- capabilities

Conceptual shape:

```json
{
  "version": 1,
  "target": "sloppy1.0",
  "runtimeIdentifier": "win-x64",
  "packages": {
    "sloppy.example/0.1.0": {
      "id": "Sloppy.Example",
      "version": "0.1.0",
      "source": "./packages",
      "sha256": "...",
      "dependencies": {},
      "assets": {
        "compile": ["lib/sloppy1.0/index.ts"],
        "runtime": [],
        "native": {
          "example": "native/win-x64/example.dll"
        }
      },
      "assetHashes": {
        "lib/sloppy1.0/index.ts": "...",
        "native/win-x64/example.dll": "..."
      },
      "capabilities": ["ffi/native"]
    }
  }
}
```

## Project Assets File

`.sloppy/obj/project.assets.json` is the compiler/runtime-facing restore
result.

It includes:

- target
- runtime identifier
- restored package roots
- compile assets
- runtime assets
- native library assets by logical name
- selected native asset hashes
- package capabilities

Conceptual shape:

```json
{
  "version": 1,
  "target": "sloppy1.0",
  "runtimeIdentifier": "win-x64",
  "packages": [
    {
      "id": "Sloppy.Example",
      "version": "0.1.0",
      "path": "C:/Users/example/.sloppy/packages/sloppy.example/0.1.0",
      "compile": [
        "lib/sloppy1.0/index.ts"
      ],
      "runtime": [],
      "nativeLibraries": {
        "example": {
          "path": "native/win-x64/example.dll",
          "sha256": "..."
        }
      },
      "capabilities": [
        "ffi/native"
      ]
    }
  ]
}
```

## Native Assets

Packages can declare native libraries for future FFI/native package work:

```json
{
  "native": {
    "libraries": {
      "example": {
        "win-x64": "native/win-x64/example.dll"
      }
    }
  }
}
```

Restore selects the path matching `runtimeIdentifier`, records it in the
lockfile, records it in `project.assets.json`, and hashes the selected native
file. Restore does not load native libraries, execute native code, auto-bind
FFI, expose native addresses, or create runtime pointers.

Missing selected RID assets fail with
`SLOPPY_E_PACKAGE_NATIVE_ASSET_MISSING`.

## Security Restrictions

Package restore rejects:

- absolute package asset paths
- package paths containing `..`
- package paths escaping the package root during extraction
- duplicate normalized package paths
- unsupported `install`, `postinstall`, `preinstall`, `prepare`, or `scripts`
  declarations
- missing declared assets
- missing selected native assets
- package hash mismatches

Restore never executes package code and never runs install scripts.

## Diagnostics

Package-manager errors use stable `SLOPPY_E_PACKAGE_*` diagnostics.

Common diagnostics:

| Code | Meaning |
| --- | --- |
| `SLOPPY_E_PACKAGE_MANIFEST_INVALID` | Manifest JSON or shape is invalid. |
| `SLOPPY_E_PACKAGE_ID_INVALID` | Package id is missing or malformed. |
| `SLOPPY_E_PACKAGE_VERSION_INVALID` | Version is not supported. |
| `SLOPPY_E_PACKAGE_RANGE_INVALID` | Version range form is not supported. |
| `SLOPPY_E_PACKAGE_SOURCE_MISSING` | A local package source is missing. |
| `SLOPPY_E_PACKAGE_NOT_FOUND` | No package source contained the requested package. |
| `SLOPPY_E_PACKAGE_CONFLICT` | Dependency constraints cannot be satisfied. |
| `SLOPPY_E_PACKAGE_HASH_MISMATCH` | Cached or locked package hashes disagree. |
| `SLOPPY_E_PACKAGE_PATH_TRAVERSAL` | Package path is unsafe. |
| `SLOPPY_E_PACKAGE_DUPLICATE_PATH` | Two archive paths normalize to the same package path. |
| `SLOPPY_E_PACKAGE_SCRIPT_UNSUPPORTED` | Package script declaration is not allowed. |
| `SLOPPY_E_PACKAGE_NATIVE_ASSET_MISSING` | Native asset for the selected RID is missing. |
| `SLOPPY_E_PACKAGE_LOCK_OUT_OF_DATE` | `--locked` restore would change the lockfile. |

## Current Limitations

This foundation does not include:

- remote registry support
- npm package restore
- `node_modules`
- install or postinstall scripts
- package signing
- vulnerability database checks
- workspaces or monorepo restore
- tools, templates, analyzers, or source generators
- native asset execution
- FFI auto-binding
- broad `sloppy.json` redesign outside package restore
