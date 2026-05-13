# Static Assets Runtime

Static assets currently have two execution paths.

`TestHost.create(app)` reads the registered metadata directly from the frozen
app object and serves files from the project root in process. It is the fastest
way to test path safety, headers, ranges, precompressed variants, and SPA
fallback behavior without producing artifacts.

The compiler snapshots literal `app.staticFiles(...)`, `app.spa(...)`, and
compatibility `app.useStaticFiles(...)` calls. It emits generated handlers for
supported files, records dependency graph assets, and includes precompressed
siblings when configured. The package command copies those graph assets into
`artifacts/assets/` so packages are self-contained.

## Security Boundaries

Static roots must be project-relative directories. Traversal and absolute
roots are rejected at registration or compile time. Request paths are decoded
before path resolution; control characters, backslashes, absolute paths, drive
prefixes, empty segments, `.`, and `..` are rejected.

Dotfiles are denied by default. `dotfiles: "deny"` returns `403`, `ignore`
returns `404`, and `allow` serves the file.

## Generated Handler Limits

Generated handlers inline file bytes in JavaScript. That keeps package smoke
tests independent from the original checkout, but it is still an alpha
implementation detail. Large files must stay under `maxFileBytes`, and Sloppy
does not watch asset directories or serve files added after build time.

The current Plan exposes static routes through normal route metadata and
dependency graph assets. Dedicated native file-send metadata is not part of the
stable Plan contract yet.

Generated validators are content-sensitive weak ETags. Generated handlers omit
`Last-Modified` because source mtimes and build times are not stable artifact
version markers.

Generated SPA fallbacks are represented as a finite set of route patterns for
the mount path and nested paths up to the native route parameter limit. A true
arbitrary-depth SPA fallback is deferred until the route artifact has a native
catch-all shape.
