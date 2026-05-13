export function createDiagnosticsSink() {
    const entries = [];
    return Object.freeze({
        push(entry) {
            entries.push(Object.freeze({ ...entry }));
        },
        recent() {
            return Object.freeze(entries.slice().reverse());
        },
    });
}
