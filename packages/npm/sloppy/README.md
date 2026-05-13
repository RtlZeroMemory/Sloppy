# @slopware/sloppy

This package is a thin launcher for installing the Sloppy runtime. It selects an installed
platform package and forwards arguments to the packaged `sloppy` binary.

The alpha root package references supported platform packages for Windows x64,
Linux x64 GNU, and macOS. Platform packages contain the native CLI binaries,
bootstrap stdlib, built-in project templates, selected docs/examples,
manifest, and license files. The launcher sets `SLOPPY_SLOPPYC` so commands
such as `sloppy create`, `sloppy build`, and `sloppy package` work outside a
repository checkout.

The root package includes TypeScript declarations for the public starter
surface (`sloppy`, `sloppy/data`, `sloppy/fs`, `sloppy/os`, and
`sloppy/providers/sqlite`) so app workspaces can install it as a local dev
dependency for editor IntelliSense.

npm is only a distribution channel for Sloppy itself. Sloppy apps can bundle
compatible already-installed pure-JavaScript packages when their imports fit
Sloppy's resolver, module loader, and runtime boundary. Sloppy does not install
packages from a registry, solve semver, support Node native addons/N-API, or
claim full Node compatibility in this alpha track.

The package has no native install script, no `node-gyp`, and no postinstall V8 build or
download.
