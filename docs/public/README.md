# Public Documentation

Status: Bootstrap app-host skeleton implemented; runtime execution planned.

This directory is for user/developer-facing documentation: how people will use Sloppy to
build applications. It is distinct from internal architecture/spec docs under `docs/`.

Current files:

- `getting-started.md`: first app workflow;
- `app-model.md`: builder/app model;
- `routing.md`: routes and route groups;
- `results.md`: result helpers;
- `modules.md`: app modules;
- `config.md`: configuration;
- `logging.md`: logging;
- `services.md`: services;
- `data.md`: data providers;
- `permissions.md`: permissions and capabilities;
- `diagnostics.md`: user-facing diagnostics;
- `cli.md`: CLI commands.

Examples in this directory are planned API examples unless the page explicitly says the
feature is implemented.

Checked-in examples:

- `examples/hello/`: current bootstrap API-shape example using the source stdlib through a
  relative import. It is not a `sloppy run` app yet.
