# Why Sloppy Does Not Load node_modules

Sloppy intentionally avoids `node_modules` resolution in the current phase.

The reason is architectural, not accidental: Sloppy is designed around
compile-time structure extraction and strict artifact validation. Arbitrary npm
graph resolution would weaken that deterministic contract unless Sloppy also
owned a much larger compatibility and package policy surface.

Current code and packaging scripts keep this boundary explicit:

- runtime execution flows through compiler artifacts and validated plans;
- package scripts call out that distribution is experimental and "no package
  manager";
- npm-oriented dry-run packaging is a launcher/distribution path for Sloppy
  binaries, not app dependency compatibility.

So "npm package exists" and "Sloppy apps are npm dependency-compatible" are
different statements. Only the first is in scope today.
