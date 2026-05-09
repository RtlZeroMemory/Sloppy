# Framework v2 DI Services Example

This is an executable Framework v2 source-input example for request-scoped service
injection.

This example registers singleton, scoped, and transient services with literal service
tokens. The compiler emits service registration metadata, and the generated runtime wrapper
creates one request scope for handler injection.

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/framework-v2-di-services/app.ts --once GET /di/42
```

## Limitations

This example covers literal service registration and request-scoped injection.
Service scanning, external package modules, and decorator discovery are outside
this example.
