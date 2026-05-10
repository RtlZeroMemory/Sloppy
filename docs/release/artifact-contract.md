# Release Artifact Contract

This is the current source contract for the RELEASE-DIST-01 alpha packaging
dry-run.

GitHub Release archives are the canonical artifacts. npm packages are
convenience installers for the Sloppy runtime. Sloppy apps still run through Sloppy-managed artifacts;
compatible package imports are bundled into those artifacts instead of loaded
from `node_modules` at run time. Full Node
compatibility, native addons, and package-manager behavior remain separate
design areas.

## Archives

| Platform | Archive | Package root | Current status |
| --- | --- | --- | --- |
| Windows x64 | `sloppy-windows-x64.zip` | `sloppy-windows-x64` | Experimental dry-run package lane. |
| Linux x64 | `sloppy-linux-x64.tar.gz` | `sloppy-linux-x64` | Experimental dry-run package lane; V8 runtime-user evidence requires the Sloppy-owned Linux x64 SDK and `test-package --require-v8-runtime`. |
| macOS arm64 | `sloppy-macos-arm64.tar.gz` | `sloppy-macos-arm64` | Experimental hosted dry-run package lane. |
| macOS x64 | `sloppy-macos-x64.tar.gz` | `sloppy-macos-x64` | Experimental hosted dry-run package lane when the Intel runner is requested. |

The package root contains:

```text
sloppy-<platform>-<arch>/
  bin/
    sloppy(.exe)
    sloppyc(.exe)
    required runtime dynamic libraries
  stdlib/
    sloppy/
  templates/
  examples/
  docs/
    KNOWN_LIMITATIONS.md
    LICENSES.md
    NOTICE.md
  manifest.json
```

Packages must not contain `.sdeps`, source build trees, `compiler/target`, `target`,
`vcpkg_installed`, maintainer-local absolute paths, V8 SDK headers, or V8 SDK import
libraries. V8 runtime support may appear only when deliberately included and when the
manifest records the V8 runtime status honestly. Static Linux V8 packages may link V8 into
`bin/sloppy` without an `engines/v8` directory; shared-library packages may include only
the runtime shared libraries required by the packaged executable.

## Manifest

`manifest.json` uses `manifestSchema: "sloppy.release-artifact.v1"` and records archive
identity, package root, version, commit, platform, architecture, configuration,
distribution role, runtime dependency status, V8 status, provider dependency status,
known limitations, and checksum references.

The supported status vocabulary is `supported`, `experimental`, `source-build only`,
`planned`, `blocked`, `unavailable`, and `untested`. Evidence reports use `PASS`, `FAIL`,
`SKIPPED`, `UNAVAILABLE`, `DEFERRED`, and `NOT RUN`.

## npm

The npm root package is `@rtlzeromemory/sloppy`. It exposes only the `sloppy` launcher and
selects an installed platform package:

- `@rtlzeromemory/sloppy-win32-x64`
- `@rtlzeromemory/sloppy-linux-x64`
- `@rtlzeromemory/sloppy-darwin-arm64`
- `@rtlzeromemory/sloppy-darwin-x64`

npm package dry-runs must use `--tag alpha`, never `latest`. Platform package
contents are generated from already-built archive contents; npm install must not
compile native code, run `node-gyp`, build V8, or download V8 in `postinstall`.

Publishing is manual and alpha-gated through the npm publish workflow. It uses
Node 22.14.0 or newer, npm 11.5.1 or newer, `id-token: write`, and
`npm publish --provenance` so npm Trusted Publishing can issue a short-lived
OIDC publish credential and attach package provenance. If Trusted Publishing is
not configured on npmjs.com for these packages, the publish step must fail; do
not add committed `.npmrc` auth or long-lived publish tokens.

User-facing wording:

> npm is a convenient way to install the Sloppy runtime. Sloppy apps still use
> Sloppy's artifact model; compatible installed packages are bundled into that
> model rather than discovered from `node_modules` at run time.
