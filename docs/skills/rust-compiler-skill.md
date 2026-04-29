# Rust Compiler Skill

Use this for `compiler/`, future `sloppyc`, Rust fixtures, and Rust-owned tooling.

1. Read `docs/rust-standards.md`.
2. Keep compiler output deterministic and path-normalized.
3. Preserve diagnostics context: source file, span, related locations, and clear messages
   where possible.
4. Do not use `unwrap()`, `expect()`, `panic!`, `todo!`, `unimplemented!`, or `dbg!` in
   production code without an explicit documented allow reason.
5. Do not silently drop unsupported syntax during extraction.
6. Add unit tests for pure extractor/diagnostic logic.
7. Add golden tests for emitted artifacts when emission exists.
8. Avoid new dependencies unless the scoped compiler phase requires them and the PR body
   documents the reason.
9. Run `cargo fmt --manifest-path compiler/Cargo.toml -- --check`.
10. Run `cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings`.
11. Run `cargo test --manifest-path compiler/Cargo.toml`.
12. Run `.\tools\windows\check-rust-standards.ps1`.
