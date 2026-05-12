import config from "json-loader";

export async function main() {
    const value = config as { name: string; version: string };
    console.log(`config: name=${value.name} version=${value.version}`);
    return 0;
}
