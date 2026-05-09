# Development Scripts

`tools/windows/dev.ps1` is the canonical Windows entrypoint.

```powershell
.\tools\windows\dev.ps1 help
.\tools\windows\dev.ps1 doctor
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
.\tools\windows\dev.ps1 clean
.\tools\windows\dev.ps1 analyze
.\tools\windows\dev.ps1 package
.\tools\windows\dev.ps1 test-package
```

Command summary:

- `doctor`: dependency and environment checks.
- `configure`: configure a CMake preset.
- `build`: build configured targets.
- `test`: run CTest and compiler tests.
- `format-check`: C/C++ and Rust formatting checks.
- `lint`: standards, docs, governance, release-policy, and hygiene checks.
- `analyze`: advanced static-analysis target.
- `package`/`test-package`: create and test a package outside the checkout.

Unix wrappers use `tools/unix/dev.sh` with the same command names where
implemented.
