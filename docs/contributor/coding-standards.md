# Coding standards

Per-language standards. CI runs the matching scanner under
`dev.ps1 lint`.

- [C standards](c-standards.md) — safety, ownership, API rules
- [C style](c-style.md) — formatting and naming
- [Rust standards](rust-standards.md) — compiler/tooling rules
- [JS/TS standards](js-ts-standards.md) — stdlib, examples, tools

## Boundaries that cut across languages

- OS APIs stay under `src/platform/`.
- OS headers don't appear in core modules.
- V8 types stay under `src/engine/v8/`.
- JavaScript never receives a raw native pointer.
- Generated and build artifacts aren't committed.

## Running scanners directly

```powershell
.\tools\windows\check-c-standards.ps1
.\tools\windows\check-rust-standards.ps1
.\tools\windows\check-js-ts-standards.ps1
```

`dev.ps1 lint` runs them as part of the standard contributor flow.
