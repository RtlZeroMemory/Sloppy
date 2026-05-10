import { migrateUsers } from "../db/migrate.ts";
import {
    repoCreateUser,
    repoGetUserById,
    repoListUsers,
} from "../db/usersRepository.ts";

export async function listUsers(db) {
    await migrateUsers(db);
    return repoListUsers(db);
}

export async function getUser(db, id) {
    await migrateUsers(db);
    return repoGetUserById(db, id);
}

export async function createUser(db, input) {
    await migrateUsers(db);
    return repoCreateUser(db, input);
}
