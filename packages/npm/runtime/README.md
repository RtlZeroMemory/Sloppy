# @sloppy/runtime

This package is a thin launcher for installing the Sloppy runtime. It selects an installed
platform package and forwards arguments to the packaged `sloppy` binary.

npm is only a distribution channel for Sloppy itself. Sloppy apps do not support arbitrary
npm package imports, `node_modules` resolution, Node built-ins, CommonJS compatibility, or
package-manager behavior in this alpha track.

The package has no native install script, no `node-gyp`, and no postinstall V8 build or
download.
