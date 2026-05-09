# Install

Sloppy is distributed as a per-platform archive that contains the `sloppy` CLI,
the `sloppyc` compiler, and the runtime stdlib. The CLI builds, runs, and
inspects Sloppy applications.

> Pre-alpha. Windows x64 archives are the most polished today. Linux x64 builds
> work but get less validation. macOS and arm64 packages are not published
> yet — for those, build from source.

## Install from an archive

1. Download the archive for your platform.

   | Platform     | Archive                       |
   | ------------ | ----------------------------- |
   | Windows x64  | `sloppy-windows-x64.zip`      |
   | Linux x64    | `sloppy-linux-x64.tar.gz`     |

2. Extract it somewhere stable (not inside a project).

   **Windows (PowerShell):**

   ```powershell
   Expand-Archive sloppy-windows-x64.zip -DestinationPath "$HOME\.sloppy"
   ```

   **Linux:**

   ```sh
   mkdir -p ~/.sloppy && tar -xzf sloppy-linux-x64.tar.gz -C ~/.sloppy
   ```

3. Add the `bin/` directory to your `PATH`.

   **Windows (current shell):**

   ```powershell
   $env:Path = "$HOME\.sloppy\sloppy-windows-x64\bin;$env:Path"
   ```

   **Linux:**

   ```sh
   export PATH="$HOME/.sloppy/sloppy-linux-x64/bin:$PATH"
   ```

   To make this permanent, add the export to your shell profile.

4. Verify.

   ```
   sloppy --version
   sloppyc --version
   ```

   Both should print a version string. `sloppy --help` lists every command.

## Build from source

If your platform has no archive yet, or you want the bleeding edge, build from
the repository. Building requires CMake, a C compiler, the Rust toolchain, and
a fetched V8 SDK for runtime execution.

See [contributor/building-from-source.md](contributor/building-from-source.md)
for the full setup. The short version on Windows:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 doctor
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
```

The built CLI lives at `build\windows-relwithdebinfo\sloppy.exe`.

## V8 and handler execution

`sloppy build` and most inspection commands work without V8. Executing
JavaScript handlers (i.e. running an actual request) needs a V8-enabled
build. Archives published with V8 included will run handlers; the source
build path requires fetching the V8 SDK first
(`.\tools\windows\resolve-v8-sdk.ps1 -Fetch` on Windows).

If `sloppy run` fails with a "V8 unavailable" diagnostic, your runtime can
still build and validate artifacts — it just can't execute handlers yet.

## Common pitfalls

- **`sloppy: command not found`** — the `bin/` directory isn't on `PATH`. Re-run
  the export step or restart your shell.
- **Mixing repo binaries with installed ones** — running inside the Sloppy repo
  doesn't pick up `build\…\sloppy.exe` automatically. Either invoke that path
  directly or put it on `PATH`.
- **`node_modules` not resolved** — Sloppy apps don't import npm packages. The
  npm distribution channel installs the `sloppy` CLI itself, not application
  dependencies. See [about/why-no-node-modules.md](about/why-no-node-modules.md).

Next: [Quickstart](quickstart.md).
