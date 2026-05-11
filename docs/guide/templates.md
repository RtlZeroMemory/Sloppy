# Templates

Sloppy templates are starting points for source-input projects.

The built-in API templates are:

| Template | Use it for |
| --- | --- |
| `minimal-api` | A small route-only API. |
| `api` | A multi-file API layout with route modules and configuration. |
| `package-api` | An API intended to exercise package/dependency artifact output. |

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
auth template yet. Add the auth setup to the generated `app.js` or route
module, then add the corresponding `Auth:*` keys to `appsettings.json` or your
deployment configuration.
