# Error Handling

Use `app.useErrors(...)` near the top of your app so later middleware and routes
share one error policy.

```ts
import { Config, RequestId, Results, Sloppy } from "sloppy";

const app = Sloppy.create();

app.useErrors({
  includeDetails: Config.boolean("Errors:IncludeDetails", false),
});
app.use(RequestId.defaults());
```

By default, Sloppy returns safe problem responses. Internal exception messages
and stack traces are hidden. Turn details on only for local development:

```ts
app.useErrors({ detail: "development" });
```

Typed mappings are useful for domain errors:

```ts
class NotFoundError extends Error {}

app.mapError(NotFoundError, (err, ctx) =>
  Results.problem({
    status: 404,
    title: "Not found",
    detail: err.message,
    requestId: ctx.requestId,
  }, { status: 404 })
);
```

Request validation failures from `ctx.body.validate(...)` already produce
`400 application/problem+json`. Auth failures already produce `401` or `403`.
Provider-shaped errors are intentionally redacted even when detail output is
enabled.

For tests, `Testing.createHost(app)` also applies the policy to missing routes,
unsupported media types, and oversized request bodies.
