# Sloppy Program

This template is a route-free Program Mode project for console tools, local
automation, and worker-style scripts.

## Run

```powershell
sloppy run -- --name Ada
```

`main(args, ctx)` receives arguments after `--` and a Program context with
`kind`, `cwd`, `environment`, and Plan metadata. `console.log` and
`console.info` write to stdout; `console.warn` and `console.error` write to
stderr.

Build and run artifacts:

```powershell
sloppy build
sloppy run .sloppy -- --name Ada
```
