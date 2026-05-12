import pkg from "alias-pkg";

export async function main() {
    const value = pkg as { name: string; value: number };
    console.log(`alias: name=${value.name} value=${value.value}`);
    return 0;
}
