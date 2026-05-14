# Compiler Mega App

This checked-in app is a compiler-contract fixture, not a product template. It
intentionally combines supported source shapes that have historically drifted:
route groups, provider-effect routes, helper chains, local shadowing,
configuration reads, static assets, package imports, and common HTTP methods.

`compiler/tests/compiler_contract_validation.rs` compiles this app twice,
validates semantic compiler-contract invariants, compares deterministic
artifacts, and checks that package dependency metadata remains present.
