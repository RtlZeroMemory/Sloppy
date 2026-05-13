# Program Hello

Minimal Program Mode source. It does not declare a web app or routes; the
compiler emits a Program Plan with an opaque metadata status and a
`__sloppy_program_main` entrypoint.

Run the source directly when using a handler-capable build:

```powershell
sloppy run main.ts -- Ada
```

Build and inspect the generated artifacts:

```powershell
sloppy build main.ts
sloppy run .sloppy -- Ada
sloppy routes .sloppy --format json
sloppy capabilities .sloppy
```

`sloppy.json` is optional project pinning for this example. From this directory,
the same project can be built with:

```powershell
sloppy build
sloppy run .sloppy -- Ada
```

Program execution requires handler execution support. In a default non-V8
developer build, `sloppy run main.ts -- Ada` and `sloppy run .sloppy -- Ada`
fail before engine initialization with a required-feature diagnostic.
