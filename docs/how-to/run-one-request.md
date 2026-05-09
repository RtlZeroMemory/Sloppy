# How To Run One Request

Run a single request and exit using `sloppy run --once METHOD TARGET`.

## Prerequisites

- A valid artifact directory or source entry file.
- A V8-enabled Sloppy runtime build for handler execution.

## Steps

1. Run one request from artifacts.

```powershell
sloppy run --artifacts .sloppy --once GET /health
```

2. Or compile source input and run one request.

```powershell
sloppy run src/main.ts --once GET /hello/Ada
```

## Expected Result

The command prints an HTTP response and exits. For a healthy text route, output
starts with:

```text
HTTP/1.1 200 OK
```

For the tutorial hello app, the JSON request prints a response whose body
contains:

```json
{"hello":"Ada"}
```

## Common Failures

- `sloppy run: --once requires METHOD and TARGET`.
- `sloppy run: --once target must start with /`.
- `sloppy run: --once method is unsupported by the bounded parser`.
- `sloppy run: sloppy run requires V8-enabled build`.
