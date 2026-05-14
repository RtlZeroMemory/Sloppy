import fs from "node:fs/promises";
import path from "node:path";

export async function loadFixtures(fixtureRoot) {
    const entries = await fs.readdir(fixtureRoot, { withFileTypes: true });
    const fixtures = [];
    for (const entry of entries.sort((left, right) => left.name.localeCompare(right.name))) {
        if (!entry.isDirectory()) {
            continue;
        }
        const root = path.join(fixtureRoot, entry.name);
        const configPath = path.join(root, "contract-fixture.json");
        let config = {
            expected: "pass",
            expectedInvariants: [],
        };
        try {
            config = {
                ...config,
                ...JSON.parse(await fs.readFile(configPath, "utf8")),
            };
        } catch (error) {
            if (error.code !== "ENOENT") {
                throw error;
            }
        }
        fixtures.push({
            name: entry.name,
            root,
            expected: config.expected,
            expectedInvariants: config.expectedInvariants ?? [],
        });
    }
    return fixtures;
}
