# Security And Redaction Model

Sloppy's current security model is boundary-first and diagnostics-first. It is
not an OS sandbox and should be treated as pre-hardening.

Current boundaries include:

- plan parsing rejects secret-bearing fields in artifact metadata
  (`src/core/plan_parse.c`);
- app-host validates provider/capability relationships before startup
  (`src/core/app_host.c`);
- provider modules redact connection-string secrets in diagnostics
  (`src/data/postgres.c`, `src/data/sqlserver.c`);
- V8 bridge modules avoid raw native pointer exposure and keep ownership private
  (`src/engine/v8/*`);
- capability checks are policy enforcement points, not containerization or
  kernel-level isolation.

The goal is predictable failure and safer evidence:

- malformed/unsafe metadata fails closed;
- diagnostics stay useful without leaking secrets;
- bridge boundaries stay auditable in source.
