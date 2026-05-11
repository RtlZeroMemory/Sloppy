import pkg from "self-ref-pkg";

export async function main() {
    const value = pkg as { identityMatch: boolean; tag: string };
    console.log(`self-ref: identity=${value.identityMatch} tag=${value.tag}`);
    return 0;
}
