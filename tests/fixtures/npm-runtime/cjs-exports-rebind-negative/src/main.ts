import pkg from "exports-rebind";

export async function main() {
    const keys = Object.keys(pkg as Record<string, unknown>).sort();
    console.log(`rebind: keeps original keys=${JSON.stringify(keys)}`);
    return 0;
}
