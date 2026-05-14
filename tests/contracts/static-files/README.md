# Static Files Contract Tests

The static-files contract lane uses the first-party TestHost to exercise
configured static files, SPA fallback, and API routes without starting a live
server. The fixtures under `fixtures/` are broken response transcripts that
prove each negative invariant would fail if the implementation served unsafe or
HTTP-invalid responses.

Run the PR-tier lane with:

```powershell
node tests/contracts/runner/contract-runner.mjs --area static-files --tier pr
```

The lane covers traversal rejection, root-boundary enforcement, API precedence,
SPA fallback boundaries, HEAD/GET agreement, validator/cache behavior,
content-type detection, Content-Length, precompressed assets, ranges, and body
byte preservation.
