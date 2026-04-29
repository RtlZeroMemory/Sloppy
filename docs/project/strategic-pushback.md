# Strategic Pushback

Status: reviewer pushback checklist for the Slop Engine foundation roadmap.

This file is intentionally blunt. It records what Slop should not do next, even if those
paths look attractive in planning.

## Pushback

- Do not define "full-scale HTTP server" as a production-grade Kestrel/Nginx replacement
  before alpha. The foundation target is a framework HTTP runtime for realistic localhost
  API apps with honest production boundaries.
- Do not implement async by pretending Promise support exists. Either returned Promises
  settle through real V8 microtask/request-scope semantics, or async handlers remain
  explicitly unsupported.
- Do not treat cancellation, deadlines, backpressure, bounded queues, or cleanup as later
  polish. They are the primitive infrastructure that makes async/HTTP/resource work
  scalable enough to be real.
- Do not overbuild `sloppyc` into an arbitrary JS/TS bundler. Compile supported Sloppy apps,
  reject unsupported shapes, and keep Node/npm compatibility out unless scoped.
- Do not turn SQLite into an ORM or migration framework yet. Finish open/query/exec,
  transactions/prepared-statement policy, capability enforcement, lifecycle cleanup, and
  examples first.
- Do not make the strongly typed Plan so rigid that early user ergonomics cannot be
  corrected. The Plan should protect runtime safety while leaving deliberate extension
  points.
- Do not start PostgreSQL or SQL Server JS bridge work before the SQLite JS/native path is
  excellent. Native provider foundations exist; provider expansion is not the current
  engine blocker.
- Do not publish public docs until examples are executable through the real compiler/runtime
  path or explicitly labeled static/deferred.
- Do not benchmark against competitors until methodology is real: release builds, real HTTP
  paths, V8 handlers, JSON serialization, DB work, hardware context, repeatability, and
  fair comparison criteria.
- Do not create massive issues that cannot be reviewed. ENGINE epics should be parents;
  implementation tasks should be bounded and evidence-oriented.
- Do not report default CI as V8 success. Default non-V8 gates, optional V8 gates, package
  smoke, live provider checks, and benchmark smoke must stay separate.
- Do not claim security enforcement until the capability hook is wired into real bridge
  calls. Metadata-only audit is not runtime denial.
- Do not let public examples outrun the compiler. Static API-shape examples are useful, but
  public alpha examples must compile and run through `sloppyc build` plus
  `sloppy run --artifacts`.
