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

## Built-In Templates

| Template | Use it for |
| --- | --- |
| `minimal-api` | Smallest web API starter. |
| `api` | SQLite-backed API starter with routes, services, provider config, migrations, health, static files, and packaging flow. |
| `program` | Program Mode starter. |
| `cli` | CLI-style Program Mode starter. |
| `package-api` | API starter that uses a compatible local pure-JavaScript package and exercises package/dependency artifact output. |
| `node-compat` | Program starter using supported Node compatibility shims. |

## API Template

The `api` template is the default alpha starter for a Sloppy Web Mode backend.
Useful smoke commands:

```sh
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /public/hello.txt
sloppy run .sloppy --once POST /users --json "{\"name\":\"Ada Lovelace\",\"email\":\"ada@example.test\"}"
sloppy package
sloppy run .sloppy/package --once GET /public/hello.txt
```

The `api` template's `POST /users` route uses `Schema` validation:

```ts
const input = await ctx.body.validate(CreateUser);
```

The route also declares `.accepts(CreateUser)` and `.returns(User)`, so
`sloppy routes`, `sloppy audit`, and `sloppy openapi` can report request and
response metadata from the generated Plan.

## Authentication

Auth is available from any API template:

```ts
import { Auth, Config } from "sloppy";

app.use(Auth.jwtBearer({
  issuer: "sloppy.local",
  audience: "api",
  secret: Config.required("Auth:JwtSecret"),
}));
```

Auth setup in templates is public-alpha/experimental. There is no dedicated
auth template yet. Add the auth setup to the generated app file or route
module, then add the corresponding `Auth:*` keys to `appsettings.json` or your
deployment configuration.

## API Template Migrations

The `api` template includes:

- `migrations/0001_create_users.sql`
- `sloppy.json` migration metadata for provider `main`
- runtime migration usage through `Migrations.apply(...)`

Build and apply the template database schema:

```sh
sloppy build
sloppy db migrate .sloppy --provider main
```

Package output includes the migration file and migration manifest metadata, so
the same command shape works against `.sloppy/package`.

## Other Templates

Use `program` for route-free Program Mode, `cli` for command-style Program
Mode, and `node-compat` for explicit partial Node-compatibility experiments.
