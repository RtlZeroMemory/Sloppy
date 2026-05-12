// node:child_process is unsupported and must fail at compile time.
import * as cp from "node:child_process";

export async function main() {
    void cp;
    return 0;
}
