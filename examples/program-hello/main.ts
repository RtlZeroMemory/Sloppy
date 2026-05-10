import { message } from "./message";

export function main(args, ctx) {
    const suffix = args.length > 0 ? ` ${args.join(" ")}` : "";
    console.log(`${message}${suffix}`);
    console.log(`cwd=${ctx.cwd}`);
    return 0;
}
