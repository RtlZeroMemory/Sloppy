# OpenAPI Contracts

The OpenAPI contract compares Plan route metadata with generated OpenAPI JSON.
It validates semantic agreement rather than JSON formatting.

Run it with:

```powershell
node tests/contracts/runner/contract-runner.mjs --area openapi --tier pr
```

Fixtures point at existing generated OpenAPI goldens and apply small mutations
for negative cases. Broken fixtures must fail through the named invariant they
declare in `contract-fixture.json`.
