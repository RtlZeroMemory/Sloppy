# App Host Module

## Purpose

The app host loads validated artifacts, activates runtime features, initializes engine and
provider boundaries, owns app/request lifecycle scopes, and coordinates development
execution paths.

## Current Status

The current host supports artifact-based execution and source-input execution through the
compiler path. It validates Plan metadata, bundle/source-map hashes, runtime features,
route/provider/capability metadata, and selected configuration inputs before execution.

Optional V8 behavior remains a separate evidence lane.

## Invariants

- Startup should fail before serving work when required metadata, features, artifacts, or
  capabilities are invalid.
- App-lifetime resources are owned by app lifecycle scope.
- Request-lifetime resources are owned by request scope.
- Shutdown and late-completion paths must not re-enter user code after terminal state.
- Source-input execution must reuse the same artifact contract as artifact-based execution.

## Non-Claims

The current app host is not a public alpha hosting model, production service supervisor,
Node-compatible runtime, package manager, or final Framework v2 host.
