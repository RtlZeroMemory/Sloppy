# Framework DI Services Example

A runnable example showing request-scoped dependency injection.

The app registers singleton, scoped, and transient services with literal service
tokens. The compiler emits service registration metadata, and the generated
runtime wrapper creates one request scope for handler injection.

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/framework-di-services/app.ts --once GET /di/42
```

## Scope

This example covers literal service registration and request-scoped injection.
Service scanning, external package modules, and decorator discovery are not
shown here.
