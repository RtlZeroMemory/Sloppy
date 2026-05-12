import target from "cjs-default-target";

export async function main() {
    const fn = target as (n: number) => string;
    console.log(`default(): ${fn(7)}`);
    return 0;
}
