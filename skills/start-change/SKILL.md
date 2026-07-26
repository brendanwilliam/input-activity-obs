---
name: start-change
description: Start a governed change in this repository. Use when beginning feature, fix, or chore work, choosing a branch, synchronizing with develop, or checking the protected-branch contribution policy.
---

# Start Change

1. Inspect the working tree and current branch. Preserve unrelated user changes.
2. Fetch `origin/develop`, then create the branch from it using exactly one of:
   - `feature/<kebab-title>` for new behavior
   - `fix/<kebab-title>` for a defect
   - `chore/<kebab-title>` for maintenance
3. Do not use `main` as a feature base or target. Do not directly push `develop` or `main`.
4. State the target branch (`develop`) and the validation expected for the scope. For release work, use `release-macos-plugin` and open the release PR from `develop` to `main`.
5. Before handoff, synchronize with `origin/develop` and resolve conflicts on the feature branch. Preserve a linear history.

Use Conventional Commit subjects. Treat global-capture changes as privacy-sensitive: never log keystroke contents and preserve the actionable Accessibility warning.
