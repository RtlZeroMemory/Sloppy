import { Sloppy, data, sql } from "sloppy";
import { Environment } from "sloppy/os";

function requireEnvironment(name) {
    const value = Environment.get(name);
    if (value === undefined || value === "") {
        throw new Error(`Missing required environment value: ${name}`);
    }
    return value;
}

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
            connectionString: requireEnvironment("SLOPPY_POSTGRES_TEST_URL"),
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
