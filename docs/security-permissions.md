# Security And Permissions

## Purpose

Sloppy should make authority explicit. Application code should receive capabilities through
configuration, modules, and services rather than ambient global power.

This model improves auditability and diagnostics. It is not a claim of complete OS-level
sandboxing.

## Scope

This document covers:

- capability model;
- permission grants;
- filesystem capabilities;
- data provider capabilities;
- environment/config secrets;
- no JS raw pointers;
- resource ID validation;
- generation counters;
- honest sandbox wording;
- future OS sandboxing;
- audit tooling;
- diagnostics;
- testing and acceptance criteria.

## Non-Goals

The foundation phase does not implement:

- filesystem APIs;
- database providers;
- resource table;
- permission checker;
- OS sandboxing;
- full security audit tooling. The current `sloppy audit` command is metadata-only and
  does not enforce permissions or execute user code.

## Current Phase

EPIC-15 implements a bootstrap JavaScript capability metadata skeleton for database
capabilities only, and EPIC-19 adds metadata-only `sloppy audit` fixture output.
Permissions, filesystem/network capabilities, runtime enforcement, OS sandboxing, grants,
and security-grade audit behavior remain future work.
MAIN1-02 adds native Plan v1 alpha parsing and validation for metadata-only
`dataProviders` and `capabilities` sections. This makes the plan shape auditable and
startup-validated, but it still does not enforce provider, filesystem, or network access.

## Future Phase

The first implementation should model capabilities and permission declarations before
exposing filesystem or database APIs to user handlers.

## Capability Model

A capability is a named authority token. Code receives a capability through services or
explicit context, not through global APIs.

Example:

```ts
export const FilesModule = Sloppy.module("files")
  .capabilities(caps => {
    caps.addDir("files.storage", "./uploads", {
      read: true,
      write: true,
    });
  })
  .services(services => {
    services.addScoped("files.storage", scope => {
      return scope.capabilities.dir("files.storage");
    });
  });
```

The token `files.storage` should appear in the Sloppy Plan with source location and access
modes.

Implemented bootstrap database capability shape:

```ts
const DataModule = Sloppy.module("data")
  .capabilities(caps => {
    caps.addDatabase("data.main", {
      provider: "sqlite",
      path: ":memory:",
      access: "readwrite",
    });
  });
```

The current registry stores metadata only. Capability tokens must be non-empty strings,
duplicates fail, and `app.capabilities.has/get/list` exposes frozen debug metadata with the
declaring module when applicable. EPIC-16 native SQLite tests can open `:memory:` databases,
but JavaScript permission enforcement and public file database policy remain future work.

## Permission Grants

Permission grants are the runtime/configured allowance for a capability to exist.

Future grant sources may include:

- CLI flags;
- config files;
- environment-selected profiles;
- deployment manifest;
- module defaults for safe built-ins.

Grants must be visible to `sloppy audit` and diagnostics.

## Filesystem Capabilities

Filesystem capabilities should specify:

- token;
- root path;
- read/write flags;
- create/delete flags later;
- path normalization policy;
- symlink policy, deferred;
- source module.

Core runtime code must use Sloppy platform file/path abstractions. No direct OS file APIs in
core.

## Data Provider Capabilities

Database providers contribute capabilities:

- provider name;
- token, such as `data.main`;
- config key references;
- lifetime;
- permissions required by routes/modules.

Plan entries must reference config keys, provider tokens, or already-redacted placeholders,
not connection string values. Raw secrets do not belong in `app.plan.json`.

Current bootstrap metadata does not open databases, validate config keys, or enforce access.
It exists so future provider modules, plan extraction, and audit tooling have a tested API
shape to build from.

## Environment And Config Secrets

Secrets must be handled as secret values:

- connection strings are redacted;
- env var values are not printed;
- diagnostics may show key names like `DATABASE_URL`;
- plan artifacts must not contain raw secret values;
- logs default to redaction for known secret fields.

## Native Handles And Resource IDs

JavaScript must never receive raw native pointers.

JS-visible native resources use resource IDs validated by the resource table. Resource IDs
must include generation counters eventually so stale handles can be detected after close and
reuse.

Invalid, stale, closed, or wrong-kind resource IDs must fail with diagnostics.

## Runtime Permissions Are Not A Full Sandbox

Sloppy permission checks reduce accidental and application-level authority. They do not
automatically confine the entire process from every operating system side effect.

Documentation and diagnostics must be honest:

- "permission denied by Sloppy capability policy" is acceptable;
- "this process is sandboxed" is not acceptable unless OS sandboxing exists.

## Future OS Sandboxing

Future options may include:

- Windows job objects/AppContainer-style exploration;
- Linux seccomp/namespaces exploration;
- macOS sandbox profiles exploration.

These are deferred until the runtime and capability model are stable.

## Audit Tooling

Current metadata-only command:

```powershell
sloppy audit --plan app.plan.json
```

Current fixture-driven output can list static metadata from an app plan. It does not
compile source files, execute user handlers, or enforce permissions.

Planned hardened output:

- module list;
- declared capabilities;
- routes reachable from each capability;
- provider tokens and driver requirements;
- missing grants;
- dynamic mode warnings.

`sloppy audit` should use compiler/app-host emitted Sloppy Plan metadata and must not
execute user handlers.

## Error And Diagnostic Behavior

Diagnostics should cover:

- missing capability grant;
- denied filesystem path;
- denied database token;
- stale resource ID;
- wrong resource kind;
- secret redaction;
- dynamic mode authority warning.

Example:

```text
error[SLP_PERMISSION_DATABASE_DENIED]: database permission was not granted

  Token:
    data.main

  Route:
    GET /users/{id:int}

  Handler:
    Users.Get

help: add a database capability grant for data.main or remove the route dependency
```

## Testing Requirements

Security/permission tests must include:

- allowed capability use;
- denied capability use;
- path normalization edge cases;
- stale resource ID;
- wrong resource kind;
- redacted connection string;
- audit fixture output;
- dynamic mode warning.

## Quality Gates

- no JS raw native pointers;
- resource IDs validated at every boundary;
- capability diagnostics have stable codes;
- plan fixtures never include secrets;
- platform-specific path behavior is tested behind platform abstraction.

## Implementation Tasks

1. Define capability and permission plan schema.
2. Define resource ID model and generation counters.
3. Add filesystem capability structs after path abstraction exists.
4. Add database capability entries during provider phases.
5. Add diagnostics and snapshot tests.
6. Harden `sloppy audit` after compiler/app-host capability metadata exists.

## Acceptance Criteria

Permissions foundation is accepted when:

- plan schema can describe filesystem and database capabilities;
- runtime can reject missing permissions before use;
- resource IDs detect stale/closed handles;
- diagnostics are actionable and redacted;
- docs clearly state that Sloppy permissions are not OS sandboxing.

## Open Questions

- Exact CLI grant syntax.
- Symlink policy.
- Whether default dev mode grants are interactive prompts or explicit config.
- How OS sandboxing, if any, composes with Sloppy capabilities.
