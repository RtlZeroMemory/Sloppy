# CLI Starter

A practical public alpha CLI starter shaped like a real local tool. Command
dispatcher, subcommands, filesystem inspection, safe platform indicators,
clear exit codes, and the packaged CLI flow.

Public alpha, pre-production. APIs and artifact formats may change between
alpha revisions.

## Build, run, package

```sh
sloppy run src/main.ts -- --help
sloppy run src/main.ts -- echo hello
sloppy run src/main.ts -- inspect package.json
sloppy build
sloppy run .sloppy -- --help
sloppy package
sloppy run .sloppy/package -- echo hello
```

Expected output:

- `--help` prints the subcommand list and exits with code `0`.
- `echo hello` prints `hello`.
- `inspect <path>` prints filesystem metadata for `<path>` or exits non-zero
  with a diagnostic.

## Where to edit next

- Add a command module under `src/commands/` and dispatch it from
  `src/main.ts`.
- Keep argument parsing in one place so error messages stay consistent.
- Use Sloppy stdlib (`sloppy/fs`, `sloppy/os`, `sloppy/time`) for system
  access.

## Current limitations

- Public alpha, pre-production. APIs and artifact formats may change between
  alpha revisions.
- Program Mode does not provide full Node globals, native addons, or raw
  terminal APIs.
- Console output is collected during the run and flushed after the
  entrypoint completes; long-running progress UIs need a different model.
