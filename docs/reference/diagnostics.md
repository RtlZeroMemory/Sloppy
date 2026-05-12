# Diagnostics

## Purpose

Diagnostics are Sloppy's user-visible contract for failure. They should identify the
failing subsystem, point at the relevant source or artifact location when available, avoid
secret disclosure, and remain stable enough for tests and tooling.

## Model

A diagnostic can include:

- stable code;
- severity;
- subsystem, phase, status, and redaction metadata in diagnostic reports;
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
- diagnostic report JSON for local runtime, package, doctor, and crash evidence;
- fixed-size native breadcrumbs rendered as local JSONL when a report requests them;
- source-frame rendering when matching source text is supplied;
- JSON diagnostic rendering with stable field order;
- Plan parse and validation failures;
- compiler parse/validation diagnostics and source-map metadata;
- route parsing and matching errors;
- HTTP parser, backend, transport, request body, response writer, and timeout diagnostics;
- capability denial and provider metadata diagnostics;
- V8 exception mapping and source-map primary-span remapping in V8-enabled runs;
- V8 handler entry/exit/rejection breadcrumbs in runtime paths;
- app-host startup, feature activation, artifact loading, and selected CLI diagnostics;
- `sloppy run --diagnostics-json` for structured runtime diagnostics on stderr;
- unique local crash report directories under `.sloppy/reports/crashes/<counter>-<pid>/`;
- local `.sloppy/reports/package-diagnostic.json` and `.sloppy/reports/doctor-report.json`;
- safe `application/problem+json` responses for Plan-backed request validation and
  `ProblemDetails.defaults()` handler failures;
- structured app-host and native request log entries through the logging engine;
- golden snapshots for representative negative paths.

The CLI does not yet expose one universal diagnostic-format switch for every command and
error path.

## Invariants

- Codes are stable once they are covered by tests or public docs.
- Messages should describe the contract violation, not internal implementation trivia.
- Hints should recommend a safe next action without promising unsupported features.
- Diagnostics must not expose secrets, tokens, raw provider connection strings, private
  keys, request bodies marked as sensitive, or native pointers.
- ProblemDetails handler-error responses must not expose thrown exception messages unless
  an explicit development/detail policy enables them in a supported path.
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

Logging redacts sensitive structured fields before sinks receive events. Default
matching covers password, secret, token, authorization, cookie, API key, client
secret, private key, passphrase, and connection-string variants.

Request logging records request metadata only: method, path or target, status,
route pattern/name when known, request ID, and duration. It does not log request
bodies or request headers by default, so authorization, cookie, API key, proxy
authorization, provider error, and connection-string values stay out of the
request log entry.

## Test Coverage

Diagnostics need negative-path tests. Good coverage includes malformed inputs, unsupported
syntax, missing files, invalid metadata, capability denial, provider failure, cancellation,
timeout, shutdown, and V8-gated exception cases where applicable. Goldens are semantic
contracts, not output dumps.

## Deferred Work

Deferred diagnostics work includes localization, fix-it metadata, IDE/protocol integration,
production crash dump integration beyond local JSON reports, broader CLI JSON output plumbing,
and more complete coverage for live providers, package verification, stress, and benchmark cases.
