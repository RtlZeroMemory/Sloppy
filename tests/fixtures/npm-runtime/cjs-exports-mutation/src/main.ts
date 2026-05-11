import pkg from "exports-mut";

export async function main() {
    console.log(`foo=${(pkg as { foo: string }).foo} bar=${(pkg as { bar: string }).bar}`);
    return 0;
}
