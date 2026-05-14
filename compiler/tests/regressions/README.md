# Compiler Regression Seeds

These seeds replay historical compiler bug shapes through the semantic
compiler-contract validator. Each directory contains a `case.json` file and an
input app. The Rust `historical_regression_seed_registry_replays` test compiles
every case and checks the expected semantic model, so these fixtures are not
passive receipts.

Add a seed here when a compiler bug reveals a contract rule that goldens alone
could snapshot incorrectly. Keep the fixture small, name the invariant in
`case.json`, and prefer semantic expectations such as provider-effect route
counts, native no-JS route counts, package entries, or diagnostic codes.
