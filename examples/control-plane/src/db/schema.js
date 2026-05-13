export const schemaStatements = Object.freeze([
    "create table if not exists projects (id integer primary key, slug text not null unique, name text not null, owner text not null)",
    "create table if not exists apps (id integer primary key, project_id integer not null, name text not null, environment text not null)",
    "create table if not exists builds (id integer primary key, app_id integer not null, commit_sha text not null, status text not null)",
    "create table if not exists deployments (id integer primary key, app_id integer not null, build_id integer not null, status text not null)",
    "create table if not exists diagnostics (id integer primary key, level text not null, message text not null)",
]);
