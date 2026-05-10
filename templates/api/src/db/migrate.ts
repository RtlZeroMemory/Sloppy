import { createUsersTableSql, seedUsers } from "./schema.ts";

export async function migrateUsers(db) {
    await db.exec(createUsersTableSql, []);
    for (const user of seedUsers) {
        await db.exec(
            "insert or ignore into users (id, name, email) values (?, ?, ?)",
            user,
        );
    }
}
