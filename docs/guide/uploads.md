# Form Bodies And Uploads

Sloppy accepts bounded in-memory form bodies for route handlers. Use
`ctx.request.form()` for `application/x-www-form-urlencoded` and
`ctx.request.multipart()` for `multipart/form-data`.

## URL-Encoded Forms

```ts
app.post("/login", (ctx) => {
    const form = ctx.request.form();
    const email = form.get("email");

    if (email === null) {
        return Results.badRequest({ error: "email required" });
    }

    return Results.ok({ email });
});
```

`form.get(name)` returns the last value for a field or `null`. `form.entries()`
iterates `[name, value]` pairs in request order.

You can smoke this through compiled artifacts:

```sh
printf "email=ada@example.test" > form.txt
sloppy run .sloppy --header "content-type: application/x-www-form-urlencoded" --body-file form.txt --once POST /login
```

On Windows PowerShell:

```powershell
Set-Content -Path form.txt -Value "email=ada@example.test" -NoNewline
sloppy run .sloppy --header "content-type: application/x-www-form-urlencoded" --body-file form.txt --once POST /login
```

## Multipart Uploads

```ts
app.post("/avatars", async (ctx) => {
    const form = ctx.request.multipart();
    const file = form.file("avatar");

    if (file === null) {
        return Results.badRequest({ error: "avatar required" });
    }

    const safeName = file.name
        .replace(/[/\\]/g, "_")
        .replace(/[^A-Za-z0-9._-]/g, "_")
        .slice(0, 128) || "upload.bin";

    await file.saveTo(`uploads/${safeName}`);
    return Results.created(`/avatars/${safeName}`, {
        name: safeName,
        contentType: file.contentType,
        size: file.size,
    });
});
```

Uploaded file objects expose:

| Member | Notes |
| --- | --- |
| `fieldName` | Multipart field name |
| `name` | Uploaded filename |
| `contentType` | Part content type, or `application/octet-stream` |
| `size` | Byte length |
| `bytes()` | Copy of file bytes |
| `text()` | UTF-8 decoded text |
| `saveTo(path)` | Writes the file through the filesystem bridge |

The current parser is not a streaming upload engine. Request bodies are read
within the configured body limit before the handler runs. Use this for small
forms and development upload flows, not unbounded file transfer.
