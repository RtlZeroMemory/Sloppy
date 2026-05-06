# Workers JS Isolate

Shows the explicit `Worker.start(...).invoke(...).stop()` API shape.

The example is a source-level contract example. Runtime execution of separate V8 worker
isolates remains bridge-gated until the native isolate lane has direct tests.
