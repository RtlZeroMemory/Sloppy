# Cache Contracts

The cache contract area validates first-party cache behavior that can affect
correctness or leak user data:

- memory cache set/get, delete, TTL, tag invalidation, schema, size, and
  redacted diagnostics semantics;
- output-cache safety for GET-only caching, authenticated responses,
  `Set-Cookie`, vary inputs, hit equivalence, tag purge, metrics, and the
  current policy that ProblemDetails/error responses bypass output-cache;
- deterministic Redis surface checks for configured Redis usage, redacted
  diagnostics, no silent memory fallback, and lock-owner enforcement.

Run the PR-tier lane with:

```powershell
node tests/contracts/runner/contract-runner.mjs --area cache --tier pr
```

PR-tier checks use in-process memory caches, TestHost, and deterministic fake
Redis bridges. Live Redis belongs in extended or provider lanes.
