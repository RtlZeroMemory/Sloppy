const WINDOWS_PATH_PATTERN = /[A-Za-z]:[\\/][^\s"'<>|)]+/gu;
const UNC_PATH_PATTERN = /\\\\[^\\/\s"'<>|)]+[\\/][^\s"'<>|)]+/gu;
const POSIX_PATH_PATTERN = /(?<![\w.-])\/(?:Users|home|tmp|var|private|workspaces|mnt)\/[^\s"'<>|)]+/gu;
const SECRET_PATTERN =
    /\b(?:token|secret|password|passwd|api[_-]?key|connectionString|access[_-]?key)\b\s*[:=]\s*["']?[^"',\s}]+/giu;

export function redactText(value) {
    if (typeof value !== "string") {
        return value;
    }
    return value
        .replace(WINDOWS_PATH_PATTERN, "<absolute-path>")
        .replace(UNC_PATH_PATTERN, "<absolute-path>")
        .replace(POSIX_PATH_PATTERN, "<absolute-path>")
        .replace(SECRET_PATTERN, (match) => {
            const separator = match.includes("=") ? "=" : ":";
            return `${match.split(separator)[0]}${separator}<redacted>`;
        });
}

export function redactValue(value) {
    if (typeof value === "string") {
        return redactText(value);
    }
    if (Array.isArray(value)) {
        return value.map((item) => redactValue(item));
    }
    if (value !== null && typeof value === "object") {
        return Object.fromEntries(
            Object.entries(value).map(([key, entry]) => [
                key,
                /token|secret|password|passwd|api[-_]?key|connection[_-]?string|access[-_]?key/iu.test(key)
                    ? "<redacted>"
                    : redactValue(entry),
            ]),
        );
    }
    return value;
}
