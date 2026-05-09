# Workers JS Isolate

This is a V8-enabled worker API example.

Shows the explicit `Worker.start(...).invoke(...).stop()` API shape.

With V8 enabled, the worker module is loaded as Sloppy-owned source, invoked in
a worker-owned isolate, and settled back on the app isolate owner thread.
