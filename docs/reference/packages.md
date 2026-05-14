# Package Management Reference

Sloppy package management is modeled after deterministic restore systems such
as NuGet restore. It is not an npm or `node_modules` compatibility layer.

The package manager restores immutable `.slpkg` artifacts from explicit
project sources into a global cache, writes `sloppy.lock.json`, and writes
`.sloppy/obj/project.assets.json` for compiler and runtime consumers.

## Philosophy

Sloppy package restore is intentionally boring:

- no `node_modules`
- no package install scripts
- no arbitrary package-executed code
- immutable package artifacts
- deterministic restore from local folders and static-feed folders
- lockfile-backed dependency selection
- package contents in a global cache instead of copied into every project
- generated assets graph for compiler/runtime lookup

Npm compatibility is treated as a separate foreign-source adapter. It is not
part of the native `.slpkg` restore contract and does not create
`node_modules`.

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

### Dependency graph commands

The package-manager CLI also includes project graph helpers:

```text
sloppy add <package-id> [--version <range>] [--source <name-or-path>] [--native-ok]
sloppy add <path-to-package.slpkg> [--native-ok]
sloppy remove <package-id>
sloppy update [package-id]
sloppy list packages
sloppy list native
sloppy list capabilities
sloppy list outdated
sloppy why <package-id>
```

`add` and `remove` edit `sloppy.json`. `update` runs restore using the current
source set. `list` and `why` inspect `sloppy.lock.json` and
`.sloppy/obj/project.assets.json`; they do not execute package code.

### Cache and source commands

```text
sloppy cache list
sloppy cache clean [--all | <package-id>]
sloppy source list
sloppy source add <name> <url-or-path> [--type folder|sloppy|npm]
sloppy source remove <name>
```

Cache clean removes entries from the global Sloppy package cache only. Source
commands edit project-level `packageSources`.

### Feed commands

```text
sloppy publish <path-to-package.slpkg> --source <folder-source>
sloppy feed index <folder>
```

These commands generate a static Sloppy feed layout in a folder. They do not
perform authenticated remote publish.

### Npm scaffold

```text
sloppy npm add <package> [--version <range>]
```

This records an explicit `npm:` foreign-source dependency and an npm source in
`sloppy.json`. Full npm restore is not implemented in this PR; `sloppy restore`
fails cleanly with `SLOPPY_E_PACKAGE_SOURCE_UNSUPPORTED` for npm dependencies.

## Project Manifest Fields

Project package restore uses `sloppy.json`.

```json
{
  "name": "my-app",
  "entry": "src/main.ts",
  "target": "sloppy1.0",
  "runtimeIdentifier": "win-x64",
  "packageSources": [
    "./packages",
    {
      "name": "local",
      "type": "folder",
      "path": "./packages"
    },
    {
      "name": "npmjs",
      "type": "npm",
      "url": "https://registry.npmjs.org"
    }
  ],
  "dependencies": {
    "Sloppy.Example": "[0.1.0]"
  },
  "trustedNativePackages": [
    "Sloppy.Example"
  ]
}
```

Supported restore fields:

| Field | Required | Meaning |
| --- | --- | --- |
| `target` | Optional | Restore target. Defaults to `sloppy1.0`. |
| `runtimeIdentifier` | Optional when host RID can be detected | Runtime identifier used for native asset selection. |
| `packageSources` | Yes for restore | Project-relative source strings or source objects. |
| `dependencies` | Optional | Direct package dependency map from package id to version range. |
| `trustedNativePackages` | Optional | Native package approvals recorded by `sloppy add --native-ok`. |

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
| `version` | Yes | Package version in `major.minor.patch` form with optional prerelease suffix. |
| `description` | No | Human-readable package description. |
| `authors` | No | Array of author names. |
| `license` | No | Package license expression or identifier. |
| `repository` | No | Repository URL. |
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
| `(1.0.0,2.0.0]` | Exclusive lower bound, inclusive upper bound. |
| `[1.0.0,)` | Inclusive lower bound with no upper bound. |
| `(,2.0.0)` | No lower bound, exclusive upper bound. |

Unsupported range forms fail with `SLOPPY_E_PACKAGE_RANGE_INVALID`.
Prerelease suffixes such as `1.0.0-alpha.1` are accepted. Stable releases sort
after prereleases for the same numeric version.

