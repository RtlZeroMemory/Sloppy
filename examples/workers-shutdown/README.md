# Workers Shutdown

This is a worker API-shape example. It is not a production supervision model or public release
guide.

Shows explicit queue drain behavior. `stop({ drain: true })` rejects new admission and waits
for already admitted work to settle.
