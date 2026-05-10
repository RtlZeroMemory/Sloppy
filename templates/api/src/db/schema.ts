export const createUsersTableSql =
    "create table if not exists users (id integer primary key, name text not null, email text not null unique)";

export const seedUsers = [
    [1, "Ada Lovelace", "ada@example.test"],
    [2, "Grace Hopper", "grace@example.test"],
];
