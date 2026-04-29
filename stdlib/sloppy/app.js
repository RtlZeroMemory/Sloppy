function validatePattern(pattern) {
    if (typeof pattern !== "string" || pattern.length === 0 || !pattern.startsWith("/")) {
        throw new TypeError("Sloppy app.mapGet pattern must be a non-empty string starting with '/'.");
    }
}

function validateHandler(handler) {
    if (typeof handler !== "function") {
        throw new TypeError("Sloppy app.mapGet handler must be a function.");
    }
}

function snapshotRoute(route) {
    return Object.freeze({
        method: route.method,
        pattern: route.pattern,
        handler: route.handler,
        name: route.name,
        metadata: Object.freeze({ ...route.metadata }),
    });
}

function createEndpointBuilder(route) {
    return Object.freeze({
        withName(name) {
            if (typeof name !== "string" || name.length === 0) {
                throw new TypeError("Sloppy endpoint name must be a non-empty string.");
            }

            route.name = name;
            return this;
        },
    });
}

function create() {
    const routes = [];

    return Object.freeze({
        mapGet(pattern, handler) {
            validatePattern(pattern);
            validateHandler(handler);

            const route = {
                method: "GET",
                pattern,
                handler,
                name: null,
                metadata: {},
            };

            routes.push(route);
            return createEndpointBuilder(route);
        },

        __getRoutes() {
            return Object.freeze(routes.map(snapshotRoute));
        },
    });
}

export const Sloppy = Object.freeze({
    create,
});
