# Sloppy Docs

Sloppy is a TypeScript backend runtime that compiles your app before it runs.
The compiler reads supported source, extracts routes, configuration, and
provider usage into an application **Plan**, then emits artifacts the runtime
loads, validates, and executes.

These docs cover what Sloppy can do today. Sloppy is pre-alpha — surfaces
labelled *experimental* are still moving.

## Get started

- [Install](install.md) — get the `sloppy` CLI on your machine
- [Quickstart](quickstart.md) — build and run a two-route API in five minutes

## Reference

- [API](api/README.md) — `Sloppy`, `Results`, routes, services, config, data, more
- [CLI](cli/README.md) — `sloppy build`, `sloppy run`, `sloppy doctor`, …
- [Guides](guide/README.md) — project layout, TypeScript support, examples, troubleshooting
- [Reference](reference/README.md) — Plan format, supported syntax, `sloppy.json`, stability matrix

## Background

- [About](about/README.md) — what Sloppy is, why the compiler-first model, design notes
- [Glossary](glossary.md)

## For contributors

- [Contributor docs](contributor/README.md) — building from source, tests, release artifacts
- [Internals](internals/README.md) — architecture, runtime, V8 bridge, providers
