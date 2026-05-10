# @rtlzeromemory/sloppy

This package is a thin launcher for installing the Sloppy runtime. It selects an installed
platform package and forwards arguments to the packaged `sloppy` binary.

The alpha root package references Windows x64, Linux x64 GNU, macOS arm64, and
macOS x64 platform packages. Platform packages contain the native CLI binaries,
bootstrap stdlib, built-in project templates, selected docs/examples, manifest,
and license files. The launcher sets `SLOPPY_SLOPPYC` so commands such as
`sloppy create`, `sloppy build`, and `sloppy package` work outside a repository
checkout.

npm is only a distribution channel for Sloppy itself. Sloppy apps do not support arbitrary
npm package imports, `node_modules` resolution, Node built-ins, CommonJS compatibility, or
package-manager behavior in this alpha track.

The package has no native install script, no `node-gyp`, and no postinstall V8 build or
download.
