# Build Tooling Skill

Use this for CMake, scripts, CI, static checks, and gates.

- Keep the Windows-first workflow working.
- Keep cross-platform design visible.
- Treat root wrappers as convenience and platform scripts as canonical.
- Avoid hardcoded local paths.
- Do not silently skip CI gates or mark a required job green while skipping its purpose.
- Keep optional V8, package, live-provider, advanced static analysis, fuzz/property,
  stress/torture, sanitizer, and benchmark lanes separate from default evidence.
- Report skipped, unavailable, deferred, and not-run lanes as such.
- Treat benchmark smoke as harness evidence only, not performance evidence.
- Keep docs/static claim checks path-tiered: strict for current/public docs, broad for
  secrets, and tolerant of archive/planning/test fixtures where historical text is scoped.
- Print clear failure messages with file/line output where practical.
