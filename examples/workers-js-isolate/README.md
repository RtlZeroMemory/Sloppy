# Workers JS Isolate

This is a V8-lane worker API example.

Shows the explicit `Worker.start(...).invoke(...).stop()` API shape.

In the V8 lane, the worker module is loaded as Sloppy-owned source, invoked in a
worker-owned isolate, and settled back on the app isolate owner thread.
