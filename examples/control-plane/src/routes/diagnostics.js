import { Results } from "sloppy";

export function diagnosticsModule(app) {
    const db = app.provider("sqlite:main");

    app.get("/diagnostics/recent", async () => {
        await db.exec("create table if not exists diagnostics (id integer primary key, level text not null, message text not null)", []);
        await db.exec("insert into diagnostics (id, level, message) select ?, ?, ? where not exists (select 1 from diagnostics where id = ?)", [1, "info", "dogfood app bootstrapped", 1]);
        return Results.ok(await db.query("select id, level, message from diagnostics order by id desc", []));
    }).withName("Diagnostics.Recent");
}
