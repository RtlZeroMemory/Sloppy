import { read } from "buf-fs-pkg";

export async function main(args) {
    const target = args[0] ?? "missing";
    console.log(typeof read, target);
    return 0;
}
