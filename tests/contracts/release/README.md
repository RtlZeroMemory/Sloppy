# Release/Npm Contract

The release contract validates npm package metadata and staged package contents
before an alpha publish. It does not publish to npm.

Run it directly:

```powershell
node tests/contracts/runner/contract-runner.mjs --area release --tier pr
node tests/contracts/runner/contract-runner.mjs --area release --tier pr --format markdown
```

The PR-tier validator checks:

- npm package names stay under `@slopware`;
- package versions are non-zero `alpha` semver versions;
- root bin, JS exports, TypeScript declarations, README, and license paths are
  safe package-relative paths that point to files that exist;
- platform optional dependencies match expected Sloppy platform packages;
- platform package `os` and `cpu` metadata match the package name;
- staged platform package binaries exist when a staged package root is present;
- root and platform package versions agree;
- package contents do not include `.env`, `.npmrc`, token-looking files, local
  absolute paths, build logs, nested `node_modules`, source checkout junk, V8
  SDK source trees, or huge accidental files;
- npm install is reported as unavailable unless local staged packages or
  tarballs exist for that validation;
- TypeScript import smoke compiles public `sloppy` imports with `tsc --noEmit`
  for staged package validation and stays unavailable for source skeletons.

The fixture suite includes one valid alpha package set plus broken package
metadata fixtures for path traversal in bins, exports, types, and platform
binaries, missing bins, missing type paths, public exports without types,
package names outside `@slopware`, `0.0.0` versions, secret files, V8 SDK
source-like directories, missing platform binaries, and optional dependency
version mismatches.
