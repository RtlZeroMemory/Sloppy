# Packaging Model

Sloppy packaging is an evidence tool today, not a release claim.

`tools/windows/package.ps1` encodes that position directly:

- package README text says "experimental development artifact, not a public
  release";
- manifest sets `releaseKind` to `dry-run`;
- package includes runtime/compiler binaries, stdlib assets, examples, and
  docs;
- V8 SDK headers/import libraries are excluded;
- optional V8 runtime files are included only when explicitly requested.

The separation of lanes matters:

- source build proves checkout buildability;
- package smoke proves outside-checkout layout/startup behavior;
- npm dry-run packaging is a distribution wrapper for Sloppy itself, not
  `node_modules` support for Sloppy apps.

That is the current product boundary: packaging exists to make runtime artifacts
portable and testable, while keeping unsupported release/compatibility claims out
of band.
