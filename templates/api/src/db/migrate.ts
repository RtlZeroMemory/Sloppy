import { createUsersTableSql, seedUsers } from "./schema.ts";

export async function migrateUsers(db) {
    await db.transaction(async (tx) => {
        await tx.exec(createUsersTableSql, []);
        for (const user of seedUsers) {
            await tx.exec(
                "insert or ignore into users (id, name, email) values (?, ?, ?)",
                user,
            );
        }
    });
}
