export function echo(args) {
    const text = args.join(" ");
    if (text.length === 0) {
        console.error("echo requires text");
        return 1;
    }
    console.log(text);
    return 0;
}
