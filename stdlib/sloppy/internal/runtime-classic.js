(() => {
    const TEXT_CONTENT_TYPE = "text/plain; charset=utf-8";
    const JSON_CONTENT_TYPE = "application/json; charset=utf-8";
    const PROBLEM_CONTENT_TYPE = "application/problem+json; charset=utf-8";

    function resolveStatus(options, defaultStatus) {
        const status = options?.status ?? defaultStatus;

        if (!Number.isInteger(status) || status < 100 || status > 999) {
            throw new TypeError("Sloppy Results status must be an integer from 100 to 999.");
        }

        return status;
    }

    function createResult(kind, body, contentType, options, defaultStatus) {
        const descriptor = {
            __sloppyResult: true,
            kind,
            status: resolveStatus(options, defaultStatus),
            contentType,
        };

        if (body !== undefined) {
            descriptor.body = body;
        }

        return Object.freeze(descriptor);
    }

    function normalizeProblem(problemOrMessage, status) {
        if (problemOrMessage === undefined) {
            return Object.freeze({ title: "Sloppy problem", status });
        }

        if (typeof problemOrMessage === "string") {
            return Object.freeze({ title: problemOrMessage, status });
        }

        if (problemOrMessage === null || typeof problemOrMessage !== "object" || Array.isArray(problemOrMessage)) {
            throw new TypeError("Sloppy Results.problem value must be a string or plain problem object.");
        }

        return Object.freeze({ status, ...problemOrMessage });
    }

    const Results = Object.freeze({
        text(body, options) {
            return createResult("text", String(body), TEXT_CONTENT_TYPE, options, 200);
        },
        json(value, options) {
            return createResult("json", value, JSON_CONTENT_TYPE, options, 200);
        },
        ok(value, options) {
            return createResult("json", value, JSON_CONTENT_TYPE, options, 200);
        },
        noContent() {
            return createResult("empty", undefined, undefined, undefined, 204);
        },
        problem(problemOrMessage, options) {
            const status = resolveStatus(options, 500);
            return createResult(
                "problem",
                normalizeProblem(problemOrMessage, status),
                PROBLEM_CONTENT_TYPE,
                { ...options, status },
                status,
            );
        },
    });

    globalThis.__sloppy_runtime = Object.freeze({
        Results,
    });
})();
