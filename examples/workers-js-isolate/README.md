# Workers JS Isolate

This is a V8-lane API-shape example. It is not Node, Web Worker, or package-manager
compatibility evidence.

Shows the explicit `Worker.start(...).invoke(...).stop()` API shape.

In the V8 lane, the worker module is loaded as Sloppy-owned source, invoked in a
worker-owned isolate, and settled back on the app isolate owner thread. This is not Node
`worker_threads`, Web Worker compatibility, or package-manager module resolution.
