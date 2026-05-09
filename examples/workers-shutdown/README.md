# Workers Shutdown

This example shows worker shutdown behavior for queue draining.

Shows explicit queue drain behavior. `stop({ drain: true })` rejects new admission and waits
for already admitted work to settle.
