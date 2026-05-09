# Workers Background Service

This is a worker API-shape example. It is not a production supervision model or public release
guide.

Shows a lifecycle-bound `BackgroundService` registered with `app.use(...)`.

The service uses a cancellation-aware loop and does not log or expose payload data.
