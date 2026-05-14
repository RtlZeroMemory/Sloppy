# Release Infrastructure Plan

This plan tracks the first Sloppy alpha release infrastructure. It is scoped to
making release artifacts reproducible, versioned, dry-run publishable, and
honest about the manual and future CI publishing paths.

## Alpha Target

- First alpha version: `0.1.0-alpha.0`.
- Current alpha release target: `0.1.0-alpha.2`.
- npm dist-tag: `alpha`.
- npm scope: `@slopware`.
- Root package: `@slopware/sloppy`.
- Platform packages:
  - `@slopware/sloppy-win32-x64`
  - `@slopware/sloppy-linux-x64`
  - `@slopware/sloppy-darwin-arm64`
  - `@slopware/sloppy-darwin-x64`

The package metadata, optional dependency pins, archive manifests, release
contract, and `sloppy --version` output must all use the current alpha release
target.

## Implemented Release Infrastructure

- GitHub Release archives remain the canonical runtime artifacts.
- npm packages are wrappers around already-built archive contents and do not
  run native install, download, `cmake`, `cargo`, `vcpkg`, `node-gyp`, V8 fetch,
  or V8 build steps during npm install.
- The root npm package includes `types/index.d.ts` plus subpath declarations for
  `sloppy/data`, `sloppy/fs`, `sloppy/os`, and `sloppy/providers/sqlite`.
- App templates that import Sloppy APIs include a local TypeScript config and a
  dev dependency on the current `@slopware/sloppy` alpha so editors can load the
  ambient declarations for bare `sloppy` imports.
- Release dry-runs produce archive checksums and npm tarballs for the root and
  matching platform package.
- The npm publish workflow is manual, uses npm Trusted Publishing with OIDC and
  provenance, and publishes with the `alpha` tag only.

## V8 SDK Artifact Flow

Release packaging consumes existing V8 SDK artifacts. It does not rebuild V8 in
the release-artifacts workflow.

- Windows restores the pinned V8 SDK asset through
  `tools/windows/fetch-v8.ps1 -Platform windows-x64`.
- Linux restores an existing `sloppy-v8-sdk-cache` release asset matching
  `sloppy-v8-sdk-linux-x64-*.tar.gz` and validates the sidecar checksum.
- macOS arm64 restores an existing `sloppy-v8-sdk-cache` release asset matching
  `sloppy-v8-sdk-darwin-arm64-*.tar.gz` and validates the sidecar checksum.
- macOS x64 restores an existing `sloppy-v8-sdk-cache` release asset matching
  `sloppy-v8-sdk-darwin-x64-*.tar.gz` and validates the sidecar checksum.

V8 SDK creation remains owned by the separate producer workflow or maintainer
tooling. If a required SDK asset is missing or has the wrong checksum, the
release artifact lane fails instead of rebuilding during release packaging.

## First Alpha Manual Publish Path

The `@slopware` npm organization already exists. Before publishing the first
alpha, a maintainer must:

1. Verify the five package names are still unpublished or intentionally owned by
   Slop maintainers.
2. Authenticate locally with `npm login --auth-type=web`.
3. Run `npm publish --dry-run --access public --tag alpha` against every staged
   tarball.
4. Publish platform package tarballs first.
5. Publish the root `@slopware/sloppy` tarball last.
6. Verify install smoke tests from a directory outside the repository.

Do not publish the `latest` tag for the first alpha.

## Future CI Trusted Publishing Path

After npm Trusted Publishing is configured for the package names, the
`npm-publish` workflow can publish release-artifact tarballs without npm tokens.
The workflow must keep:

- `id-token: write` for OIDC.
- npm CLI `11.5.1` or newer.
- Node.js `22.14.0` or newer.
- `npm publish --provenance`.
- no `.npmrc`, `NODE_AUTH_TOKEN`, automation token, or setup-node token auth.

If Trusted Publishing is not configured on npmjs.com, the workflow should fail
closed and the manual browser-auth path remains the release path.

## Evidence Gates

- `tools/windows/check-release-artifacts.ps1 -SelfTest`
- `tools/windows/check-release-artifacts.ps1`
- `tools/unix/scripts/validate-npm-package-policy.mjs`
- `npm pack --dry-run --json` for the root package
- TypeScript smoke compile for bare `sloppy` and `sloppy/*` imports with
  `types: ["@slopware/sloppy"]`
- npm registry name checks for all five `@slopware` package names
- `git diff --check`

## Non-Goals

- Publishing any package from this PR.
- Publishing the `latest` npm tag.
- Making the repository public.
- Claiming production readiness.
- Signing, notarization, SBOM completion, or final legal review.
- Rebuilding V8 inside release packaging.
