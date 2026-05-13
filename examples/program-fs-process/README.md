# Program Filesystem And Process

Program Mode can use the Sloppy stdlib without declaring a web app. This
example writes a small file, reads it back, and runs `git --version` through
`Process.run`.

Prerequisite: `git` must be installed and available on `PATH` for the
`Process.run("git", ["--version"])` call.

Run from this directory with a handler-capable build:

```powershell
sloppy run -- report
```

Build and run the artifacts:

```powershell
sloppy build
sloppy run .sloppy -- report
```

Package and run from the package directory:

```powershell
sloppy package
sloppy run .sloppy/package -- report
```

The example writes under `./tmp`. It uses `sloppy/fs` and `sloppy/os`; it does
not use Node `fs`, `process`, or `child_process` globals.
