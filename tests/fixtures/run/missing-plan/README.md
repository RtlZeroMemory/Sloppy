This fixture intentionally omits `app.plan.json`.

It exists so `sloppy run --artifacts tests/fixtures/run/missing-plan --once GET /` validates
the command fails clearly for an artifact directory that exists but is not a Sloppy
artifact directory.
