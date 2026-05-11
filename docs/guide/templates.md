# Templates

Sloppy templates are starting points for source-input projects.

```sh
sloppy create my-api --template api
```

Current public templates are:

| Template | Use it for |
| --- | --- |
| `api` | SQLite-backed API starter with routes, services, provider config, migrations, health, and packaging flow. |
| `minimal-api` | Smallest web API starter. |
| `program` | Program Mode starter. |
| `cli` | CLI-style Program Mode starter. |
| `package-api` | API starter that uses a compatible local pure-JavaScript package. |
| `node-compat` | Program starter using supported Node compatibility shims. |

## API Template

The `api` template is the default alpha starter for a Sloppy Web Mode backend.

```sh
sloppy create api my-api
cd my-api
sloppy build
sloppy run .sloppy --once GET /health
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
