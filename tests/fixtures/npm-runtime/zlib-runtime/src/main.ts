import probe from "zlib-probe";

export async function main(): Promise<number> {
    const value = await (probe as () => Promise<{
        roundTripText: string;
        gzipLengthOk: boolean;
    }>)();
    console.log(
        `zlib: roundTripText=${value.roundTripText} gzipLengthOk=${value.gzipLengthOk}`,
    );
    return 0;
}
