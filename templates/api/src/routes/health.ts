export function healthModule(app) {
    app.mapHealthChecks({
        path: "/health",
        livenessPath: "/health/live",
        readinessPath: "/health/ready",
        checks: [
            { name: "sqlite", readiness: true, check: () => true },
        ],
    });
}
