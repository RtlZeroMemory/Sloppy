# V8 SDK

V8 execution is default-off. Use the resolver and V8 preset when touching
runtime, app-host, compiler, bootstrap, provider, configuration, or V8-adjacent
behavior.

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

If SDK resolution fails, keep the exact resolver error in your report and mark
the lane `UNAVAILABLE`.
