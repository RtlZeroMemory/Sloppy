import pkg from "exports-mut";

export async function main(): Promise<number> {
    const value = pkg as { foo: string; bar: string };
    console.log(`foo=${value.foo} bar=${value.bar}`);
    return 0;
}