Restore prefers the locked version when a lockfile exists and the current
constraints still allow that version. Sloppy does not install multiple
versions of the same package in this foundation. Direct or transitive conflicts
fail with `SLOPPY_E_PACKAGE_CONFLICT`.

## Package Sources

Supported source declarations:

```json
{
  "packageSources": [
    "./packages",
    {
      "name": "local",
      "type": "folder",
      "path": "./packages"
    },
    {
      "name": "offline-feed",
      "type": "folder",
      "path": "./feed"
    },
    {
      "name": "official",
      "type": "sloppy",
      "url": "https://packages.sloppy.dev/v3/index.json"
    },
    {
      "name": "npmjs",
      "type": "npm",
      "url": "https://registry.npmjs.org"
    }
  ]
}
```

Folder sources scan flat `.slpkg` files and static-feed folder layouts under
`v3-flatcontainer`. Source paths must be project-relative and cannot contain
`..`, drive prefixes, or absolute roots; this keeps lockfiles reproducible and
avoids leaking machine-local paths.

HTTP Sloppy feeds and npm sources are recognized as source types, but network
restore is not implemented in this PR. They fail with stable diagnostics
instead of silently falling back to another package model.

Static folder feeds use:

```text
v3/index.json
v3-flatcontainer/<normalized-id>/index.json
v3-flatcontainer/<normalized-id>/<version>/<normalized-id>.<version>.slpkg
```

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
matches the cache marker and declared package assets still match the selected
artifact. A missing marker or tampered cache fails with
`SLOPPY_E_PACKAGE_CACHE_CORRUPT`. Restore does not copy package contents into
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
- package sources used
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
      "normalizedId": "sloppy.example",
      "version": "0.1.0",
      "source": "./packages",
      "sourceType": "folder",
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
      "normalizedId": "sloppy.example",
      "version": "0.1.0",
      "path": "<global-cache>/sloppy.example/0.1.0",
      "compile": [
        "lib/sloppy1.0/index.ts"
      ],
      "runtime": [],
      "nativeLibraries": {
        "example": {
          "path": "native/win-x64/example.dll",
          "package": "Sloppy.Example",
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

`project.assets.json` now carries enough native asset metadata for the CLI and
runtime integration seam: logical library name, package id, selected package
root, relative asset path, and hash. Full automatic `unsafeFfi.library(...)`
resolution from restored package assets remains follow-up work.

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
- stale manifest asset hashes
- symlinks, hardlinks, and special ZIP entries
- oversized package archives and oversized archive entries

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
| `SLOPPY_E_PACKAGE_CACHE_CORRUPT` | Cached package contents are missing or tampered. |
| `SLOPPY_E_PACKAGE_PATH_TRAVERSAL` | Package path is unsafe. |
| `SLOPPY_E_PACKAGE_DUPLICATE_PATH` | Two archive paths normalize to the same package path. |
| `SLOPPY_E_PACKAGE_SCRIPT_UNSUPPORTED` | Package script declaration is not allowed. |
| `SLOPPY_E_PACKAGE_NATIVE_ASSET_MISSING` | Native asset for the selected RID is missing. |
| `SLOPPY_E_PACKAGE_LOCK_OUT_OF_DATE` | `--locked` restore would change the lockfile. |
| `SLOPPY_E_PACKAGE_REMOTE_UNAVAILABLE` | A remote source is declared but cannot be used. |
| `SLOPPY_E_PACKAGE_SOURCE_UNSUPPORTED` | A declared source type is recognized but not supported by restore yet. |

## CI and Offline Flow

Typical CI restore should use:

```text
sloppy restore --locked
sloppy build --no-restore
```

The second command is the intended future shape for package-aware builds. Until
that flag exists, CI should run locked restore before build and treat
`sloppy.lock.json` plus `.sloppy/obj/project.assets.json` as the auditable
restore evidence.

Offline and enterprise flows can publish approved `.slpkg` artifacts into a
folder source or static folder feed, commit `sloppy.lock.json`, and run
`sloppy restore --locked` without contacting a network registry.

## Current Limitations

This foundation does not include:

- HTTP Sloppy feed restore
- full npm package restore
- `node_modules`
- install or postinstall scripts
- package signing
- vulnerability database checks
- workspaces or monorepo restore
- tools, templates, analyzers, or source generators
- native asset execution or native loading during restore
- automatic FFI binding from restored native assets
- broad `sloppy.json` redesign outside package restore
