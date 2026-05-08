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
- inbound HTTPS transport config validation and OpenSSL-backed TLS wrapping for the HTTP
  server;
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

SQLite, PostgreSQL, and SQL Server have scoped executable V8 bridge lanes. Provider
metadata/native boundaries must still report JavaScript execution, live-provider evidence,
driver availability, and async support separately; skipped or unavailable live lanes are
not pass evidence.

## Secrets

Config and environment secrets must not appear in diagnostics, goldens, logs, doctor/audit
output, source maps, or generated artifacts except as approved fake placeholders used to
prove redaction. Static checks should reject high-confidence secret-looking values in docs,
examples, fixtures, and goldens.

Inbound HTTP TLS passphrases and private-key material are secret-bearing values. Server TLS
diagnostics may identify the failing contract, such as missing path, unsupported backend,
certificate/key load failure, key mismatch, handshake failure, or shutdown failure, but
must not include passphrases, PEM contents, raw OpenSSL error strings with sensitive
material, or native TLS handles. The native transport keeps its arena-owned TLS passphrase
copy in private platform state and zeroes it immediately after OpenSSL key loading succeeds
or fails. TLS loopback tests generate
temporary local certificates and keys at runtime instead of committing key fixtures.

## Native Pointers

JavaScript-facing APIs must use opaque handles, descriptors, or resource IDs. Native
pointers stay inside the runtime and bridge layers. Stale resource IDs must fail
deterministically through generation checks.

## Deferred Work

Deferred security work includes OS sandbox integration, permission prompts or enforcement
beyond current metadata and feature gates, broader audit enforcement, production
least-privilege review tooling, and remaining provider/network policy gaps.
