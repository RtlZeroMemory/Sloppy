# How To Troubleshoot V8

Fix V8 setup issues when `sloppy run` cannot execute handlers.

## Prerequisites

- Windows checkout with `tools/windows/resolve-v8-sdk.ps1` and `tools/windows/dev.ps1`.

## Steps

1. Resolve a compatible V8 SDK.

```powershell
.\tools\windows\resolve-v8-sdk.ps1
```

2. Configure/build the V8 preset.

```powershell
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
```

3. Verify runtime execution with one request.

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/hello-minimal/src/main.ts --once GET /health
```

## Expected Result

- SDK resolution prints a concrete V8 SDK root path.
- Build completes for `windows-relwithdebinfo`.
- `--once` returns `HTTP/1.1 200 OK`.

## Common Failures

- `No compatible Sloppy V8 SDK was resolved`: install/fetch a compatible SDK and re-run resolver.
- `The current V8 SDK is release/RelWithDebInfo. Use -Preset windows-relwithdebinfo ...`: wrong preset with `-EnableV8`.
- `sloppy run: sloppy run requires V8-enabled build`: non-V8 runtime binary is being used.
