import { isPlainObject } from "./internal/shared.js";

function normalizeOptions(options) {
    if (options !== undefined && !isPlainObject(options)) {
        throw new TypeError("Sloppy RequestLogging.defaults options must be a plain object.");
    }

    const includeRoute = options?.includeRoute ?? true;
    const includeDuration = options?.includeDuration ?? true;
    const includeRequestId = options?.includeRequestId ?? true;

    if (typeof includeRoute !== "boolean") {
        throw new TypeError("Sloppy RequestLogging includeRoute option must be a boolean.");
    }
    if (typeof includeDuration !== "boolean") {
        throw new TypeError("Sloppy RequestLogging includeDuration option must be a boolean.");
    }
    if (typeof includeRequestId !== "boolean") {
        throw new TypeError("Sloppy RequestLogging includeRequestId option must be a boolean.");
    }

    return Object.freeze({
        includeRoute,
        includeDuration,
        includeRequestId,
    });
}

function requestMethod(context) {
    return String(context?.request?.method ?? context?.method ?? "GET").toUpperCase();
}

function requestPath(context) {
    const request = context?.request;
    const value = request?.rawTarget ?? request?.target ?? request?.path ?? context?.path ??
        context?.routePattern ?? "";
    return String(value);
}

function requestRoute(context) {
    const route = context?.routePattern ?? context?.route?.pattern;
    return typeof route === "string" && route.length !== 0 ? route : undefined;
}

function responseStatus(result) {
    return Number.isInteger(result?.status) ? result.status : 200;
}

function writeRequestLog(context, options, startedAt, status) {
    if (context?.log === undefined || typeof context.log.info !== "function") {
        return;
    }

    const fields = {
        method: requestMethod(context),
        path: requestPath(context),
        status,
    };

    if (options.includeRoute) {
        const route = requestRoute(context);
        if (route !== undefined) {
            fields.route = route;
        }
    }
    if (options.includeRequestId && typeof context.requestId === "string") {
        fields.requestId = context.requestId;
    }
    if (options.includeDuration) {
        fields.durationMs = Math.max(0, Date.now() - startedAt);
    }

    context.log.info("request completed", fields);
}

function defaults(options) {
    const normalized = normalizeOptions(options);

    async function requestLoggingMiddleware(context, next) {
        const startedAt = normalized.includeDuration ? Date.now() : 0;

        try {
            const result = await next();
            writeRequestLog(context, normalized, startedAt, responseStatus(result));
            return result;
        } catch (error) {
            writeRequestLog(context, normalized, startedAt, 500);
            throw error;
        }
    }

    return Object.freeze(requestLoggingMiddleware);
}

export const RequestLogging = Object.freeze({
    defaults,
});
