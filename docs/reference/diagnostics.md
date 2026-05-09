# Diagnostics

## Purpose

Diagnostics are Sloppy's user-visible contract for failure. They should identify the
failing subsystem, point at the relevant source or artifact location when available, avoid
secret disclosure, and remain stable enough for tests and tooling.

## Model

A diagnostic can include:

- stable code;
- severity;
- primary message;
- optional primary source span;
- related spans;
- hints;
- source frames supplied at render time;
- JSON rendering for machine-readable output.

Diagnostics are data first. Text rendering and JSON rendering are views over the same
contract.

## Current Coverage

Implemented diagnostic coverage includes:

- core diagnostic builder and renderers;
- source-frame rendering when matching source text is supplied;
- JSON diagnostic rendering with stable field order;
- Plan parse and validation failures;
- compiler parse/validation diagnostics and source-map metadata;
- route parsing and matching errors;
- HTTP parser, backend, transport, request body, response writer, and timeout diagnostics;
- capability denial and provider metadata diagnostics;
- V8 exception mapping and source-map primary-span remapping in V8-enabled runs;
- app-host startup, feature activation, artifact loading, and selected CLI diagnostics;
- golden snapshots for representative negative paths.

The CLI does not yet expose one universal diagnostic-format switch for every command and
error path.

## Invariants

- Codes are stable once they are covered by tests or public docs.
- Messages should describe the contract violation, not internal implementation trivia.
- Hints should recommend a safe next action without promising unsupported features.
- Diagnostics must not expose secrets, tokens, raw provider connection strings, private
  keys, request bodies marked as sensitive, or native pointers.
- V8 exceptions and panics must not cross the ABI boundary unsafely.
- Optional checks should be listed separately from the default build/test result.

## Source Locations

Compiler and source-input diagnostics should prefer author-source spans. Runtime artifact
diagnostics should use Plan/bundle/source-map spans where validated metadata is available.
When a span cannot be trusted, the diagnostic should say what failed without inventing a
location.

## Redaction

Redaction is required for environment/config secrets, provider connection strings,
passphrases, private keys, tokens, and any field explicitly marked as secret. Tests should
cover both the presence of useful context and the absence of the secret value.

## Test Coverage

Diagnostics need negative-path tests. Good coverage includes malformed inputs, unsupported
syntax, missing files, invalid metadata, capability denial, provider failure, cancellation,
timeout, shutdown, and V8-gated exception cases where applicable. Goldens are semantic
contracts, not output dumps.

## Deferred Work

Deferred diagnostics work includes localization, richer structured categories and fix-it
metadata, IDE/protocol integration, broader CLI JSON output plumbing, and more complete
coverage for V8, live providers, package verification, stress, and benchmark cases.
