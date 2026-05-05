# CORE-FS-01 Issue Index

Status: CORE-FS-01.A/B planning and source-of-truth index.

| Slice | Issues | PR intent |
| --- | --- | --- |
| CORE-FS-01.A/B | #539, #540 | API contract, feature descriptor, Plan metadata, capability vocabulary, development/strict policy. |
| CORE-FS-01.C/D/H | #541, #542, #546 | Path/root resolver, backend contract, async/offloaded core file operations, initial V8/stdlib surface. |
| CORE-FS-01.E/F | #543, #544 | Directory/metadata/symlink/temp/atomic/locking APIs plus FileHandle and filesystem streams. |
| CORE-FS-01.G | #545 | File watch API, bounded event queues, platform semantics, cleanup. |
| CORE-FS-01.I/J | #547, #548 | Diagnostics, doctor/audit, security goldens, conformance, examples, documentation closeout. |

Parent EPIC: #538 CORE-FS-01 FileSystem Runtime API.

## Feature And Capability Names

- Runtime feature id: `stdlib.fs`.
- Public import: `sloppy/fs`.
- Future V8 intrinsic namespace: `__sloppy.fs`.
- Plan capability kind: `filesystem`.
- Filesystem capability categories: `fs.read`, `fs.write`, `fs.append`, `fs.delete`,
  `fs.list`, `fs.metadata`, `fs.watch`, `fs.lock`.

## Non-Goals For The EPIC

- No Node `fs` compatibility promise.
- No public synchronous filesystem APIs for app runtime.
- No package-manager behavior.
- No benchmark/performance claims.
- No unrelated HTTP, network, process, crypto, provider, or module-runtime expansion.
- No public alpha docs.

## Completion Rule

The parent EPIC can close only after #539-#548 are closed and the final PR records evidence
for local gates, feature/capability behavior, diagnostics, examples, conformance, and the
explicit non-goals above.
