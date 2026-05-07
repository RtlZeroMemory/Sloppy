# Security And Permissions

## Purpose

Sloppy should make authority explicit. Application code should receive capabilities through
configuration, modules, services, and Plan-visible metadata rather than ambient global
power.

This model improves auditability and diagnostics. It is not a claim of complete OS-level
sandboxing.

## Current Status

Implemented foundations include:

- Plan-visible provider and capability metadata;
- native Plan validation for capability and provider sections;
- runtime capability registry and explicit provider-bridge checks;
- feature activation checks before selected engine/provider/transport startup;
- filesystem policy checks for implemented filesystem operations;
- network capability metadata and audit/doctor evidence for scoped TCP APIs;
- SQLite bridge checks in the V8-gated path;
- metadata-driven `doctor` and `audit` surfaces.

The current audit command is metadata-oriented. It does not execute user code, enforce an
OS sandbox, or prove production least-privilege hardening.

## Capability Model

Capability metadata is a runtime contract. The runtime must fail closed when required
capability metadata is missing, malformed, or denied. Future framework/compiler work should
generate common capability entries from provider/module declarations so ordinary apps do
not hand-author low-level policy blocks.

Every inferred capability must be Plan-visible and inspectable through tooling. If
inference is not safe, compilation should fail or require explicit metadata.

## Filesystem

Filesystem APIs must be rooted in explicit capability policy. Implemented operations must
validate path policy before native filesystem access. Diagnostics should distinguish
missing capability, denied path, unsupported operation, and native I/O failure where
practical.

## Providers

Provider operations must validate capability token, provider kind, access mode, and runtime
feature activation before native work begins. Provider bridges must not expose raw native
pointers to JavaScript.

SQLite has the current scoped executable bridge. Other provider metadata/native boundaries
must remain honest about whether JavaScript execution, live-provider evidence, and
offloaded execution are implemented.

## Secrets

Config and environment secrets must not appear in diagnostics, goldens, logs, doctor/audit
output, source maps, or generated artifacts except as approved fake placeholders used to
prove redaction. Static checks should reject high-confidence secret-looking values in docs,
examples, fixtures, and goldens.

## Native Pointers

JavaScript-facing APIs must use opaque handles, descriptors, or resource IDs. Native
pointers stay inside the runtime and bridge layers. Stale resource IDs must fail
deterministically through generation checks.

## Deferred Work

Deferred security work includes OS sandbox integration, permission prompts or enforcement
beyond current metadata and feature gates, broader audit enforcement, production
least-privilege review tooling, and remaining provider/network policy gaps.
