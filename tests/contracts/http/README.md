# HTTP Dispatch Contracts

The HTTP contract area compares generic handler semantics with optimized route
dispatch and native no-JS response metadata. Fixtures are intentionally small:
each one contains a Plan, an optional `routes.slrt`, and deterministic response
traces for requests that exercise routing, request binding, and response
normalization.

The positive fixture set includes `compiler-generated-grouped-route`, which
copies the current compiler fixture output from
`compiler/tests/fixtures/grouped-route/expected/`. The validator byte-compares
that fixture's `app.plan.json` and `routes.slrt` against the compiler fixture
goldens before checking dispatch semantics, so at least one PR-tier path is
grounded in real compiler-emitted route metadata and dispatch artifact bytes.

Run the PR-tier lane with:

```powershell
node tests/contracts/runner/contract-runner.mjs --area http --tier pr
```

Broken fixtures are part of the lane. They must fail for their expected
invariant so the report proves the contract detects semantic drift.

Runtime counters are still deferred. This lane checks native semantic
equivalence now; proving native no-JS routes make zero V8 calls still needs
runtime counters or profiling support.
