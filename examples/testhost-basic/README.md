# TestHost Basic

```js
import assert from "node:assert/strict";
import test from "node:test";
import { Results, Sloppy, TestHost } from "sloppy";

test("creates a user", async () => {
    const app = Sloppy.create();

    app.post("/users", (ctx) => Results.json({
        email: ctx.request.json().email,
    }, { status: 201 }));

    await using host = await TestHost.create(app);

    await host
        .post("/users")
        .json({ email: "ada@example.com" })
        .expectStatus(201)
        .then((response) => response.expectJson({ email: "ada@example.com" }));
});
```

Use `TestHost.fromArtifacts(".sloppy")` when the test must exercise compiled
artifacts and the native runtime path.
