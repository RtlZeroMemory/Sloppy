# npm Package Compatibility Gauntlet

This directory holds fixtures that exercise the Sloppy compiler's package
resolver against realistic npm package shapes. The matrix is committed and
tested in CI; it is **not** an installation of npm packages from a registry.

## Scope

- Pure-JavaScript package layouts that Sloppy can plausibly support today.
- Shapes Sloppy must reject with clear diagnostics (native addons, unsupported
  conditions, computed dynamic require without `moduleInclude`).
- Fixtures use minimal, hand-authored sources. No real registry dependency.

## Out of scope

- Full Node runtime compatibility.
- Native (`.node`) addon execution or N-API.
- Registry install or semver resolution.
- Internet-dependent tests in the normal lane.

## Layout

```
tests/fixtures/npm-compat/
  matrix.json                 -- machine-readable matrix
  <fixture>/                  -- one directory per matrix entry
    package.json              -- package.json under test
    index.{js,mjs,cjs,ts}     -- entry or feature module
    node_modules/<pkg>/...    -- inline dependency, when needed
    sloppy.json               -- optional, when the fixture is also buildable
```

`matrix.json` is the source of truth for expected status. Tests load it
and assert resolver behavior matches each entry.

## Status values

| Status        | Meaning |
| ---           | --- |
| `supported`   | Sloppy resolves and builds the fixture cleanly. |
| `partial`     | Sloppy resolves the supported subset; specific subpaths or conditions fail with explicit diagnostics. |
| `unsupported` | Sloppy must emit a documented diagnostic and fail. |

The local smoke script `tools/bench-or-test/npm-compat-smoke.mjs` can install
a curated list of real pure-JS packages for ad-hoc experimentation. The smoke
script is not part of normal CI and does not gate release.
