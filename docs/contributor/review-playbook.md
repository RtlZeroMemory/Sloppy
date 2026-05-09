# Review Playbook

Reviewers should spend attention on bugs that can break contracts, safety, or
the evidence trail.

Prioritize:

- correctness and resource lifetime;
- C/C++ memory safety;
- Rust panic and unsafe invariants;
- JavaScript descriptor and async cleanup drift;
- native boundary ownership and thread affinity;
- parser, Plan, route, protocol, and binary-format validation;
- concurrency and shutdown ordering;
- build, CI, and tooling regressions;
- tests that mirror implementation instead of documented intent;
- docs that overclaim unsupported behavior.

Use a blocking/non-blocking split in review comments.

Blocking examples:

- correctness or safety bugs;
- broken required gate;
- missing required behavior coverage;
- platform/V8 boundary violations;
- unsupported product claim in current docs.

Non-blocking examples:

- wording polish;
- optional refactor suggestion;
- naming preference when behavior is clear.
