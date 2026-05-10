# Docs to Issues Skill

Use this when converting docs into GitHub epics/tasks.

1. Start from `docs/project/README.md` to distinguish current contracts, issue snapshots,
   and archives.
2. Read the current source docs and ADRs before using historical planning records.
3. Search GitHub for duplicate, superseded, closed, or renamed issues before creating new
   work.
4. Create medium bounded-context tasks: one coherent reviewable slice with explicit
   non-goals.
5. Include source docs, acceptance criteria, tests/evidence lanes, docs updates, and
   deferred coverage in each issue.
6. Avoid issue-number bookkeeping that duplicates GitHub live state.
7. Archive or remove stale construction docs when a current issue/source doc supersedes
   them.
8. Do not create tasks that imply public alpha, production readiness, performance,
   package-readiness, provider-readiness, V8-readiness, or compatibility claims without a
   source doc and evidence requirement.
