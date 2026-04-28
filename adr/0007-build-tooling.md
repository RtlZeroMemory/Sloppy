# 0007: Build Tooling

## Status

Accepted.

## Context

The first-class environment is Windows x64. Contributors need a predictable local path and
CI needs realistic commands that can later grow into release packaging.

## Decision

Sloppy uses CMake and Ninja for the runtime, `clang-cl` and `lld-link` for the Windows C
toolchain, vcpkg manifest mode for normal C dependencies, and PowerShell-first tooling.

V8 is handled as a special SDK dependency. Release packaging starts as a GitHub Release ZIP.

## Consequences

The build remains close to the target production environment. Normal dependencies can use
vcpkg later without forcing V8 into that model. PowerShell scripts are part of the supported
developer experience.

## Alternatives Considered

- Visual Studio solution as primary build: rejected because CMake/Ninja is easier to
  automate consistently.
- vcpkg-managed V8: deferred because V8 needs special artifact and version handling.
- Source-build V8 for every contributor: rejected as too expensive for the normal path.

## Follow-up Tasks

- Keep root scripts as compatibility forwarders to platform-specific tools.
- Add Unix tooling only when Linux/macOS build paths are real.
- Define release ZIP staging and install layout before packaging.
- Keep generated artifacts out of source archives.
