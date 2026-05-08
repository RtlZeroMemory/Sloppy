# MAIN1-12 Packaging and Cross-Platform Hardening

Status: active for PR implementation.

## Goal

Make package and CI evidence explicit enough that default, package, V8, sanitizer, fuzz,
and live-provider claims cannot be confused.

## Scope

- Document Windows package smoke, Linux/macOS package smoke status, outside-checkout
  validation, stdlib asset checks, CLI help/version checks, and current V8 runtime status.
- Document build-time V8 SDK versus runtime V8 artifacts and the V8-enabled package smoke
  contract.
- Add the Linux x64 V8 developer/package lane needed for the prebuilt JavaScript app
  development story.
- Document the sanitizer/fuzz gate plan for the current parser/resource surfaces.
- Add only small package-smoke tooling that validates current archive layout and policy.

## Non-Goals

- No runtime, compiler, provider, plan schema, or diagnostics behavior changes.
- No release upload, signing, notarization, installers, or package-manager integration.
- No mandatory V8 downloads/builds in default CI.
- No distro Node/V8 development package adapter; Linux x64 uses the same Sloppy-owned SDK
  model as Windows.
- No full sanitizer/fuzz infrastructure until a later scoped task.

## Plan

1. Read source docs, package scripts, CI, CMake presets, and GitHub issues #191/#228/#229/#230.
2. Tighten package smoke validation around stdlib assets, V8 SDK exclusion, and optional
   V8 runtime file presence.
3. Add a Unix package-layout smoke companion for the existing TAR script.
4. Update build/distribution, dependency, quality gate, testing, quality score, tech debt,
   README, and V8 module docs with implemented-vs-deferred evidence boundaries.
5. Run the requested Windows/default gates and package smoke where available; report V8,
   Linux/macOS, sanitizer, and fuzz checks separately when not run.
6. Add a Linux x64 Sloppy-owned V8 SDK builder/resolver, V8-enabled Unix package mode,
   static/shared runtime packaging policy, and extracted-package JS source-input smoke.
7. Prove the Linux V8 package in Docker from a clean archive and update PR evidence.

## Decisions

- Default package smoke remains a layout and CLI-startup smoke. It does not prove V8
  execution, live providers, installers, package managers, or public release readiness.
- V8 package smoke requires a V8-enabled build from a compatible Sloppy-owned SDK and
  positive extracted-package JS execution. `containsV8Runtime: true` is not enough by
  itself to claim V8 execution.
- Linux/macOS package smoke now has a local script but remains outside required CI until a
  scoped job proves it on hosted runners.
- Linux x64 V8 package smoke should be positive runtime execution evidence, not just
  runtime-file presence. Static SDK packages may link V8 into `bin/sloppy`; the package
  smoke must run from an extracted archive and exercise the JavaScript source-input user
  story.
- Sanitizer/fuzz work starts as an opt-in plan around existing CMake sanitizer options and
  future parser/resource fuzz targets. It is not required by default CI yet.
