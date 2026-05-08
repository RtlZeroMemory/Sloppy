import { Sloppy, data, sql } from "sloppy";

const PostgresModule = Sloppy.module("data.postgres")
    .capabilities((caps) => {
        caps.addDatabase("data.main", {
            provider: "postgres",
            configKey: "SLOPPY_POSTGRES_TEST_URL",
            access: "readwrite",
        });
    })
    .services((services) => {
        services.addSingleton("data.main", () => data.postgres.open({
            connectionString: "postgres://localhost/sloppy_test",
            maxConnections: 2,
        }));
    });

const app = Sloppy.createBuilder()
    .addModule(PostgresModule)
    .build();

const lowered = sql.lower(["select id, name from users where name = ", ""], ["Ada"], {
    placeholderStyle: data.postgres.placeholderStyle,
});

async function insertUser(db, name) {
    return db.queryOne`
        insert into users (name)
        values (${name})
        returning id, name
    `;
}

async function renameUser(db, id, name) {
    return db.transaction(async (tx) => {
        await tx.exec`update users set name = ${name} where id = ${id}`;
        return tx.queryOne`select id, name from users where id = ${id}`;
    });
}

export { app, insertUser, lowered, renameUser };
