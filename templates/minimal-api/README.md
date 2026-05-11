# Minimal API Template

This is the smallest public alpha, pre-production API starter. Use it for a
quick first run or a smoke test; use `api` when you want a fuller backend
layout.

```sh
sloppy build
sloppy routes .sloppy
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /hello/Ada
sloppy package
sloppy run .sloppy/package --once GET /health
```
