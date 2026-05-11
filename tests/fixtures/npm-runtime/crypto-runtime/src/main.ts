import probe from "crypto-probe";

export async function main() {
    const value = (await (probe as () => Promise<{
        sha256: string;
        randomLen: number;
        timingEq: boolean;
    }>)());
    console.log(
        `crypto: sha256=${value.sha256} randomLen=${value.randomLen} timingEq=${value.timingEq}`,
    );
    return 0;
}
