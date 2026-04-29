# Unix Tools

Linux/macOS shell tooling belongs here.

`package.sh` is the first experimental TAR packaging path:

```sh
tools/unix/package.sh --configuration Release
```

It stages the same local archive layout as the Windows ZIP script and writes a `.tar.gz`
plus `SHA256SUMS.txt` under ignored `artifacts/packages/`. This script is intentionally
simple and is not yet validated by default Windows gates or cross-platform CI. EPIC-26 owns
Linux/macOS CI validation.
