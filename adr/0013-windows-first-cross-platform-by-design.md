# 0013: Windows-First, Cross-Platform By Design

## Status

Accepted.

## Context

The primary developer environment is currently Windows. Sloppy should work well on Windows
from the start. But Sloppy is a runtime and should not become Windows-only.

Platform API leakage into core modules would make Linux and macOS support painful later.

## Decision

Windows x64 remains the first-class developer workflow. The core runtime must remain
portable C. Platform-specific code is isolated under `src/platform/*`.

Core runtime code must not include OS-specific headers or call OS-specific APIs directly.
Platform-specific tooling is organized under platform-specific tool directories. Root
tooling scripts may forward to platform-specific scripts for convenience.

Linux and macOS support is a design requirement, even if they are not the first CI targets.

## Consequences

This adds slightly more structure before implementation. It gives Sloppy cleaner future
Linux/macOS support, more explicit OS behavior boundaries, and fewer accidental WinAPI or
POSIX leaks.

Windows development remains smooth through first-class Windows scripts and presets.

## Alternatives Considered

- Windows-only until later: rejected because core abstractions would harden around Windows.
- POSIX-first abstractions: rejected because the current developer workflow is Windows.
- Scattered `#ifdef _WIN32` throughout core modules: rejected because it spreads platform
  behavior into runtime logic.
- Let dependencies hide all platform behavior: rejected because Sloppy still owns runtime
  lifecycle, diagnostics, and resource semantics.

## Follow-up Tasks

- Keep platform-boundary scanner in CI.
- Add platform abstraction skeleton before OS-backed allocators/files.
- Add Linux/macOS presets only when they can represent real build paths.
- Keep platform-specific tooling under `tools/<platform>/`.
