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
- root bin, JS exports, TypeScript declarations, README, and license paths
  point to files that exist;
- platform optional dependencies match expected Sloppy platform packages;
- platform package `os` and `cpu` metadata match the package name;
- staged platform package binaries exist when a staged package root is present;
- root and platform package versions agree;
- package contents do not include `.env`, `.npmrc`, token-looking files, local
  absolute paths, build logs, nested `node_modules`, source checkout junk, V8
  SDK source trees, or huge accidental files;
- npm install and TypeScript smoke lanes are reported as unavailable unless
  local staged packages or tarballs exist for that validation.

The fixture suite includes one valid alpha package set plus broken package
metadata fixtures for missing bins, missing type paths, public exports without
types, package names outside `@slopware`, `0.0.0` versions, secret files, V8 SDK
source-like directories, missing platform binaries, and optional dependency
version mismatches.
