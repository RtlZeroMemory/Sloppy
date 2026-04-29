const TEXT_CONTENT_TYPE = "text/plain; charset=utf-8";
const JSON_CONTENT_TYPE = "application/json; charset=utf-8";

function resolveStatus(options) {
    const status = options?.status ?? 200;

    if (!Number.isInteger(status) || status < 100 || status > 999) {
        throw new TypeError("Sloppy Results status must be an integer from 100 to 999.");
    }

    return status;
}

function createResult(kind, body, contentType, options) {
    return Object.freeze({
        __sloppyResult: true,
        kind,
        status: resolveStatus(options),
        body,
        contentType,
    });
}

function text(body, options) {
    return createResult("text", String(body), TEXT_CONTENT_TYPE, options);
}

function json(value, options) {
    return createResult("json", value, JSON_CONTENT_TYPE, options);
}

export const Results = Object.freeze({
    text,
    json,
});
