# Capability Conformance

Status: native policy covered; JavaScript bridge enforcement deferred.

Source fixture: `tests/unit/core/test_capability.c`.

Default evidence:

```powershell
ctest -R core.capability.registry --output-on-failure
```

Expected behavior:

- missing database capability fails before the fake provider operation is called;
- insufficient read/write access fails before provider work;
- provider mismatch fails before provider work;
- filesystem/network capability entries remain metadata/check-only skeletons;
- denied diagnostics are redacted.

Gated/deferred requirement: no JavaScript provider bridge calls the native capability hook
yet. SQLite bridge capability enforcement remains tracked in `docs/tech-debt-tracker.md`
and must not be reported as executable JS conformance until the hook is wired.
