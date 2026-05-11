# Golden tests

Golden tests pin user-visible contracts that should not drift silently: CLI
output, Plan JSON, OpenAPI JSON, package manifests, diagnostics, generated
artifact shape, template output, example coverage, and alpha app flows.

Use goldens when the output is itself the contract. Use unit or integration
tests when the contract is behavior that can be asserted directly.

## Update workflow

Normal tests compare current output with committed goldens. Updating goldens is
explicit:

```powershell
tools/windows/update-goldens.ps1 -Area all
tools/windows/update-goldens.ps1 -Area cli -Verify
tools/windows/test-engine.ps1 -Tier pr -Area golden -Out artifacts/test-engine/golden.json
tools/windows/test-engine.ps1 -Tier pr -Area integration -Out artifacts/test-engine/integration.json
```

```sh
tools/unix/update-goldens.sh --area all
tools/unix/update-goldens.sh --area cli --verify
tools/unix/test-engine.sh --tier pr --area golden --out artifacts/test-engine/golden-unix.json
tools/unix/test-engine.sh --tier pr --area integration --out artifacts/test-engine/integration-unix.json
```

Do not update goldens automatically in CI. A PR that changes goldens must say
which contract changed and why the new output is intended.

## Normalization

The alpha proof harness is itself a Sloppy Program Mode tool at
`tools/golden/alpha-proof.ts`. Because it imports `sloppy/fs` and `sloppy/os`,
the runner process requires a V8-enabled `sloppy` binary. The wrapper scripts
use that V8 runner to verify either a V8 target binary or a non-V8 target
binary, depending on the selected target preset.

The harness normalizes machine-specific values before comparison:

- checkout, work, temp, and user paths;
- Windows and Unix line endings;
- local ports;
- timestamps and durations.

Do not run multiple `sloppy run tools/golden/alpha-proof.ts` invocations in
parallel from the same checkout. Source-input compilation uses the checkout's
`.sloppy` handoff directory.

It does not normalize meaningful behavior such as route order, Plan kind,
diagnostic wording, OpenAPI paths, package manifest shape, source-map metadata,
or whether generated artifacts exist.

Docs snippets use `tests/fixtures/docs-snippets/manifest.json` to assert that
public docs and template READMEs still contain commands covered by a named proof
lane. Manifest status values are precise:

- `checked` means the command appears in the document and is covered by the
  named proof lane. It is not executed by the docs-snippet proof.
- `executed` means the command is actually run by the docs-snippet proof.
- `skipped` means the command is not run and has an explicit reason.

Commands that require a registry install or long-running local server stay
listed with an explicit skip reason instead of being counted as passed execution
evidence. The current manifest is provisional while public alpha docs are still
being consolidated; do not cite it as final alpha documentation proof.

## Review checklist

When reviewing golden diffs, check:

- the changed output corresponds to a documented behavior change;
- JSON shape, route metadata, capabilities, and diagnostics remain strict;
- redacted values stay redacted;
- no absolute paths, temp names, ports, timestamps, or durations leaked;
- example entries are either smoke-tested or classified with a concrete reason;
- Program and Web app flows are covered separately;
- package goldens include manifest shape and artifact paths.

## Adding a case

Add the source fixture, template, example, or command to
`tools/golden/alpha-proof.ts`, then regenerate only the relevant area:

```powershell
tools/windows/update-goldens.ps1 -Area diagnostics
tools/windows/update-goldens.ps1 -Area diagnostics -Verify
```

If the case is an example, also update
`tests/golden/examples/examples.manifest.json`. The manifest must list every
directory under `examples/`; unlisted examples fail the examples lane.
