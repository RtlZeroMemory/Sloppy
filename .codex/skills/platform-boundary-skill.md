# Platform Boundary Skill

Use this for platform-sensitive changes.

- Enforce `src/platform/*` as the OS API boundary.
- Do not include OS headers in core modules.
- Update `docs/platform-abstraction.md` when platform behavior changes.
- Add scanner coverage if a new boundary rule appears.
- Keep core code cross-platform by design.
