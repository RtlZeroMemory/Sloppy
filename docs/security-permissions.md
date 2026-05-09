# Security And Permissions

## Purpose

Sloppy is built around explicit authority boundaries. Permission-related behavior
should be visible in metadata, validated at startup, and enforced at runtime
entry points.

This is an auditability model, not OS-level sandboxing.

## Current Status

From the current source:

- plan parsing validates provider/capability sections and rejects malformed or
  inconsistent metadata (`src/core/plan_parse.c`);
- app-host startup re-validates provider/capability relationships before serving
  (`src/core/app_host.c`);
- provider modules include redaction-aware diagnostic behavior for
  connection-string secrets (`src/data/postgres.c`, `src/data/sqlserver.c`);
- V8 bridge code keeps resource ownership private and avoids raw native pointer
  exposure (`src/engine/v8/*`).

## Capability Model

Capability metadata is treated as a runtime contract. Missing or inconsistent
provider/capability relationships fail validation before normal request
execution.

The design goal is clear denial signals, not implicit ambient access.

## Secret Handling

Plan artifacts are not allowed to carry obvious secret-bearing fields. Provider
diagnostics must redact secret values while keeping failure categories usable.

## Deferred Work

This model still defers OS sandbox enforcement, production hardening claims, and
broader release-grade security posture work.
