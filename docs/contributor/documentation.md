# Documentation

Current docs must come from current code, tests, scripts, examples, and command
output actually produced in this repository.

The repository-wide policy is [Documentation Policy](../documentation-policy.md).

Use the page type that matches the reader need:

- Tutorial: guided learning with a working result.
- How-to: one concrete task with exact steps.
- Reference: precise lookup material.
- Explanation: architecture, tradeoffs, and mental models.
- Contributor: operational repo work.
- Internals: implementation boundaries and invariants.

Do:

- update docs when behavior or workflow changes;
- include concrete commands and expected outcomes;
- keep status statements aligned with the checks that currently cover the behavior;
- delete stale pages and duplicated planning material.

Do not:

- add `Type:` or `Status:` metadata lines;
- keep planning notes or task transcripts as current docs;
- use fake examples;
- describe production, performance, compatibility, or release states beyond
  current validation.
