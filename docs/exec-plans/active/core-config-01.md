# Execution Plan: CORE-CONFIG-01

## Status

Implementation complete locally; PR handoff and remote CI remain pending.

## Goal

Implement CORE-CONFIG-01 as one coherent feature PR targeting `main`: layered application
configuration, typed reads and binding, secret redaction, provider-owned contracts,
Plan/package metadata, doctor visibility, examples, and tests.

## Source Docs Read

- `AGENTS.md`
- `docs/project/README.md`
- `docs/project/framework-api-shape.md`
- `docs/app-plan.md`
- `docs/modules/app-host/README.md`
- `docs/modules/plan/README.md`
- `docs/developer-ergonomics.md`
- `docs/security-permissions.md`
- `docs/diagnostics.md`
- `docs/testing-strategy.md`
- `docs/quality-gates.md`
- `docs/documentation-policy.md`
- `docs/c-standards.md`
- `docs/c-style.md`
- `docs/js-ts-standards.md`
- `docs/rust-standards.md`

## Scope

1. Preserve deterministic source precedence:
   - built-in runtime defaults
   - `appsettings.json`
   - `appsettings.{Environment}.json`
   - `appsettings.local.json`
   - `appsettings.{Environment}.local.json`
   - local user secrets when explicitly present
   - environment variables with `__` hierarchy mapping
   - compiler command-line config overrides
   - explicit bootstrap/test overrides
2. Extend the bootstrap config surface with typed getters, descriptor-based `bind`, defaults,
   required validation, enum/range validation, arrays/objects, durations, sizes, and secret values.
3. Extend compiler metadata for static config reads, static bind descriptors, provider-owned
   config requirements, missing/present status, redaction, and package-manifest requirements.
4. Keep dynamic config metadata honest: static keys are emitted; dynamic keys are flagged as
   unsupported in compiler strict/package contexts rather than guessed.
5. Extend doctor text/JSON output with config checks that never print raw secret values.
6. Add source-input, compiler, bootstrap, CLI golden, and documentation coverage for positive and
   negative paths.

## Explicit Deferred Behavior

- Reload-on-change file watchers are not implemented in this PR. Runtime loading is deterministic
  at compile/source-input handoff and app startup; failed reload behavior stays deferred.
- Production secret vault integrations are not implemented. Local/user secret files are a local
  development source only and must stay ignored.
- Provider runtime bridges beyond the existing supported provider runtime remain out of scope.
  Provider-owned config contracts may be represented in metadata without implying live provider
  connectivity.
- No public alpha docs, package-manager behavior, Node compatibility work, benchmark claims, or
  generated build artifacts are part of this PR.

## Verification Plan

- Run focused bootstrap API tests while editing the JavaScript app-host config surface.
- Run focused compiler tests for config source precedence, env mapping, bind metadata, diagnostics,
  Plan metadata, and redaction.
- Run CLI golden checks for doctor text/JSON config output.
- Run practical local build/test/format/lint gates before opening or marking the PR ready.
- On this Codex Windows machine, resolve the local V8 SDK and run the separate
  `windows-relwithdebinfo` V8-enabled configure/build/test lane before PR handoff.

## Verification Log

- `node tests\bootstrap\test_app_host_foundation.mjs`: passed.
- `cargo test --manifest-path compiler\Cargo.toml --lib`: passed.
- `ctest --test-dir build\windows-dev --output-on-failure -R "(examples\.config\.api_shape|sloppy\.cli\.doctor_config_text|sloppy\.cli\.doctor_config_json|bootstrap\.stdlib\.app_host_foundation)"`: passed.
- `.\tools\windows\dev.ps1 build`: passed.
- `.\tools\windows\dev.ps1 test`: passed; default `windows-dev` CTest passed 188/188,
  with live PostgreSQL and SQL Server provider tests skipped by design, and compiler cargo
  tests passed.
- `.\tools\windows\resolve-v8-sdk.ps1`: passed; resolved
  `V:\Slop\.sdeps\v8\windows-x64`.
- `.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8`: passed.
- `.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo`: passed.
- `.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo`: passed; V8-enabled
  CTest passed 211/211, including V8-labeled tests, with live PostgreSQL and SQL Server
  provider tests skipped by design, and compiler cargo tests passed.
- `.\tools\windows\dev.ps1 format-check`: passed.
- `.\tools\windows\dev.ps1 lint`: passed, with existing non-fatal C complexity warnings.
- `git diff --check`: passed.
