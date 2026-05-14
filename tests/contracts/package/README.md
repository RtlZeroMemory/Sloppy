# Package Contract

The package contract validates `.sloppy/package` directories. It checks that
`manifest.json`, Plan metadata, bundled artifacts, migrations, route dispatch,
native libraries, and relocatable paths agree before a user tries to run the
package.

Run it directly:

```powershell
node tests/contracts/runner/contract-runner.mjs --area package --tier pr
node tests/contracts/runner/contract-runner.mjs --area package --tier pr --format markdown
```

The PR fixture suite contains one valid minimal package plus intentionally
broken package roots. Broken fixtures are expected to produce specific
invariant failures; the suite passes only when those failures are detected.

The current PR-tier validator checks:

- `manifest.json` exists, parses, and uses `schema: "sloppy.app-package.v1"`;
- manifest entry and artifact paths are package-relative, safe, and present;
- `app.plan.json`, `app.js`, optional source maps, and optional `routes.slrt`
  are present when referenced;
- Plan route-dispatch hashes match `routes.slrt`;
- manifest migrations and native libraries exist and match hashes when listed;
- manifest and Plan content do not leak source-checkout absolute paths;
- SQLite database paths are relocatable and their package parent directories
  exist when needed;
- token-looking files, `.env`, `.npmrc`, `node_modules`, build trees, and other
  obvious local junk are not included.
