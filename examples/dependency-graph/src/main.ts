import path from "node:path";
import { describe } from "graph-helper";

export function main() {
    const assetPath = path.join("public", "message.txt");
    console.log(describe(assetPath));
    return 0;
}

