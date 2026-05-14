function normalizeRow(row) {
  return row ?? { id: 0, name: "missing" };
}

export async function listUsers(db) {
  return await db.query("select id, name from users order by id", []);
}

export async function findUser(db, id) {
  return normalizeRow(await db.queryOne("select id, name from users where id = ?", [id]));
}

export async function createUser(db, name) {
  await db.exec("insert into users (name) values (?)", [name]);
  return await db.queryOne("select id, name from users where id = last_insert_rowid()", []);
}

export async function updateUser(db, id, name) {
  await db.exec("update users set name = ? where id = ?", [name, id]);
  return await findUser(db, id);
}

export async function deleteUser(db, id) {
  await db.exec("delete from users where id = ?", [id]);
  return { deleted: true };
}
