# CLI Starter

This Program Mode starter is shaped like a practical local tool. It has a
command dispatcher, subcommands, filesystem inspection, platform information,
clear exit codes, and the packaged CLI flow.

```sh
sloppy run src/main.ts -- --help
sloppy run src/main.ts -- echo hello
sloppy run src/main.ts -- inspect package.json
sloppy build
sloppy package
sloppy run .sloppy/package -- echo hello
```

Add commands under `src/commands/` and dispatch them from `src/main.ts`.
