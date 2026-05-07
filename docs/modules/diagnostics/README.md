# Diagnostics Module

## Purpose

The diagnostics module owns stable failure reporting for core/runtime code. It provides
diagnostic builders, stable codes, text rendering, JSON rendering, source-frame support,
related locations, hints, and redaction-oriented conventions.

## Current Status

Diagnostics are used by Plan parsing, HTTP, route matching, capability checks, providers,
app-host startup, compiler fixtures, V8-gated exception mapping, and CLI output paths.

## Invariants

- Stable codes are contracts.
- Messages explain the violated contract.
- Hints describe safe next actions without claiming unsupported behavior.
- Secrets and raw native pointers must not appear in diagnostics.
- JSON output must remain deterministic where covered by goldens.

## Tests

Golden diagnostics are semantic contracts. Updates must explain the behavior change and
preserve redaction, source-span, related-location, and hint intent where applicable.
