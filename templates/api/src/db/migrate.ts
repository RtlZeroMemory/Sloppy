import { Migrations } from "sloppy/data";
import { seedUsers } from "./schema.ts";

export async function migrateUsers(db) {
    await Migrations.apply(db, { provider: "sqlite", path: "migrations/*.sql" });
    await db.transaction(async (tx) => {
        for (const user of seedUsers) {
            await tx.exec(
                "insert or ignore into users (id, name, email) values (?, ?, ?)",
                user,
            );
        }
    });
}
