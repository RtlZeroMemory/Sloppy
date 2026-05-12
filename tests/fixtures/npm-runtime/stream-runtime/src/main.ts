import probe from "stream-probe";

export async function main() {
    const value = await (probe as () => Promise<{ readable: string; passThrough: string }>)();
    console.log(`stream: readable=${value.readable} passThrough=${value.passThrough}`);
    return 0;
}
