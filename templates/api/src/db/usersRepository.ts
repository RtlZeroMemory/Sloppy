import { toUser } from "../models/user.ts";

export async function repoListUsers(db) {
    const rows = await db.query("select id, name, email from users order by id", []);
    return rows.map(toUser);
}

export async function repoGetUserById(db, id) {
    const row = await db.queryOne("select id, name, email from users where id = ?", [id]);
    return row === null ? null : toUser(row);
}

export async function repoCreateUser(db, input) {
    await db.exec("insert into users (name, email) values (?, ?)", [input.name, input.email]);
    const row = await db.queryOne(
        "select id, name, email from users where id = last_insert_rowid()",
        [],
    );
    if (row === null) {
        throw new Error("inserted user could not be loaded");
    }
    return toUser(row);
}
