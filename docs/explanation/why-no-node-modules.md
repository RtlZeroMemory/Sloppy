# Why Sloppy Does Not Load node_modules

Sloppy does not load application dependencies from `node_modules` in the
current pre-alpha runtime.

The reason is architectural. Sloppy's compiler needs to understand the app
graph. During pre-alpha it follows Sloppy imports and supported relative
modules, then emits a deterministic Plan. Loading arbitrary npm dependency
graphs would require a separate design for package resolution, bundling,
diagnostics, compatibility, and security.

Current code and packaging scripts keep this boundary explicit:

- runtime execution flows through compiler artifacts and validated plans;
- package scripts call out that distribution is experimental and does not add
  application package-manager support;
- npm-oriented dry-run packaging is a launcher/distribution path for Sloppy
  binaries, not app dependency compatibility.

npm may still be used to install the Sloppy CLI in a later distribution shape.
Application dependency support may come later, but it is not part of the
current pre-alpha runtime.
