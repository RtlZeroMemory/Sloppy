# Data Contract Tests

The data area checks deterministic Plan/package metadata and PR-tier SQLite
semantics that can run without Docker or live provider services.

Run:

```powershell
node tests/contracts/runner/contract-runner.mjs --area data --tier pr
```

The PR tier covers Plan/package artifacts with committed fixtures. Its SQLite
behavior checks are model coverage for the shared migration/provider contract;
they do not prove the native SQLite bridge or a TestHost provider binding yet.

PostgreSQL and SQL Server SQL generation is checked through provider
placeholder contracts only. Live PostgreSQL, SQL Server, and native-provider
SQLite behavior belongs in extended provider lanes.
