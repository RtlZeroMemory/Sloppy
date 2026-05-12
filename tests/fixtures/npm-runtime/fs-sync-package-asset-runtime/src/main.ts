import pkg from "fs-sync-pkg";

export async function main() {
    const value = pkg as { exists: boolean; isFile: boolean; text: string };
    console.log(
        `fs-sync: exists=${value.exists} isFile=${value.isFile} text=${value.text}`,
    );
    return 0;
}
