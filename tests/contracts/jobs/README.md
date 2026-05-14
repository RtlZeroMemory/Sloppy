# Jobs Model Contracts

The jobs contract area currently protects durable scheduler state-machine
behavior with a deterministic model/spec harness. It does not claim to validate
the shipped `sloppy/jobs` runtime implementation until that implementation is
present on the branch under test.

PR-tier checks do not require live PostgreSQL, SQL Server, Redis, Docker, or a
long-running worker daemon.

Run with:

```powershell
node tests/contracts/runner/contract-runner.mjs --area jobs --tier pr
node tests/contracts/runner/contract-runner.mjs --area all --tier pr
```

The contract names match the intended public jobs API and scheduler docs. When
the runtime module or native `sloppy jobs` CLI is not present in a checkout,
those implementation-backed lanes are reported as unavailable rather than as
passing coverage.

The follow-up for this area is an implementation-backed SQLite PR-tier contract
that imports the real `sloppy/jobs` API and exercises the native jobs CLI once
those files are available on `main`.
