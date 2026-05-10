# Templates

`sloppy create` copies a local starter template. Templates are intentionally
small: they show supported alpha APIs without implying Node or production
framework compatibility.

```sh
sloppy create my-api --template api
cd my-api
sloppy build
sloppy run .sloppy --once GET /health
```

## API Template

The `api` template is the default backend starter. It includes:

- route modules;
- health endpoints;
- ProblemDetails handler-error mapping;
- SQLite provider metadata;
- app settings files;
- a `public/` directory registered through `app.useStaticFiles`;
- package flow examples.

Useful smoke commands:

```sh
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /public/hello.txt
sloppy run .sloppy --once POST /users --json "{\"name\":\"Ada Lovelace\",\"email\":\"ada@example.test\"}"
sloppy package
sloppy run .sloppy/package --once GET /public/hello.txt
```

## Other Templates

Use `minimal-api` for the smallest web shape, `program` for route-free Program
Mode, `cli` for command-style Program Mode, `package-api` for local package
compatibility, and `node-compat` for explicit partial Node-compatibility
experiments.

