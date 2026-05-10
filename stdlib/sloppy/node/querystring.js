function parse(input = "") {
    const output = Object.create(null);
    for (const part of String(input).split("&")) {
        if (part === "") {
            continue;
        }
        const [rawKey, rawValue = ""] = part.split("=");
        output[decodeURIComponent(rawKey.replace(/\+/g, " "))] =
            decodeURIComponent(rawValue.replace(/\+/g, " "));
    }
    return output;
}

function stringify(value = {}) {
    return Object.entries(value)
        .map(([key, item]) => `${encodeURIComponent(key)}=${encodeURIComponent(String(item))}`)
        .join("&");
}

export { parse, stringify };
export default { parse, stringify };
