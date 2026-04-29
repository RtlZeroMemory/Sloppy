# JS/TS Stdlib Skill

Use this for `stdlib/sloppy/`, examples, public JS/TS API shape, and JS/TS compiler
fixtures.

1. Read `docs/js-ts-standards.md`.
2. Preserve the public API contract from `docs/developer-ergonomics.md` and related docs.
3. Keep examples honest about what runs today versus what is future compiler/runtime work.
4. Do not add Node, npm, package-manager, bundler, or transpiler assumptions.
5. Prefer stable frozen descriptors, explicit builders, and small factory functions.
6. Avoid dynamic patterns that make compiler extraction unclear.
7. Add behavior tests through the V8 harness where possible.
8. Use static checks only when the execution harness cannot load the module shape yet, and
   document that reason.
9. Run `.\tools\windows\check-js-ts-standards.ps1`.
10. Run `.\tools\windows\dev.ps1 lint` before review when practical.
