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
- Document the sanitizer/fuzz gate plan for the current parser/resource surfaces.
- Add only small package-smoke tooling that validates current archive layout and policy.

## Non-Goals

- No runtime, compiler, provider, plan schema, or diagnostics behavior changes.
- No release upload, signing, notarization, installers, or package-manager integration.
- No mandatory V8 downloads/builds in default CI.
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

## Decisions

- Default package smoke remains a layout and CLI-startup smoke. It does not prove V8
  execution, live providers, installers, package managers, or public release readiness.
- V8 package smoke requires both a V8-enabled build and explicit runtime-file validation.
  `containsV8Runtime: true` is not enough by itself to claim V8 execution.
- Linux/macOS package smoke now has a local script but remains outside required CI until a
  scoped job proves it on hosted runners.
- Sanitizer/fuzz work starts as an opt-in plan around existing CMake sanitizer options and
  future parser/resource fuzz targets. It is not required by default CI yet.
