# Program Hello

Minimal Program Mode source. It does not declare a web app or routes; the
compiler emits a Program Plan with an opaque metadata status and a
`__sloppy_program_main` entrypoint.

Run the source directly when using a V8-enabled build:

```powershell
sloppy run main.ts
```

Build and inspect the generated artifacts:

```powershell
sloppy build main.ts
sloppy run .sloppy
sloppy routes .sloppy --format json
sloppy capabilities .sloppy
```

`sloppy.json` is optional project pinning for this example. From this directory,
the same project can be built with:

```powershell
sloppy build
sloppy run .sloppy
```

Program execution requires a V8-enabled build. In a default non-V8 developer
build, `sloppy run main.ts` and `sloppy run .sloppy` fail before V8
initialization with a required-feature diagnostic.
