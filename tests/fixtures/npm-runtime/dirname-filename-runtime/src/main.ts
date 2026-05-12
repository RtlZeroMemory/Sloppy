import pkg from "dirname-pkg";

export async function main() {
    const value = pkg as {
        filenameEndsWith: string;
        dirnameEndsWith: string;
    };
    console.log(
        `dirname: filenameEndsWith=${value.filenameEndsWith} dirnameEndsWith=${value.dirnameEndsWith}`,
    );
    return 0;
}
