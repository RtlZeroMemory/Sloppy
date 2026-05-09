# Install

Public release archives and npm packages aren't published yet. There are
three supported local paths today: build from source, build a local archive
and extract it, or run the local npm packaging proof from a tested archive.

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

`.\tools\windows\dev.ps1 package` produces a per-platform archive under
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

The archive includes `bin/`, `stdlib/`, `templates/`, selected docs/examples,
and `manifest.json`. The templates are used by `sloppy create` after install.

`dev.ps1 test-package` smokes the archive from outside the checkout so
you can confirm the layout is good before publishing. `dev.ps1 test-install`
extracts the archive, puts `bin/` on `PATH`, runs `sloppy --version`,
`sloppy doctor`, `sloppy create`, `sloppy build`, `sloppy package`, and a
`sloppy run --once` smoke.

> Local archives are pre-alpha distribution. Linux archives can be built from
> source and smoked with `tools/unix/test-install.sh`; macOS and arm64 archives
> still need hosted proof before they are user-facing. Public GitHub Release
> archives and npm packages are not published by this PR.

## Local npm package proof

The npm package name prepared by the repository is `@rtlzeromemory/sloppy`.
It is a launcher package for Sloppy itself, not npm dependency support for
Sloppy apps. Platform packages are:

- `@rtlzeromemory/sloppy-win32-x64`
- `@rtlzeromemory/sloppy-linux-x64`
- `@rtlzeromemory/sloppy-darwin-arm64`
- `@rtlzeromemory/sloppy-darwin-x64`

The root package resolves the installed platform package and runs the packaged
`sloppy` binary. Platform packages are staged from already-built archives and
must include `bin/`, `stdlib/`, `templates/`, `manifest.json`, and license/readme
files.

Local proof on Windows:

```powershell
.\tools\windows\dev.ps1 package
.\tools\windows\dev.ps1 npm-dry-run
```

Local proof on Linux:

```sh
./tools/unix/dev.sh package
./tools/unix/dev.sh npm-dry-run --package-path artifacts/packages/sloppy-linux-x64.tar.gz
```

Those dry-runs create local tarballs under `artifacts/npm/tarballs/`, install
them into a temp prefix, run the launcher, scaffold a minimal app, build it,
and smoke `/health`. They do not publish to npm.

When alpha packages are actually published, the intended user command is:

```sh
npm install -g @rtlzeromemory/sloppy
```

Until then, install from source or from a local archive.

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
sloppy create tmp-api --template minimal-api --no-git
```

Both binaries should print a version. `sloppy --help` lists every
command. `sloppy doctor` reports environment health. `sloppy create` proves
the installed templates are discoverable.

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
