import { z } from "zod-like";

const userSchema = z.object({
    name: z.string(),
    score: z.number()
});

export function main(args) {
    const user = userSchema.parse({
        name: args[0] ?? "Ada",
        score: Number(args[1] ?? 42)
    });

    console.log(`validated ${user.name} score=${user.score}`);
    return 0;
}

