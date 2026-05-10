# C Safety Skill

Use this for C runtime code.

- Confirm ownership and lifetime comments.
- Check bounds, overflow, and truncation.
- Verify cleanup paths release every resource.
- Enforce allocator rules.
- Enforce `SlStr`/`SlBytes` string and byte rules.
- Check platform isolation.
- Check V8 isolation.
- Keep code sanitizer-ready.
- Require tests for success, failure, boundary, and cleanup behavior.
- Require C module docs to document ownership/lifetime rules and invariants.
- Require tests to cover documented ownership, lifetime, and invariant behavior.
- Overengineering can harm safety by hiding ownership, error paths, and bounds checks.
- Prefer code where ownership and failure are visible.
- C safety comments must make ownership/lifetime and invariants visible.
- Require comments near tricky bounds checks, arena lifetimes, resource IDs, and
  thread/engine ownership assumptions.
