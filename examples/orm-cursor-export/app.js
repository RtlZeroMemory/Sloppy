import { Sloppy, Results } from "../../stdlib/sloppy/index.js";
import { column, orm, table } from "../../stdlib/sloppy/orm.js";

const Users = table("users", {
    id: column.uuid().primaryKey(),
    email: column.text().notNull().unique(),
    createdAt: column.instant().notNull().defaultNow(),
});

const app = Sloppy.create();

app.get("/exports/users.ndjson", async (ctx) => {
    const cursor = await orm
        .from(Users)
        .select((u) => ({ id: u.id, email: u.email }))
        .orderBy((u) => u.id.asc())
        .cursor(ctx.db, { batchSize: 512, maxRows: 100000 });

    const stream = orm.stream.ndjson(cursor);
    return Results.stream(async (writer) => {
        for await (const chunk of stream) {
            writer.writeText(chunk);
        }
    }, {
        contentType: stream.contentType,
    });
});

export default app;
