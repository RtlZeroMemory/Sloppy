import { Sloppy, data, sql } from "sloppy";

const SqlServerModule = Sloppy.module("data.sqlserver")
    .capabilities((caps) => {
        caps.addDatabase("data.main", {
            provider: "sqlserver",
            configKey: "SLOPPY_SQLSERVER_TEST_CONNECTION_STRING",
            access: "readwrite",
        });
    })
    .services((services) => {
        services.addSingleton("data.main", () => data.sqlserver.open({
            connectionString: "Driver={ODBC Driver 18 for SQL Server};Server=localhost;Database=sloppy_test;Trusted_Connection=yes;TrustServerCertificate=yes;",
            maxConnections: 2,
        }));
    });

const app = Sloppy.createBuilder()
    .addModule(SqlServerModule)
    .build();

const lowered = sql.lower(["select id, name from users where name = ", ""], ["Ada"], {
    placeholderStyle: data.sqlserver.placeholderStyle,
});

const doctor = data.sqlserver.doctor({
    connectionString: "Driver={ODBC Driver 18 for SQL Server};Server=localhost;Database=sloppy_test;UID=sa;PWD=<secret>;TrustServerCertificate=yes;",
});

async function insertUser(db, name) {
    await db.exec`
        insert into users (name)
        values (${name})
    `;

    return db.queryOne`
        select id, name
        from users
        where name = ${name}
    `;
}

async function renameUser(db, id, name) {
    return db.transaction(async (tx) => {
        await tx.exec`update users set name = ${name} where id = ${id}`;
        return tx.queryOne`select id, name from users where id = ${id}`;
    });
}

export { app, doctor, insertUser, lowered, renameUser };
