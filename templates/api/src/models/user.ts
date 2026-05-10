export function toUser(row) {
    return {
        id: row.id,
        name: row.name,
        email: row.email,
    };
}
