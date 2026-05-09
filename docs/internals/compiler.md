# Compiler Internals

## Where It Lives

- `compiler/src/sloppyc.rs`
- `compiler/src/resolver.rs`
- `compiler/src/parser.rs`
- `compiler/tests/fixtures/**`

## Lifecycle

The compiler parses supported JavaScript or TypeScript, resolves supported
imports, extracts Sloppy app metadata, emits `app.plan.json`, emits generated
`app.js`, and writes `app.js.map`.

## Failure Behavior

Unsupported imports, dynamic imports, dynamic route strings, and unsupported
handler shapes fail as compiler diagnostics. The compiler does not silently
fall back to runtime discovery.

Compiler output uses deterministic artifacts for the same supported input. The compiler
does not implement Node package resolution and does not treat `node_modules` as
application input.
