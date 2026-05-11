import probe from "buffer-probe";

export async function main() {
    const value = probe as {
        utf8: string;
        byteLen: number;
        hex: string;
        base64: string;
        concat: string;
    };
    console.log(
        `buffer: utf8=${value.utf8} byteLen=${value.byteLen} hex=${value.hex} base64=${value.base64} concat=${value.concat}`,
    );
    return 0;
}
