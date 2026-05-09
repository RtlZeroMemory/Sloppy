# Install

Public release archives aren't published yet. There are two supported
ways to get a working `sloppy` CLI today: build from source, or build a
local archive and extract it.

## Build from source

The full path. See
[contributor/building-from-source.md](contributor/building-from-source.md)
for prerequisites and details. The short version on Windows:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 doctor
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
```

The built CLI lives at `build\windows-dev\sloppy.exe` (or
`build\windows-relwithdebinfo\sloppy.exe` for the V8-enabled preset).

Linux:

```sh
./tools/unix/bootstrap.sh
./tools/unix/dev.sh doctor
./tools/unix/dev.sh configure
./tools/unix/dev.sh build
```

Add the resulting `bin/` (or per-preset directory) to `PATH` if you
want to invoke `sloppy` from anywhere.

## Build a local archive

`dev.ps1 package` produces a per-platform archive under
`artifacts/packages/`. This is the same shape that release distribution
will eventually use.

```powershell
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 package
```

The result is something like
`artifacts/packages/sloppy-windows-x64.zip`. Extract it outside the
checkout and add its `bin/` directory to `PATH`:

```powershell
Expand-Archive .\artifacts\packages\sloppy-windows-x64.zip `
    -DestinationPath "$HOME\.sloppy"
$env:Path = "$HOME\.sloppy\sloppy-windows-x64\bin;$env:Path"
sloppy --version
```

`dev.ps1 test-package` smokes the archive from outside the checkout so
you can confirm the layout is good before publishing.

> Local archives are pre-alpha distribution. Linux archives can be
> built from source; macOS and arm64 archives still need work. Public
> GitHub Release archives and an npm launcher package are upcoming
> distribution work, not yet published.

## V8 and handler execution

`sloppy build` and the introspection commands (`sloppy routes`,
`sloppy capabilities`, `sloppy doctor`, `sloppy audit`,
`sloppy openapi`) work without V8.

Executing JavaScript handlers (i.e. `sloppy run` against a real app)
requires a V8-enabled build. Source builds fetch the V8 SDK first:

```powershell
.\tools\windows\resolve-v8-sdk.ps1 -Fetch
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
```

If `sloppy run` fails with a "V8 unavailable" diagnostic, your build
isn't V8-enabled — `sloppy build` still works, but handler execution
needs the V8 lane.

## Verify

```
sloppy --version
sloppyc --version
sloppy --help
```

Both binaries should print a version. `sloppy --help` lists every
command. `sloppy doctor` reports environment health.

## Common pitfalls

- **`sloppy: command not found`** — the `bin/` directory isn't on
  `PATH`. Re-run the export step or restart your shell.
- **Mixing repo binaries with installed ones** — running inside the
  Sloppy repo doesn't pick up `build\…\sloppy.exe` automatically.
  Either invoke that path directly or put it on `PATH`.
- **`node_modules` not resolved** — Sloppy apps don't import npm
  packages. See
  [about/why-no-node-modules.md](about/why-no-node-modules.md).

Next: [Quickstart](quickstart.md).
