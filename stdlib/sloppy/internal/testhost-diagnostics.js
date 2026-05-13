import { redactObject, redactTextSecrets } from "./redaction.js";
import { isPlainObject, requirePlainObject } from "./validation.js";

function redactConfiguredSecrets(value, secretTexts) {
    return redactTextSecrets(value, secretTexts);
}

function redactedValue(key, value, secretTexts = []) {
    return redactObject({ [key]: value }, {
        secrets: secretTexts,
        redactText: (text) => redactConfiguredSecrets(text, secretTexts),
        stringifyPrimitives: true,
    })[key];
}

export function normalizeOverrideMap(value, subject) {
    if (value === undefined) {
        return Object.freeze({});
    }
    requirePlainObject(value, `Sloppy TestHost ${subject} overrides must be a plain object.`);
    return Object.freeze({ ...value });
}

export function createDiagnosticsStore(secrets = []) {
    const records = [];
    const secretTexts = secrets
        .filter((value) => typeof value === "string" && value.length > 0)
        .map((value) => String(value));

    function sanitizeRecord(record) {
        const fields = {};
        for (const [key, value] of Object.entries(record.fields ?? {})) {
            fields[key] = redactedValue(key, value, secretTexts);
        }
        return Object.freeze({
            code: String(record.code),
            subsystem: record.subsystem ?? "testhost",
            severity: record.severity ?? "info",
            message: redactConfiguredSecrets(record.message ?? record.code, secretTexts),
            fields: Object.freeze(fields),
        });
    }

    const diagnostics = {
        record(record) {
            records.push(sanitizeRecord(record));
        },
        snapshot() {
            return Object.freeze([...records]);
        },
        latest() {
            return records.at(-1);
        },
        filter(criteria = {}) {
            if (!isPlainObject(criteria)) {
                throw new TypeError("Sloppy TestHost diagnostics filter criteria must be a plain object.");
            }
            return Object.freeze(records.filter((record) => {
                if (criteria.code !== undefined && record.code !== criteria.code) {
                    return false;
                }
                if (criteria.subsystem !== undefined && record.subsystem !== criteria.subsystem) {
                    return false;
                }
                if (criteria.severity !== undefined && record.severity !== criteria.severity) {
                    return false;
                }
                return true;
            }));
        },
        expectCode(code) {
            if (!records.some((record) => record.code === code)) {
                throw new Error(`Sloppy TestHost expected diagnostic code '${code}'.`);
            }
            return diagnostics;
        },
        expectNoSecretLeaks() {
            const text = JSON.stringify(records);
            for (const secret of secretTexts) {
                if (text.includes(secret)) {
                    throw new Error("Sloppy TestHost diagnostics leaked a configured secret value.");
                }
            }
            return diagnostics;
        },
    };
    return Object.freeze(diagnostics);
}
