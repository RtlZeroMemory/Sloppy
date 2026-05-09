# Documentation Home

This is the product documentation entrypoint for Sloppy.

Documentation is organized by reader need using Diataxis:
tutorials, how-to guides, reference, and explanation.

## Start Here

| Goal | Page |
| --- | --- |
| Build and run the smallest app | [Tutorial: Build your first Sloppy API](tutorials/first-api.md) |
| Build artifacts from source input | [How-to: Build artifacts](how-to/build-artifacts.md) |
| Run one bounded request | [How-to: Run one request](how-to/run-one-request.md) |
| Understand all CLI commands and flags | [Reference: CLI](reference/cli.md) |
| Understand framework/runtime shape | [Reference: Framework](reference/framework.md) |
| Build and test the repository itself | [Contributor: Building from source](contributor/building-from-source.md) |

## Tutorials (Learn)

- [Build your first Sloppy API](tutorials/first-api.md)
- [Build a SQLite API](tutorials/sqlite-api.md)

## How-To Guides (Do)

- [Install or build Sloppy locally](how-to/install-sloppy.md)
- [Create a project](how-to/create-a-project.md)
- [Build artifacts](how-to/build-artifacts.md)
- [Run an app](how-to/run-an-app.md)
- [Run one request](how-to/run-one-request.md)
- [Configure an app](how-to/configure-an-app.md)
- [Use SQLite](how-to/use-sqlite.md)
- [Run live PostgreSQL checks](how-to/run-live-postgres-checks.md)
- [Run live SQL Server checks](how-to/run-live-sqlserver-checks.md)
- [Package Sloppy locally](how-to/package-sloppy.md)
- [Troubleshoot V8](how-to/troubleshoot-v8.md)

## Reference (Look Up)

- [CLI](reference/cli.md)
- [Configuration](reference/configuration.md)
- [Data API](reference/data-api.md)
- [Dependency injection](reference/dependency-injection.md)
- [Diagnostics](reference/diagnostics.md)
- [Framework](reference/framework.md)
- [Plan format](reference/plan-format.md)
- [Platform status](reference/platform-status.md)
- [Providers](reference/providers.md)
- [Request context](reference/request-context.md)
- [Results](reference/results.md)
- [Routing](reference/routing.md)
- [`sloppy.json`](reference/sloppy-json.md)
- [Stability](reference/stability.md)
- [Supported syntax](reference/supported-syntax.md)
- [Validation](reference/validation.md)

## Explanation (Understand)

- [What is Sloppy?](explanation/what-is-sloppy.md)
- [Source input and artifacts](explanation/source-input-and-artifacts.md)
- [Compiler and Plan model](explanation/compiler-and-plan-model.md)
- [Configuration model](explanation/configuration-model.md)
- [Provider runtime model](explanation/provider-runtime-model.md)
- [V8 bridge model](explanation/v8-bridge-model.md)
- [Packaging model](explanation/packaging-model.md)
- [Security and redaction](explanation/security-and-redaction.md)

## Contributor Docs

- [Building from source](contributor/building-from-source.md)
- [Development scripts](contributor/dev-scripts.md)
- [Testing](contributor/testing.md)
- [Quality gates](contributor/quality-gates.md)
- [Coding standards](contributor/coding-standards.md)
- [Documentation](contributor/documentation.md)
- [Review playbook](contributor/review-playbook.md)
- [Release artifacts](contributor/release-artifacts.md)
- [V8 SDK](contributor/v8-sdk.md)

## Internals

- [Architecture](internals/architecture.md)
- [Runtime](internals/runtime.md)
- [Compiler](internals/compiler.md)
- [Plan](internals/plan.md)
- [V8 bridge](internals/v8-bridge.md)
- [Provider runtime](internals/provider-runtime.md)
- [HTTP runtime](internals/http-runtime.md)
- [Async runtime](internals/async-runtime.md)
- [Memory model](internals/memory-model.md)
- [Platform boundaries](internals/platform-boundaries.md)
- [Security model](internals/security-model.md)

## Boundaries

- Sloppy is pre-alpha.
- Runtime execution requires V8-enabled builds.
- Provider, packaging, and performance statements must be backed by explicit evidence lanes.
