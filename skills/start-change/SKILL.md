---
name: start-change
description: Start a governed change in this repository. Use when beginning feature, fix, or chore work, choosing a branch, synchronizing with main, or checking the protected-branch contribution policy.
---

# Start Change

1. Inspect the working tree and current branch. Preserve unrelated user changes.
2. Fetch `origin/main`, then create the branch from it using exactly one of:
   - `feature/<kebab-title>` for new behavior
   - `fix/<kebab-title>` for a defect
   - `chore/<kebab-title>` for maintenance
3. Use `main` as the feature base and pull-request target. Do not directly push `main`.
4. State the target branch (`main`) and the validation expected for the scope. For release work, use `release-macos-plugin`.
5. Before handoff, synchronize with `origin/main` and resolve conflicts on the feature branch. Preserve a linear history.

Use Conventional Commit subjects. Treat global-capture changes as privacy-sensitive: never log keystroke contents and preserve the actionable Accessibility warning.
