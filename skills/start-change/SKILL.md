---
name: start-change
description: Start a governed change in this repository. Use when beginning feature, fix, or chore work, choosing a branch, synchronizing with develop, or checking the protected-branch contribution policy.
---

# Start Change

1. Inspect the working tree and current branch. Preserve unrelated user changes.
2. Fetch `origin/main` and `origin/develop`. Ensure `origin/develop` contains `origin/main`; if it does not,
   first synchronize `main` into `develop` through a pull request. Then create the branch from `origin/develop`
   using exactly one of:
   - `feature/<kebab-title>` for new behavior
   - `fix/<kebab-title>` for a defect
   - `chore/<kebab-title>` for maintenance
   - `chore/updates-YYYY-MM-DD` for a coordinated group of multiple tasks implemented that day; merge it to
     `develop` in one pull request after the group is ready
3. Use `develop` as the feature base and pull-request target. Do not directly push `develop` or `main`.
4. State the target branch (`develop`) and the validation expected for the scope. For release work, use `release-macos-plugin` and promote `develop` to `main`.
5. Before handoff, synchronize with `origin/develop` and resolve conflicts on the feature branch. Preserve a linear history.

Use a task-specific branch for isolated work. `develop` consolidates related work, but the final promotion to
`main` remains subject to its required checks. Use Conventional Commit subjects. Treat global-capture changes as
privacy-sensitive: never log keystroke contents and preserve the actionable Accessibility warning.
