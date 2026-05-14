# Data Contract Tests

The data area checks deterministic Plan/package metadata and PR-tier SQLite
semantics that can run without Docker or live provider services.

Run:

```powershell
node tests/contracts/runner/contract-runner.mjs --area data --tier pr
```

The PR tier covers SQLite with committed fixtures. PostgreSQL and SQL Server
SQL generation is checked through provider placeholder contracts; live database
behavior belongs in extended provider lanes.
