import { alpha, beta } from "cjs-named-target";

export async function main(): Promise<number> {
    console.log(`named: alpha=${alpha} beta=${beta}`);
    return 0;
}
