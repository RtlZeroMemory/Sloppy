# Coding Standards

Use these standards as the contract for code review and CI:

- [C standards](c-standards.md): safety and API rules.
- [C style](c-style.md): formatting and naming conventions.
- [JavaScript and TypeScript standards](js-ts-standards.md): stdlib/examples/tools behavior contract.
- [Rust standards](rust-standards.md): compiler/tooling rules.

Core boundaries:

- OS APIs stay under `src/platform/*`.
- OS headers must not appear in core modules.
- V8 types stay under `src/engine/v8/*`.
- JavaScript must not receive raw native pointers.
- Generated or build artifacts must not be committed.

Run standards checks directly when needed:

```powershell
.\tools\windows\check-c-standards.ps1
.\tools\windows\check-js-ts-standards.ps1
.\tools\windows\check-rust-standards.ps1
```

For normal contributor flow, `.\tools\windows\dev.ps1 lint` runs these checks
alongside other repository gates.
