# HTTP Dispatch Contracts

The HTTP contract area compares generic handler semantics with optimized route
dispatch and native no-JS response metadata. Fixtures are intentionally small:
each one contains a Plan, an optional `routes.slrt`, and deterministic response
traces for requests that exercise routing, request binding, and response
normalization.

Run the PR-tier lane with:

```powershell
node tests/contracts/runner/contract-runner.mjs --area http --tier pr
```

Broken fixtures are part of the lane. They must fail for their expected
invariant so the report proves the contract detects semantic drift.
